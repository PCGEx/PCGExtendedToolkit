// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExGetCollectionDataFlatten.h"

#include "PCGContext.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Elements/PCGExGetCollectionData.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"

namespace PCGExGetCollectionData
{
	void ProcessEntry(
		const FProcessEntryContext& Ctx,
		const FPCGExAssetCollectionEntry* InEntry,
		const UPCGExAssetCollection* InHost,
		TArray<FFlattenedEntry>& OutEntries,
		const FName EffectiveParentCategory,
		const int32 RootCollectionIndex,
		const int32 Depth,
		TSet<uint64>& GUIDS)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetCollectionData::ProcessEntry);

		// bNoDuplicates: pointer-identical entries reached through multiple subcollection paths are
		// deduped. TSet lookup is O(1) per call -- was O(N) linear scan of OutEntries, quadratic
		// over collection size with the default-on flag.
		const bool bDedup = Ctx.bNoDuplicates && Ctx.SeenEntries != nullptr;
		if (bDedup && Ctx.SeenEntries->Contains(InEntry))
		{
			return;
		}

		auto AddNone = [&]()
		{
			if (Ctx.bOmitInvalidAndEmpty)
			{
				return;
			}
			// Null entries inherit RootCollectionIndex (they belong to a root that resolved to nothing
			// at this slot) but have no host -> CollectionIndex stays -1. Depth is the recursion level
			// at which the slot was attempted. Hash is keyed off this (Root, -1, Depth) tuple, so
			// null rows from the same root+depth group together under one hash.
			const int32 NoneHash = GetOrAssignCollectionHash(Ctx, RootCollectionIndex, -1, Depth);
			OutEntries.Add({nullptr, nullptr, NAME_None, RootCollectionIndex, -1, Depth, NoneHash});
			if (bDedup)
			{
				Ctx.SeenEntries->Add(nullptr);
			}
		};

		if (!InEntry)
		{
			AddNone();
			return;
		}

		if (!Ctx.CategoryFilters->Test(InEntry->Category.ToString()))
		{
			return;
		}

		auto ResolveCategory = [&](const FName Authored) -> FName
		{
			switch (Ctx.CategoryInheritance)
			{
			case EPCGExCategoryInheritance::FillEmpty:
				return Authored.IsNone() ? EffectiveParentCategory : Authored;
			case EPCGExCategoryInheritance::Replace:
				return EffectiveParentCategory.IsNone() ? Authored : EffectiveParentCategory;
			default:
				return Authored;
			}
		};

		const int32 HostCollectionIndex = GetOrAssignCollectionIndex(Ctx, InHost);
		// All non-recursing add sites below share the same (Root, HostColl, Depth) identity tuple,
		// so the hash is computed once here and reused.
		const int32 EntryHash = GetOrAssignCollectionHash(Ctx, RootCollectionIndex, HostCollectionIndex, Depth);

		auto AddEmpty = [&](const FPCGExAssetCollectionEntry* S)
		{
			if (Ctx.bOmitInvalidAndEmpty)
			{
				return;
			}
			OutEntries.Add({S, InHost, NAME_None, RootCollectionIndex, HostCollectionIndex, Depth, EntryHash});
			if (bDedup)
			{
				Ctx.SeenEntries->Add(S);
			}
		};

		if (!InEntry->bIsSubCollection)
		{
			OutEntries.Add({InEntry, InHost, ResolveCategory(InEntry->Category), RootCollectionIndex, HostCollectionIndex, Depth, EntryHash});
			if (bDedup)
			{
				Ctx.SeenEntries->Add(InEntry);
			}
			return;
		}

		if (Ctx.SubHandling == EPCGExSubCollectionToSet::Ignore)
		{
			return;
		}

		if (Ctx.SubHandling == EPCGExSubCollectionToSet::Grammar)
		{
			if (InEntry->SubGrammarMode != EPCGExGrammarSubCollectionMode::Flatten)
			{
				// Sub-collection emitted as a single placeholder row -- depth stays at the host's depth
				// (the entry physically lives in InHost; we didn't recurse into its contents).
				OutEntries.Add({InEntry, InHost, ResolveCategory(InEntry->Category), RootCollectionIndex, HostCollectionIndex, Depth, EntryHash});
				if (bDedup)
				{
					Ctx.SeenEntries->Add(InEntry);
				}
				return;
			}
		}

		UPCGExAssetCollection* SubCollection = InEntry->Staging.LoadSync<UPCGExAssetCollection>(Ctx.Context);
		const PCGExAssetCollection::FCache* SubCache = SubCollection ? SubCollection->LoadCache() : nullptr;

		if (!SubCache)
		{
			AddEmpty(InEntry);
			return;
		}

		bool bVisited = false;
		GUIDS.Add(SubCollection->GetUniqueID(), &bVisited);
		if (bVisited)
		{
			return;
		}

		const FName NextParent = InEntry->Category.IsNone() ? EffectiveParentCategory : InEntry->Category;
		const int32 NextDepth = Depth + 1;

		FPCGExEntryAccessResult SubResult;
		switch (Ctx.SubHandling)
		{
		default: ;
		case EPCGExSubCollectionToSet::Grammar:
		case EPCGExSubCollectionToSet::Expand:
			for (int32 i = 0; i < SubCache->Main->Order.Num(); i++)
			{
				SubResult = SubCollection->GetEntryAt(i);
				ProcessEntry(Ctx, SubResult.Entry, SubResult.Host, OutEntries, NextParent, RootCollectionIndex, NextDepth, GUIDS);
			}
			return;
		case EPCGExSubCollectionToSet::PickRandom:
			SubResult = SubCollection->GetEntryRandom(0);
			break;
		case EPCGExSubCollectionToSet::PickRandomWeighted:
			SubResult = SubCollection->GetEntryWeightedRandom(0);
			break;
		case EPCGExSubCollectionToSet::PickFirstItem:
			SubResult = SubCollection->GetEntryAt(0);
			break;
		case EPCGExSubCollectionToSet::PickLastItem:
			SubResult = SubCollection->GetEntryAt(SubCache->Main->Indices.Num() - 1);
			break;
		}

		ProcessEntry(Ctx, SubResult.Entry, SubResult.Host, OutEntries, NextParent, RootCollectionIndex, NextDepth, GUIDS);
	}

	void FlattenCollection(
		const FProcessEntryContext& Ctx,
		UPCGExAssetCollection* Collection,
		const int32 RootCollectionIndex,
		TArray<FFlattenedEntry>& OutEntries)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetCollectionData::FlattenCollection);

		if (!Collection)
		{
			return;
		}

		const PCGExAssetCollection::FCache* MainCache = Collection->LoadCache();
		if (!MainCache)
		{
			return;
		}

		TSet<uint64> GUIDS;
		for (int32 i = 0; i < MainCache->Main->Order.Num(); i++)
		{
			GUIDS.Reset();
			FPCGExEntryAccessResult Result = Collection->GetEntryAt(i);
			ProcessEntry(Ctx, Result.Entry, Result.Host, OutEntries, NAME_None, RootCollectionIndex, /*Depth=*/0, GUIDS);
		}
	}

	FSoftObjectPath ReadSinglePath(FPCGExContext* InContext, const UPCGData* InData, const FName AttributeName)
	{
		if (!InData)
		{
			return FSoftObjectPath();
		}
		FSoftObjectPath Path;
		if (PCGExData::Helpers::TryReadDataValue<FSoftObjectPath>(InContext, InData, AttributeName, Path, /*bQuiet=*/true))
		{
			return Path;
		}
		FString PathStr;
		if (PCGExData::Helpers::TryReadDataValue<FString>(InContext, InData, AttributeName, PathStr, /*bQuiet=*/false))
		{
			return FSoftObjectPath(PathStr);
		}
		return FSoftObjectPath();
	}

	int64 ReadSingleHash(FPCGExContext* InContext, const UPCGData* InData, const FName AttributeName)
	{
		if (!InData)
		{
			return 0;
		}
		int64 Hash = 0;
		PCGExData::Helpers::TryReadDataValue<int64>(InContext, InData, AttributeName, Hash, /*bQuiet=*/false);
		return Hash;
	}

	bool ParseSourceInputsIntoSlots(
		FPCGExGetCollectionDataContext* Context,
		const UPCGExGetCollectionDataSettings* Settings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetCollectionData::ParseSourceInputsIntoSlots);

		FPCGExContext* InContext = Context;

		PCGEX_VALIDATE_NAME_C(InContext, Settings->SourceAttribute)

		TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(SourcesPin);
		if (Inputs.IsEmpty())
		{
			return true;
		} // nothing to do

		// EntryIdAndMap: resolve hashes to sub-collection paths via the unpacker.
		TSharedPtr<PCGExCollections::FPickUnpacker> Unpacker;
		if (Settings->SourceShape == EPCGExGetCollectionDataSourceShape::EntryIdAndMap)
		{
			Unpacker = MakeShared<PCGExCollections::FPickUnpacker>();
			Unpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);
			if (!Unpacker->HasValidMapping())
			{
				PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("EntryIdAndMap mode requires a valid Collection Map on the Map pin."));
				return false;
			}
		}

		const bool bDataDomainOnly = Settings->Fanout == EPCGExGetCollectionDataFanout::PerInputData;
		const bool bIsSoftPath = Settings->SourceShape == EPCGExGetCollectionDataSourceShape::SoftPath;
		const FName SourceAttribute = Settings->SourceAttribute;

		// Per-input scratch: paths (SoftPath mode) or hashes (EntryIdAndMap mode).
		// We split path resolution into two passes so we can parallelize each independently.
		struct FInputParse
		{
			TArray<FSoftObjectPath> Paths;
			TArray<int64> Hashes;
		};
		TArray<FInputParse> PerInput;
		PerInput.SetNum(Inputs.Num());

		// Phase A: parallel per-input bulk read. Each iteration owns its own PerInput[i] slot.
		// Bulk GetRange replaces hundreds of read-locked GetValueFromItemKey calls with one.
		PCGExMT::ParallelOrSequential(Inputs.Num(), [&](const int32 i)
		{
			const FPCGTaggedData& TD = Inputs[i];
			FInputParse& Out = PerInput[i];

			if (bDataDomainOnly)
			{
				// One value per input (regardless of read success -> empty slot on failure).
				if (bIsSoftPath)
				{
					Out.Paths.Add(ReadSinglePath(InContext, TD.Data, SourceAttribute));
				}
				else
				{
					Out.Hashes.Add(ReadSingleHash(InContext, TD.Data, SourceAttribute));
				}
			}
			else
			{
				if (bIsSoftPath)
				{
					PCGExData::Helpers::BulkReadRows<FSoftObjectPath>(TD.Data, SourceAttribute, Out.Paths);
				}
				else
				{
					PCGExData::Helpers::BulkReadRows<int64>(TD.Data, SourceAttribute, Out.Hashes);
				}
			}
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		// Phase B (single-threaded): pre-size Slots, write SourceInputIndex backrefs and -- for
		// SoftPath -- the paths themselves. For EntryIdAndMap we leave Path empty and fill it in phase C.
		int32 TotalRows = 0;
		for (const FInputParse& P : PerInput)
		{
			TotalRows += bIsSoftPath ? P.Paths.Num() : P.Hashes.Num();
		}
		Context->Slots.SetNum(TotalRows);

		TArray<int64> FlatHashes; // only used for EntryIdAndMap
		if (!bIsSoftPath)
		{
			FlatHashes.SetNumUninitialized(TotalRows);
		}

		{
			int32 Cursor = 0;
			for (int32 i = 0; i < Inputs.Num(); i++)
			{
				const FInputParse& P = PerInput[i];
				const int32 RowCount = bIsSoftPath ? P.Paths.Num() : P.Hashes.Num();
				for (int32 r = 0; r < RowCount; r++)
				{
					FPCGExGetCollectionDataContext::FSlot& Slot = Context->Slots[Cursor];
					Slot.SourceInputIndex = i;
					if (bIsSoftPath)
					{
						Slot.Path = P.Paths[r];
					}
					else
					{
						FlatHashes[Cursor] = P.Hashes[r];
					}
					Cursor++;
				}
			}
		}

		// Phase C: parallel hash -> sub-collection path resolution (EntryIdAndMap only).
		// FPickUnpacker::UnpackHash is read-only on CollectionMap after UnpackPin, safe for concurrent reads.
		if (!bIsSoftPath)
		{
			PCGExMT::ParallelOrSequential(TotalRows, [&](const int32 i)
			{
				const int64 Hash = FlatHashes[i];
				int16 OutPrimary = 0;
				int16 OutSecondary = 0;
				UPCGExAssetCollection* HostCollection = Unpacker->UnpackHash(static_cast<uint64>(Hash), OutPrimary, OutSecondary);
				if (!HostCollection)
				{
					return;
				}
				FPCGExEntryAccessResult AccessResult = HostCollection->GetEntryRaw(OutPrimary);
				const FPCGExAssetCollectionEntry* Entry = AccessResult.Entry;
				if (!Entry || !Entry->bIsSubCollection)
				{
					return;
				} // leaf -> empty slot (per design)
				Context->Slots[i].Path = Entry->Staging.Path;
			}, /*Threshold=*/64, EParallelForFlags::None);
		}

		// Batch-load all unique valid paths synchronously. The framework's async LoadAssets path
		// (AddAssetDependency) defers AdvanceWork to the next tick, which on cold loads stacks a
		// full frame of wall-clock latency on top of the load itself. LoadBlocking_AnyThread keeps
		// everything in this frame: warm-cache resolves are instant, cold loads stall a worker
		// thread briefly -- same total cost as the load itself, no extra frame.
		{
			TSharedRef<TSet<FSoftObjectPath>> UniquePaths = MakeShared<TSet<FSoftObjectPath>>();
			UniquePaths->Reserve(Context->Slots.Num());
			for (const FPCGExGetCollectionDataContext::FSlot& Slot : Context->Slots)
			{
				if (Slot.Path.IsValid())
				{
					UniquePaths->Add(Slot.Path);
				}
			}
			if (!UniquePaths->IsEmpty())
			{
				PCGExHelpers::LoadBlockingTracked_AnyThread(TSharedPtr<TSet<FSoftObjectPath>>(UniquePaths), InContext);
			}

			// Resolve into ResolvedCollections now that everything's in memory.
			for (const FSoftObjectPath& Path : *UniquePaths)
			{
				Context->ResolvedCollections.Add(Path, Cast<UPCGExAssetCollection>(Path.ResolveObject()));
			}
		}

		return true;
	}
}
