// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "MeshSelectors/PCGExSkinnedMeshSelectorStaged.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGSkinnedMeshSpawnerContext.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "MeshSelectors/PCGSkinnedMeshSelector.h"

#include "Collections/PCGExSkinnedMeshCollection.h"
#include "Engine/SkinnedAsset.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Tasks/Task.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExSkinnedMeshSelectorStaged)

#define LOCTEXT_NAMESPACE "PCGExSkinnedMeshSelectorStaged"

bool UPCGExSkinnedMeshSelectorStaged::SelectInstances(FPCGSkinnedMeshSpawnerContext& Context, const UPCGSkinnedMeshSpawnerSettings* Settings, const UPCGPointData* InPointData, TArray<FPCGSkinnedMeshInstanceList>& OutMeshInstances, UPCGPointData* OutPointData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExSkinnedMeshSelectorStaged::SelectInstances);

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
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExSkinnedMeshSelectorStaged::SetupOutPointData);

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
		if (!CollectionMap->BuildPartitions<FPCGSkinnedMeshInstanceList>(InPointData, OutMeshInstances))
		{
			PCGE_LOG_C(Error, GraphAndLog, &Context, FTEXT( "Unable to build any partitions"));
			return true;
		}
	}
	else
	{
		// Recover the partitions the previous slice built, rather than re-deriving them from points:
		// BuildPartitions would re-insert every index already placed and orphan the earlier lists.
		CollectionMap->ReindexPartitions<FPCGSkinnedMeshInstanceList>(OutMeshInstances);

		const int32 NumPoints = InPointData->GetNumPoints();

		if (Context.CurrentPointIndex != NumPoints)
		{
			TConstPCGValueRange<int64> MetadataEntries = InPointData->GetConstMetadataEntryValueRange();
			while (Context.CurrentPointIndex < NumPoints)
			{
				CollectionMap->InsertEntry<FPCGSkinnedMeshInstanceList>(InPointData, HashAttribute->GetValueFromItemKey<int64>(MetadataEntries[Context.CurrentPointIndex]), Context.CurrentPointIndex, OutMeshInstances);
				Context.CurrentPointIndex++;
				if (Context.ShouldStop())
				{
					return false;
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExSkinnedMeshSelectorStaged::SelectEntries);

		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		for (const TPair<int64, int32>& Partition : CollectionMap->IndexedPartitions)
		{
			const FPCGExSkinnedMeshCollectionEntry* Entry = nullptr;
			int16 MaterialPick = -1;

			FPCGExEntryAccessResult Result = CollectionMap->ResolveEntry(Partition.Key, MaterialPick);
			if (!Result.IsValid() || !Result.Entry->IsType(PCGExAssetCollection::TypeIds::SkinnedMesh))
			{
				continue;
			}

			Entry = static_cast<const FPCGExSkinnedMeshCollectionEntry*>(Result.Entry);

			FPCGSkinnedMeshInstanceList& InstanceList = OutMeshInstances[Partition.Value];

			InstanceList.Descriptor = TemplateDescriptor;
			FPCGSoftSkinnedMeshComponentDescriptor& OutDescriptor = InstanceList.Descriptor;

			if (bUseTemplateDescriptor)
			{
				OutDescriptor.ComponentTags.Append(Entry->Tags.Array());
				OutDescriptor.SkinnedAsset = Entry->SkinnedAsset;
			}
			else
			{
				// Globals via the host's seam; hosts without a block fall back to the local descriptor.
				Entry->InitPCGSoftSkinnedDescriptor(Result.Host, OutDescriptor);
			}

			if (bForceDisableCollisions)
			{
				OutDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			if (bApplyMaterialOverrides)
			{
				// No-op under UE 5.8 -- see FPCGExSkinnedMeshCollectionEntry::ApplyMaterials.
				Entry->ApplyMaterials(MaterialPick, OutDescriptor);
			}

			// AnimationIndex stays at default 0 for now -- handled as per-instance selector-scoped
			// concern outside the partitioning flow (see TODO in PCGExSkinnedMeshSelectorStaged).
			const TArray<int32>& InstanceIndices = InstanceList.InstancePointIndices;
			InstanceList.Instances.Reserve(InstanceIndices.Num());
			for (const int32 i : InstanceIndices)
			{
				FPCGSkinnedMeshInstance& NewInstance = InstanceList.Instances.Emplace_GetRef();
				NewInstance.Transform = InTransforms[i];
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
