// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "MeshSelectors/PCGExMeshSelectorStaged.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGStaticMeshSpawnerContext.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"

#include "Collections/PCGExMeshCollection.h"
#include "Engine/StaticMesh.h"
#include "Helpers/PCGExMaterialSwapTable.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Tasks/Task.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExMeshSelectorStaged)

#define LOCTEXT_NAMESPACE "PCGExMeshSelectorStaged"

namespace PCGExMeshSelectorStaged
{
	// An ISM renders all its instances with one cull mode, so mirrored points move to a twin list with Reverse Culling
	// flipped. Nanite flips mirrored instances on its own and ignores the flag (SUPPORT_REVERSE_CULLING_IN_NANITE is 0).
	void SplitMirroredInstances(FPCGMeshInstanceList& InstanceList, const TConstPCGValueRange<FTransform>& InTransforms, TArray<FPCGMeshInstanceList>& OutMirroredLists)
	{
		TArray<TPair<int32, FPCGMeshInstanceList>> Splits;
		const int32 Kept = SplitByKey(InstanceList, [&InTransforms](const int32 PointIndex) { return InTransforms[PointIndex].GetDeterminant() < 0 ? 1 : 0; }, 0, Splits);

		if (Kept == 1)
		{
			// Every point is mirrored: flip this list instead of leaving an empty one.
			InstanceList.Descriptor.bReverseCulling = !InstanceList.Descriptor.bReverseCulling;
			return;
		}

		for (TPair<int32, FPCGMeshInstanceList>& Split : Splits)
		{
			FPCGMeshInstanceList& Mirrored = OutMirroredLists.Add_GetRef(MoveTemp(Split.Value));
			Mirrored.Descriptor.bReverseCulling = !Mirrored.Descriptor.bReverseCulling;
			Mirrored.Instances.Reserve(Mirrored.InstancesIndices.Num());
			for (const int32 PointIndex : Mirrored.InstancesIndices)
			{
				Mirrored.Instances.Emplace(InTransforms[PointIndex]);
			}
		}
	}

	void ReadSwapStamps(const UPCGBasePointData* InPointData, const FName InLayer, TArray<uint32>& OutTables)
	{
		const UPCGMetadata* Metadata = InPointData->ConstMetadata();
		const FPCGMetadataAttribute<int32>* Stamp = Metadata ? Metadata->GetConstTypedAttribute<int32>(PCGExCollections::Labels::MaterialSwapIdName(InLayer)) : nullptr;
		if (!Stamp)
		{
			return;
		}

		const TConstPCGValueRange<int64> MetadataEntries = InPointData->GetConstMetadataEntryValueRange();
		OutTables.SetNumUninitialized(MetadataEntries.Num());
		for (int32 PointIndex = 0; PointIndex < MetadataEntries.Num(); PointIndex++)
		{
			OutTables[PointIndex] = static_cast<uint32>(Stamp->GetValueFromItemKey(MetadataEntries[PointIndex]));
		}
	}

	FOutcomeSplit SplitByOutcome(FPCGMeshInstanceList& InOutList, const TConstArrayView<uint32> InPointTables, const PCGExCollections::FMaterialSwapLibrary& InLibrary, const FName InLayer, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, const TConstArrayView<TSoftObjectPtr<UMaterialInterface>> InMeshMaterials, TArray<FPCGMeshInstanceList>& OutLists)
	{
		FOutcomeSplit Result;
		if (InPointTables.IsEmpty() || InOutList.InstancesIndices.IsEmpty())
		{
			return Result;
		}

		const FPCGSoftISMComponentDescriptor Base = InOutList.Descriptor;

		// Each distinct table resolves once, to the index of the descriptor it makes.
		TArray<uint32, TInlineAllocator<4>> Tables;
		TArray<int32, TInlineAllocator<4>> OutcomeOfTable;
		TArray<bool, TInlineAllocator<4>> UnknownOfTable;
		TArray<FPCGSoftISMComponentDescriptor> Outcomes;

		auto ResolveOutcome = [&](const uint32 InTable, bool& bOutUnknown) -> int32
		{
			if (const int32 Seen = Tables.IndexOfByKey(InTable); Seen != INDEX_NONE)
			{
				bOutUnknown = UnknownOfTable[Seen];
				return OutcomeOfTable[Seen];
			}

			const PCGExCollections::FMaterialSwapLibrary::FStages* Stages = InTable ? InLibrary.Find(InLayer, InTable) : nullptr;
			bOutUnknown = InTable && !Stages;

			FPCGSoftISMComponentDescriptor Descriptor = Base;
			if (Stages)
			{
				Result.bAnyTable = true;
				PCGExCollections::FMaterialSwapTable::Apply(*Stages, InEntry, InHost, InMeshMaterials, Descriptor);
			}

			// Apply only writes the material fields, so they alone tell two outcomes of one base apart.
			int32 Outcome = Outcomes.IndexOfByPredicate([&Descriptor](const FPCGSoftISMComponentDescriptor& Candidate)
			{
				return Candidate.OverrideMaterials == Descriptor.OverrideMaterials && Candidate.OverlayMaterial == Descriptor.OverlayMaterial;
			});

			if (Outcome == INDEX_NONE)
			{
				Outcome = Outcomes.Add(MoveTemp(Descriptor));
			}

			Tables.Add(InTable);
			OutcomeOfTable.Add(Outcome);
			UnknownOfTable.Add(bOutUnknown);
			return Outcome;
		};

		// Points of one input mostly share a table: resolve a run once.
		uint32 LastTable = 0;
		int32 LastOutcome = INDEX_NONE;
		bool bLastUnknown = false;

		TArray<TPair<int32, FPCGMeshInstanceList>> Splits;
		const int32 Kept = SplitByKey(InOutList, [&](const int32 PointIndex)
		{
			const uint32 Table = InPointTables[PointIndex];
			if (LastOutcome == INDEX_NONE || Table != LastTable)
			{
				LastTable = Table;
				LastOutcome = ResolveOutcome(Table, bLastUnknown);
			}

			if (bLastUnknown) { Result.NumUnknown++; }
			return LastOutcome;
		}, 0, Splits);

		InOutList.Descriptor = Outcomes[Kept];
		for (TPair<int32, FPCGMeshInstanceList>& Split : Splits)
		{
			FPCGMeshInstanceList& List = OutLists.Add_GetRef(MoveTemp(Split.Value));
			List.Descriptor = Outcomes[Split.Key];
		}

		return Result;
	}
}

bool UPCGExMeshSelectorStaged::SelectMeshInstances(FPCGStaticMeshSpawnerContext& Context, const UPCGStaticMeshSpawnerSettings* Settings, const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& OutMeshInstances, UPCGBasePointData* OutPointData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExMeshSelectorStaged::SelectInstances);

	if (!InPointData)
	{
		PCGE_LOG_C(Error, GraphAndLog, &Context, LOCTEXT("InputMissingData", "Missing input data"));
		return true;
	}

	if (!InPointData->Metadata)
	{
		PCGE_LOG_C(Error, GraphAndLog, &Context, LOCTEXT("InputMissingMetadata", "Unable to get metadata from input"));
		return true;
	}

	const FName EntryIdxAttributeName = PCGExCollections::Labels::EntryIdxName(StagingLayer);
	const FPCGMetadataAttribute<int64>* HashAttribute = PCGExMetaHelpers::TryGetConstAttribute<int64>(InPointData->Metadata, EntryIdxAttributeName);

	if (!HashAttribute)
	{
		if (!bQuietMissingStagingDataWarning)
		{
			PCGE_LOG_C(Error, GraphAndLog, &Context, FTEXT("Unable to get hash attribute from input. Enable 'Quiet Missing Staging Data Warning' to silence this."));
		}

		if (OutPointData)
		{
			OutPointData->SetNumPoints(0);
		}
		return true;
	}

	if (Context.CurrentPointIndex == 0)
	{
		// First time init

		if (OutPointData && bOutputPoints)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExMeshSelectorStaged::SetupOutPointData);

			const int32 NumPoints = InPointData->GetNumPoints();
			OutPointData->SetNumPoints(NumPoints);
			InPointData->CopyPointsTo(OutPointData, 0, 0, InPointData->GetNumPoints());

			OutPointData->Metadata->DeleteAttribute(EntryIdxAttributeName);

			const FName SwapStampName = PCGExCollections::Labels::MaterialSwapIdName(StagingLayer);
			if (OutPointData->Metadata->HasAttribute(SwapStampName))
			{
				OutPointData->Metadata->DeleteAttribute(SwapStampName);
			}
		}
	}

	// 1- Build collection map from override attribute set
	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionMap = MakeShared<PCGExCollections::FPickUnpacker>();
	CollectionMap->EntryIdxAttributeName = EntryIdxAttributeName;

	CollectionMap->UnpackPin(&Context, PCGPinConstants::DefaultParamsLabel);

	if (!CollectionMap->HasValidMapping())
	{
		PCGE_LOG_C(Error, GraphAndLog, &Context, FTEXT( "Unable to find Staging Map data in overrides"));
		return true;
	}

	if (!bUseTimeSlicing)
	{
		// Partition & write points in one go
		if (!CollectionMap->BuildPartitions(InPointData, OutMeshInstances))
		{
			PCGE_LOG_C(Error, GraphAndLog, &Context, FTEXT( "Unable to build any partitions"));
			return true;
		}
	}
	else
	{
		// Recover the partitions the previous slice built, rather than re-deriving them from points:
		// BuildPartitions would re-insert every index already placed and orphan the earlier lists.
		CollectionMap->ReindexPartitions(OutMeshInstances);

		const int32 NumPoints = InPointData->GetNumPoints();

		if (Context.CurrentPointIndex != NumPoints)
		{
			TConstPCGValueRange<int64> MetadataEntries = InPointData->GetConstMetadataEntryValueRange();
			while (Context.CurrentPointIndex < NumPoints)
			{
				CollectionMap->InsertEntry(InPointData, HashAttribute->GetValueFromItemKey(MetadataEntries[Context.CurrentPointIndex]), Context.CurrentPointIndex, OutMeshInstances);
				Context.CurrentPointIndex++;
				if (Context.ShouldStop())
				{
					return false;
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExMeshSelectorStaged::SelectEntries);

		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		// Read once, in the final pass: time-sliced calls return before reaching here. A point follows the table its stamp
		// names (the last Staging : Swap it passed); a point without one keeps its materials.
		PCGExCollections::FMaterialSwapLibrary MaterialSwaps;
		TArray<uint32> PointTables;
		if (bApplyMaterialSwaps)
		{
			MaterialSwaps.UnpackPin(&Context, PCGPinConstants::DefaultParamsLabel);
			PCGExMeshSelectorStaged::ReadSwapStamps(InPointData, StagingLayer, PointTables);
		}

		// Points whose stamp names a table the Overrides pin doesn't carry.
		int32 NumUnknownTables = 0;

		// Hosts of entries the table could only partly reach (no slot snapshot yet).
		TSet<const UPCGExAssetCollection*> UnstagedHosts;

		// Appended only after the loop: Partition.Value indexes OutMeshInstances, which must not grow mid-iteration.
		TArray<FPCGMeshInstanceList> MirroredLists;
		TArray<FPCGMeshInstanceList> OutcomeLists;

		for (const TPair<int64, int32>& Partition : CollectionMap->IndexedPartitions)
		{
			const FPCGExMeshCollectionEntry* Entry = nullptr;
			int16 MaterialPick = -1;

			FPCGExEntryAccessResult Result = CollectionMap->ResolveEntry(Partition.Key, MaterialPick);
			if (!Result.IsValid() || !Result.Entry->IsType(PCGExAssetCollection::TypeIds::Mesh))
			{
				continue;
			}

			Entry = static_cast<const FPCGExMeshCollectionEntry*>(Result.Entry);

			FPCGMeshInstanceList& InstanceList = OutMeshInstances[Partition.Value];

			InstanceList.Descriptor = TemplateDescriptor;
			FPCGSoftISMComponentDescriptor& OutDescriptor = InstanceList.Descriptor;

			if (bUseTemplateDescriptor)
			{
				OutDescriptor.ComponentTags.Append(Entry->Tags.Array());
				OutDescriptor.StaticMesh = Entry->StaticMesh;
			}
			else
			{
				// Globals via the host's seam; hosts without a block fall back to the local descriptor.
				Entry->InitPCGSoftISMDescriptor(Result.Host, OutDescriptor);
			}

			if (bForceDisableCollisions)
			{
				OutDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			if (bApplyMaterialOverrides)
			{
				Entry->ApplyMaterials(MaterialPick, OutDescriptor);
			}

			// Swaps go before the mirrored split, so mirrored twins inherit them. Points of one pick may follow different
			// tables (branches merged upstream): one list per descriptor those tables make.
			TArray<FPCGMeshInstanceList> SplitLists;
			if (!PointTables.IsEmpty())
			{
				// The snapshot only describes the mesh it was staged from; one changed without a restage is unstaged.
				const bool bSnapshotValid = !Entry->StaticMesh.IsNull() && Entry->StagedMaterialsSource == Entry->StaticMesh.ToSoftObjectPath();

				const PCGExMeshSelectorStaged::FOutcomeSplit Split = PCGExMeshSelectorStaged::SplitByOutcome(
					InstanceList, PointTables, MaterialSwaps, StagingLayer, *Entry, Result.Host,
					bSnapshotValid ? TConstArrayView<TSoftObjectPtr<UMaterialInterface>>(Entry->StagedMaterials) : TConstArrayView<TSoftObjectPtr<UMaterialInterface>>(),
					SplitLists);

				NumUnknownTables += Split.NumUnknown;
				if (Split.bAnyTable && !bSnapshotValid && !Entry->StaticMesh.IsNull())
				{
					UnstagedHosts.Add(Result.Host);
				}
			}

			auto FinishList = [&](FPCGMeshInstanceList& List)
			{
				if (!bForceEntryReverseCulling)
				{
					PCGExMeshSelectorStaged::SplitMirroredInstances(List, InTransforms, MirroredLists);
				}

				List.Instances.Reserve(List.InstancesIndices.Num());
				for (const int32 i : List.InstancesIndices)
				{
					List.Instances.Emplace(InTransforms[i]);
				}
			};

			FinishList(InstanceList);
			for (FPCGMeshInstanceList& List : SplitLists)
			{
				FinishList(List);
				OutcomeLists.Add(MoveTemp(List));
			}
		}

		OutMeshInstances.Append(MoveTemp(OutcomeLists));
		OutMeshInstances.Append(MoveTemp(MirroredLists));

		if (NumUnknownTables > 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, &Context, FText::Format(
				           FTEXT("{0} point(s) follow a material swap table the Overrides pin doesn't carry, and keep their materials: wire the Map of the Staging : Swap that stamped them."),
				           FText::AsNumber(NumUnknownTables)));
		}

		if (!UnstagedHosts.IsEmpty())
		{
			TArray<FString> HostNames;
			for (const UPCGExAssetCollection* Host : UnstagedHosts)
			{
				HostNames.Add(GetNameSafe(Host));
			}
			HostNames.Sort();

			PCGE_LOG_C(Warning, GraphAndLog, &Context, FText::Format(
				           FTEXT("Material swaps reached override slots only for entries of {0}: their meshes' own materials are not staged yet. Rebuild their staging (the collection editor toolbar can rebuild the whole project)."),
				           FText::FromString(FString::Join(HostNames, TEXT(", ")))));
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
