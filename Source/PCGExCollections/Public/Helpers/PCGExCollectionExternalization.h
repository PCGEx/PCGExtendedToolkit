// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectGlobals.h"

/**
 * Subobject <-> standalone-asset externalization helpers, lifted to a public header (like
 * PCGExCollectionSortKeys.h) so UPCGExPCGDataAssetCollection and the Valency bonding rules share
 * one rename-out / rename-back cycle. Editor-only; non-editor builds no-op.
 */
namespace PCGExSharedCompact
{
	/**
	 * Resolve a ContentDir-style package path whose mount point may no longer exist.
	 * ContentDir paths (ExportFolder pickers) are authored alongside their owning asset, under
	 * its plugin's mount; a merged bundle or a content migration moves both while preserving the
	 * mount-relative layout. A path under a live mount is returned untouched (user /Game folders,
	 * standalone installs); a stale mount is substituted with the owning package's current mount,
	 * which reconstructs the folder's merged/migrated location.
	 */
	PCGEXCOLLECTIONS_API FString ResolveStaleMountPoint(const FString& InPath, const UObject* Owner);

	/**
	 * Rename Source into DesiredPackagePath as DesiredAssetName (evicting any occupant to transient
	 * first) and notify the asset registry. Idempotent. Source keeps its identity, so a hard pointer
	 * to it stays valid afterward. Returns the resulting soft path (empty if Source was null).
	 * The target package is ALWAYS marked dirty, including the already-in-place path -- callers
	 * invoke this right after regenerating Source's content.
	 * DesiredPackagePath is routed through ResolveStaleMountPoint (Source's package as owner), so
	 * ExportFolder values authored under a re-mounted plugin heal transparently.
	 */
	PCGEXCOLLECTIONS_API FSoftObjectPath ExternalizeUObject(UObject* Source, const FString& DesiredPackagePath, const FString& DesiredAssetName);

	/**
	 * Reverse of ExternalizeUObject (soft-external variant): load External, rename it back into
	 * NewOuter, populate Instanced, reset External. Skips when Instanced is already set so a live
	 * working buffer is never overwritten by a stale on-disk copy.
	 */
	template <typename T>
	void Internalize(TObjectPtr<T>& Instanced, TSoftObjectPtr<T>& External, UObject* NewOuter)
	{
#if WITH_EDITOR
		if (!Instanced && !External.IsNull())
		{
			if (T* Loaded = External.LoadSynchronous())
			{
				Loaded->Rename(nullptr, NewOuter, REN_DontCreateRedirectors | REN_NonTransactional);
				Loaded->ClearFlags(RF_Public | RF_Standalone);
				Instanced = Loaded;
			}
		}
		External.Reset();
#endif
	}

	/**
	 * Reverse of ExternalizeUObject (hard-external variant): same as above but no load needed, since
	 * a hard TObjectPtr is always resident.
	 */
	template <typename T>
	void Internalize(TObjectPtr<T>& Instanced, TObjectPtr<T>& External, UObject* NewOuter)
	{
#if WITH_EDITOR
		if (!Instanced && External)
		{
			External->Rename(nullptr, NewOuter, REN_DontCreateRedirectors | REN_NonTransactional);
			External->ClearFlags(RF_Public | RF_Standalone);
			Instanced = External;
		}
		External = nullptr;
#endif
	}
}
