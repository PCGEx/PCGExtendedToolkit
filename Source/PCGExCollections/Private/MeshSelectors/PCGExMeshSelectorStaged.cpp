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
#include "Helpers/PCGExCollectionPropertyFloatPacker.h"
#include "Helpers/PCGExMaterialSwapTable.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "SceneTypes.h"
#include "Tasks/Task.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExMeshSelectorStaged)

#define LOCTEXT_NAMESPACE "PCGExMeshSelectorStaged"

namespace PCGExMeshSelectorStaged
{
	// An ISM renders all its instances with one cull mode, so mirrored points move to a twin list with Reverse Culling
	// flipped. Nanite flips mirrored instances on its own and ignores the flag (SUPPORT_REVERSE_CULLING_IN_NANITE is 0).
	void SplitMirroredInstances(FPCGMeshInstanceList& InstanceList, const TConstPCGValueRange<FTransform>& InTransforms, TArray<FPCGMeshInstanceList>& OutMirroredLists)
	{
		TArray<int32>& Indices = InstanceList.InstancesIndices;
		TArray<int32> MirroredIndices;

		int32 WriteIndex = 0;
		for (int32 ReadIndex = 0; ReadIndex < Indices.Num(); ReadIndex++)
		{
			const int32 PointIndex = Indices[ReadIndex];
			if (InTransforms[PointIndex].GetDeterminant() < 0)
			{
				MirroredIndices.Add(PointIndex);
			}
			else
			{
				Indices[WriteIndex++] = PointIndex;
			}
		}

		if (MirroredIndices.IsEmpty())
		{
			return;
		}

		if (WriteIndex == 0)
		{
			// Every point is mirrored, and none was compacted away: flip this list instead of leaving an empty one.
			InstanceList.Descriptor.bReverseCulling = !InstanceList.Descriptor.bReverseCulling;
			return;
		}

		Indices.SetNum(WriteIndex, EAllowShrinking::No);

		FPCGMeshInstanceList& Mirrored = OutMirroredLists.Add_GetRef(InstanceList);
		Mirrored.Descriptor.bReverseCulling = !InstanceList.Descriptor.bReverseCulling;
		Mirrored.InstancesIndices = MoveTemp(MirroredIndices);
		Mirrored.Instances.Reserve(Mirrored.InstancesIndices.Num());
		for (const int32 PointIndex : Mirrored.InstancesIndices)
		{
			Mirrored.Instances.Emplace(InTransforms[PointIndex]);
		}
	}

	// Each point's material swap table on InLayer (0 = none); left empty when the points carry no stamp.
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

	// Moves InstanceList's points that follow another table than its first point into OutSplits, one list per table, each a
	// copy of InstanceList as it stands. Returns the first point's table.
	uint32 SplitByTable(FPCGMeshInstanceList& InstanceList, const TArray<uint32>& PointTables, TArray<TPair<uint32, FPCGMeshInstanceList>>& OutSplits)
	{
		TArray<int32>& Indices = InstanceList.InstancesIndices;
		if (Indices.IsEmpty())
		{
			return 0;
		}

		const uint32 KeptTable = PointTables[Indices[0]];

		int32 WriteIndex = 0;
		for (int32 ReadIndex = 0; ReadIndex < Indices.Num(); ReadIndex++)
		{
			const int32 PointIndex = Indices[ReadIndex];
			const uint32 Table = PointTables[PointIndex];
			if (Table == KeptTable)
			{
				Indices[WriteIndex++] = PointIndex;
				continue;
			}

			TPair<uint32, FPCGMeshInstanceList>* Split = OutSplits.FindByPredicate([Table](const TPair<uint32, FPCGMeshInstanceList>& Candidate) { return Candidate.Key == Table; });
			if (!Split)
			{
				FPCGMeshInstanceList Copy = InstanceList;
				Copy.InstancesIndices.Reset();
				Copy.Instances.Reset();
				Split = &OutSplits.Emplace_GetRef(Table, MoveTemp(Copy));
			}
			Split->Value.InstancesIndices.Add(PointIndex);
		}

		Indices.SetNum(WriteIndex, EAllowShrinking::No);
		return KeptTable;
	}
}

#if WITH_EDITOR
void UPCGExMeshSelectorStaged::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName PopulateFromSchemaName = GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatLayout, PopulateFromSchema);

	if (PropertyChangedEvent.GetPropertyName() == PopulateFromSchemaName && CustomPrimitiveDataLayout.PopulateFromSchema)
	{
		CustomPrimitiveDataLayout.EDITOR_PopulateFromSchemaAsset(CustomPrimitiveDataLayout.PopulateFromSchema);

		// One-shot: the rows are the data now, and leaving the asset assigned would read as a live
		// reference that re-expands.
		CustomPrimitiveDataLayout.PopulateFromSchema = nullptr;
	}
}
#endif

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
	const FPCGMetadataAttributeBase* HashAttribute = PCGExMetaHelpers::TryGetConstAttribute<int64>(InPointData->Metadata, EntryIdxAttributeName);

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
				CollectionMap->InsertEntry(InPointData, HashAttribute->GetValueFromItemKey<int64>(MetadataEntries[Context.CurrentPointIndex]), Context.CurrentPointIndex, OutMeshInstances);
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

		// One layout for the whole selection: a per-entry one would move a property between entries,
		// and the material reading that slot has no way to notice.
		PCGExCollections::FPCGExCollectionPropertyFloatPacker CustomDataPacker;
		bool bPackCustomData = false;

		if (!CustomPrimitiveDataLayout.IsEmpty())
		{
			TArray<const UPCGExAssetCollection*> Hosts;
			CollectionMap->GetCollectionsInStableOrder(Hosts);

			// Anything wider than the renderer stores also defeats ISM reuse, whose match compares
			// the stored array against the one we hand it.
			bPackCustomData = CustomDataPacker.Initialize(
				&Context, CustomPrimitiveDataLayout, Hosts, FCustomPrimitiveData::NumCustomPrimitiveDataFloats);

#if WITH_EDITOR
			if (bPackCustomData && bDebugCustomPrimitiveData)
			{
				PCGE_LOG_C(Log, LogOnly, &Context, FText::Format(
					           FTEXT("Custom Primitive Data layout ({0} floats): {1}"),
					           CustomDataPacker.GetNumFloats(), FText::FromString(CustomDataPacker.DescribeLayout())));
			}
#endif
		}

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
		TArray<FPCGMeshInstanceList> TableLists;

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

			// Swaps go before the mirrored split, so mirrored twins inherit them.
			auto FinishList = [&](FPCGMeshInstanceList& List, const uint32 InTable)
			{
				const PCGExCollections::FMaterialSwapLibrary::FStages* Swaps = InTable ? MaterialSwaps.Find(StagingLayer, InTable) : nullptr;
				if (InTable && !Swaps)
				{
					NumUnknownTables += List.InstancesIndices.Num();
				}

				if (Swaps)
				{
					// The snapshot only describes the mesh it was staged from; one changed without a restage is unstaged.
					const bool bSnapshotValid = !Entry->StaticMesh.IsNull() && Entry->StagedMaterialsSource == Entry->StaticMesh.ToSoftObjectPath();
					if (!bSnapshotValid && !Entry->StaticMesh.IsNull())
					{
						UnstagedHosts.Add(Result.Host);
					}

					PCGExCollections::FMaterialSwapTable::Apply(
						*Swaps, *Entry, Result.Host,
						bSnapshotValid ? TConstArrayView<TSoftObjectPtr<UMaterialInterface>>(Entry->StagedMaterials) : TConstArrayView<TSoftObjectPtr<UMaterialInterface>>(),
						List.Descriptor);
				}

				if (bPackCustomData)
				{
					CustomDataPacker.PackEntry(Entry, Result.Host, List.CustomPrimitiveData);

#if WITH_EDITOR
					if (bDebugCustomPrimitiveData)
					{
						PCGE_LOG_C(Log, LogOnly, &Context, FText::Format(
							           FTEXT("Custom Primitive Data for entry '{0}': {1}"),
							           FText::FromString(Entry->Staging.Path.ToString()),
							           FText::FromString(PCGExCollections::DescribeFloats(List.CustomPrimitiveData))));
					}
#endif
				}

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

			if (PointTables.IsEmpty())
			{
				FinishList(InstanceList, 0);
				continue;
			}

			// Points of one pick may follow different tables (branches merged upstream): one list per table.
			TArray<TPair<uint32, FPCGMeshInstanceList>> Splits;
			FinishList(InstanceList, PCGExMeshSelectorStaged::SplitByTable(InstanceList, PointTables, Splits));
			for (TPair<uint32, FPCGMeshInstanceList>& Split : Splits)
			{
				FinishList(Split.Value, Split.Key);
				TableLists.Add(MoveTemp(Split.Value));
			}
		}

		OutMeshInstances.Append(MoveTemp(TableLists));
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
