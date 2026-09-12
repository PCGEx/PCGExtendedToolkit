// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Helpers/PCGExCollectionAssetSave.h"

#include "PCGElement.h"
#include "PCGParamData.h"

#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExAssetSaveTargetDetails.h"
#include "Details/PCGExRoamingAssetCollectionDetails.h"
#include "Misc/Crc.h"
#include "Templates/TypeHash.h"

#define LOCTEXT_NAMESPACE "PCGExCollectionAssetSave"

namespace PCGExCollectionSave
{
	int32 MakeStableEntryId(const FSoftObjectPath& InPath, const int32 InOccurrence)
	{
		// StrCrc32 folds every char width to 32 bits by design, so the value matches across platforms.
		const FString PathString = InPath.ToString();
		const uint32 Hash = HashCombine(FCrc::StrCrc32<TCHAR>(*PathString), static_cast<uint32>(InOccurrence));

		const int32 Id = static_cast<int32>(Hash);
		return Id == 0 ? 1 : Id;
	}

	void StampStableEntryIds(UPCGExAssetCollection* InCollection)
	{
		if (!InCollection) { return; }

		TMap<FSoftObjectPath, int32> Occurrences;
		TSet<int32> Used;
		Used.Reserve(InCollection->NumEntries());

		InCollection->ForEachEntry([&Occurrences, &Used](FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (!Entry) { return; }

			const FSoftObjectPath& Path = Entry->Staging.Path;
			if (Path.IsNull())
			{
				Entry->EntryId = 0;
				return;
			}

			// Bumping the occurrence on a collision keeps the result deterministic for a given row order.
			int32& Occurrence = Occurrences.FindOrAdd(Path, 0);
			int32 Id = MakeStableEntryId(Path, Occurrence++);
			while (Used.Contains(Id)) { Id = MakeStableEntryId(Path, Occurrence++); }

			Used.Add(Id);
			Entry->EntryId = Id;
		});
	}

	UPCGExAssetCollection* SaveOmniFromAttributeSet(
		const FPCGExAssetSaveTargetDetails& InTarget,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		const FPCGExRoamingAssetCollectionDetails& InDetails)
	{
#if WITH_EDITOR
		if (!InContext || !InAttributeSet) { return nullptr; }

		// Dry run into a throwaway first: rewriting the target guts it, and a build that then failed would
		// leave the asset empty with every EntryId gone. Staging off, so this loads nothing.
		UPCGExOmniCollection* Probe = NewObject<UPCGExOmniCollection>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Probe || !PCGExCollectionHelpers::BuildFromAttributeSet(Probe, InContext, InAttributeSet, InDetails, /*bBuildStaging=*/false))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("BuildFailed", "Failed to build the collection to save from the input attribute set; the target asset was left untouched."));
			return nullptr;
		}

		bool bCreated = false;
		UPCGExOmniCollection* Target = Cast<UPCGExOmniCollection>(
			PCGExAssetSave::FindOrCreateAsset(InTarget, UPCGExOmniCollection::StaticClass(), InContext, bCreated));

		if (!Target) { return nullptr; }

		if (!bCreated)
		{
			// Every row is rewritten below; unretired subobjects linger as unreferenced inners in the package.
			Target->ForEachEntry([Target](FPCGExAssetCollectionEntry* Entry, const int32 Index)
			{
				if (Entry) { PCGExCollectionHelpers::RetireInstancedSubobjects(Target->EDITOR_GetEntryScriptStruct(Index), Entry); }
			});

			Target->InitNumEntries(0);

			// BuildFromAttributeSet appends schemas; without this the set duplicates on every re-save.
			Target->CollectionProperties.Schemas.Reset();
		}

		if (!PCGExCollectionHelpers::BuildFromAttributeSet(Target, InContext, InAttributeSet, InDetails, /*bBuildStaging=*/true))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("BuildIntoTargetFailed", "Failed to build the collection into the target asset."));
			return nullptr;
		}

		// Before the rebuild's SyncEntryIds pass, which only re-mints ids that are 0 or duplicated.
		StampStableEntryIds(Target);

		// A runtime-built collection has no type globals, no staging fingerprints and no thumbnail.
		Target->EDITOR_StampSchemaVersionsCurrent();
		Target->EDITOR_EnsureTypeSetup();
		Target->EDITOR_CleanupUnusedCategoryOverrides();
		Target->EDITOR_RebuildStagingData();

		PCGExAssetSave::FinalizeAsset(InTarget, Target, bCreated, InContext);
		return Target;
#else
		return nullptr;
#endif
	}
}

#undef LOCTEXT_NAMESPACE
