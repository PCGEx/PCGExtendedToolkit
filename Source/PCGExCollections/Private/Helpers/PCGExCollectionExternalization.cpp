// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionExternalization.h"

#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace PCGExSharedCompact
{
	FSoftObjectPath ExternalizeUObject(UObject* Source, const FString& DesiredPackagePath, const FString& DesiredAssetName)
	{
#if WITH_EDITOR
		if (!Source)
		{
			return FSoftObjectPath();
		}

		UPackage* CurrentPackage = Source->GetOutermost();
		if (CurrentPackage && CurrentPackage->GetName() == DesiredPackagePath && Source->GetName() == DesiredAssetName)
		{
			return FSoftObjectPath(Source);
		}

		UPackage* TargetPackage = CreatePackage(*DesiredPackagePath);
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
