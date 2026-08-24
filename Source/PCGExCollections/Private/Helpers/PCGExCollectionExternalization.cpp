// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionExternalization.h"

#include "PCGExLog.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace PCGExSharedCompact
{
	FString ResolveStaleMountPoint(const FString& InPath, const UObject* Owner)
	{
		if (InPath.IsEmpty() || InPath[0] != TEXT('/') || !Owner)
		{
			return InPath;
		}

		// Live mount (user /Game folders, or this plugin's own mount in a standalone
		// install): hands off.
		if (!FPackageName::GetPackageMountPoint(InPath).IsNone())
		{
			return InPath;
		}

		const UPackage* OwnerPackage = Owner->GetPackage();
		const FName OwnerMount = OwnerPackage ? FPackageName::GetPackageMountPoint(OwnerPackage->GetName()) : NAME_None;
		if (OwnerMount.IsNone())
		{
			// Transient owner -- nothing better to offer.
			return InPath;
		}

		// Graft the mount-relative remainder onto the owner's current mount.
		int32 SecondSlash = INDEX_NONE;
		if (!InPath.RightChop(1).FindChar(TEXT('/'), SecondSlash))
		{
			return InPath;
		}
		const FString Healed = FString::Printf(TEXT("/%s%s"), *OwnerMount.ToString(), *InPath.RightChop(1 + SecondSlash));
		UE_LOG(LogPCGEx, Log, TEXT("Remapped stale content path '%s' -> '%s' (owner: %s)"), *InPath, *Healed, *OwnerPackage->GetName());
		return Healed;
	}

	FSoftObjectPath ExternalizeUObject(UObject* Source, const FString& DesiredPackagePath, const FString& DesiredAssetName)
	{
#if WITH_EDITOR
		if (!Source)
		{
			return FSoftObjectPath();
		}

		// ExportFolder values authored under a re-mounted plugin (merged bundle, migrated
		// content) heal here, against the owning package's current mount.
		const FString ResolvedPackagePath = ResolveStaleMountPoint(DesiredPackagePath, Source);

		UPackage* CurrentPackage = Source->GetOutermost();
		if (CurrentPackage && CurrentPackage->GetName() == ResolvedPackagePath && Source->GetName() == DesiredAssetName)
		{
			// Already in place -- taken on every rebuild after the first externalization (names are
			// GUID-stable). Callers invoke this right after regenerating Source's content, so the
			// package must dirty here too or in-place refills never reach disk.
			CurrentPackage->MarkPackageDirty();
			return FSoftObjectPath(Source);
		}

		UPackage* TargetPackage = CreatePackage(*ResolvedPackagePath);
		check(TargetPackage);

		// An on-disk destination not loaded this session comes back as an unloaded stub, which
		// SavePackage refuses to overwrite; fully load it first (also materializes the occupant
		// evicted below). No-op for brand-new packages.
		if (!TargetPackage->IsFullyLoaded())
		{
			TargetPackage->FullyLoad();
		}

		// Evict any occupant of the target name -- renaming onto a conflict asserts (happens on
		// rebuilds when the previous externalized asset is still loaded).
		const FName TargetName(*DesiredAssetName);
		if (UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), TargetPackage, TargetName))
		{
			if (Existing != Source)
			{
				Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}

		Source->Rename(*DesiredAssetName, TargetPackage, REN_DontCreateRedirectors | REN_NonTransactional);
		Source->SetFlags(RF_Public | RF_Standalone);
		TargetPackage->SetFlags(RF_Public | RF_Standalone);
		TargetPackage->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(Source);

		return FSoftObjectPath(Source);
#else
		return Source ? FSoftObjectPath(Source) : FSoftObjectPath();
#endif
	}
}
