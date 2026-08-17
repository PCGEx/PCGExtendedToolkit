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
#include "Helpers/PCGExMetaHelpers.h"
#include "SceneTypes.h"
#include "Tasks/Task.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExMeshSelectorStaged)

#define LOCTEXT_NAMESPACE "PCGExMeshSelectorStaged"

namespace PCGExMeshSelectorStaged
{
	// Returns variation based on mesh, material overrides and reverse culling
	FPCGMeshInstanceList& GetInstanceList(TArray<FPCGMeshInstanceList>& InstanceLists, const FPCGSoftISMComponentDescriptor& TemplateDescriptor, TSoftObjectPtr<UStaticMesh> Mesh, const TArray<TSoftObjectPtr<UMaterialInterface>>& MaterialOverrides, bool bReverseCulling, const UPCGBasePointData* InPointData, const int AttributePartitionIndex = INDEX_NONE)
	{
		for (FPCGMeshInstanceList& InstanceList : InstanceLists)
		{
			if (InstanceList.Descriptor.StaticMesh == Mesh && InstanceList.Descriptor.bReverseCulling == bReverseCulling && InstanceList.Descriptor.OverrideMaterials == MaterialOverrides && InstanceList.AttributePartitionIndex == AttributePartitionIndex)
			{
				return InstanceList;
			}
		}

		FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef(TemplateDescriptor);
		NewInstanceList.Descriptor.StaticMesh = Mesh;
		NewInstanceList.Descriptor.OverrideMaterials = MaterialOverrides;
		NewInstanceList.Descriptor.bReverseCulling = bReverseCulling;
		NewInstanceList.AttributePartitionIndex = AttributePartitionIndex;
		NewInstanceList.PointData = InPointData;

		return NewInstanceList;
	}

#if WITH_EDITOR
	FString DescribeFloats(TConstArrayView<float> InFloats)
	{
		return FString::JoinBy(InFloats, TEXT(", "), [](const float Value) { return FString::SanitizeFloat(Value); });
	}
#endif
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

			if (bPackCustomData)
			{
				CustomDataPacker.PackEntry(Entry, Result.Host, InstanceList.CustomPrimitiveData);

#if WITH_EDITOR
				if (bDebugCustomPrimitiveData)
				{
					PCGE_LOG_C(Log, LogOnly, &Context, FText::Format(
						           FTEXT("Custom Primitive Data for entry '{0}': {1}"),
						           FText::FromString(Entry->Staging.Path.ToString()),
						           FText::FromString(PCGExMeshSelectorStaged::DescribeFloats(InstanceList.CustomPrimitiveData))));
				}
#endif
			}

			const TArray<int32>& InstanceIndices = InstanceList.InstancesIndices;
			InstanceList.Instances.Reserve(InstanceIndices.Num());
			for (const int32 i : InstanceIndices)
			{
				InstanceList.Instances.Emplace(InTransforms[i]);
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
