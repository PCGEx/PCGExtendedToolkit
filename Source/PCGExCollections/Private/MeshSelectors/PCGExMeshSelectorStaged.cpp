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
#include "Helpers/PCGExMetaHelpers.h"
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

		// Appended only after the loop: Partition.Value indexes OutMeshInstances, which must not grow mid-iteration.
		TArray<FPCGMeshInstanceList> MirroredLists;

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

			if (!bForceEntryReverseCulling)
			{
				PCGExMeshSelectorStaged::SplitMirroredInstances(InstanceList, InTransforms, MirroredLists);
			}

			const TArray<int32>& InstanceIndices = InstanceList.InstancesIndices;
			InstanceList.Instances.Reserve(InstanceIndices.Num());
			for (const int32 i : InstanceIndices)
			{
				InstanceList.Instances.Emplace(InTransforms[i]);
			}
		}

		OutMeshInstances.Append(MoveTemp(MirroredLists));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
