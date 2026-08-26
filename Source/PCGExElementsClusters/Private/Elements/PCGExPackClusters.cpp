// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPackClusters.h"


#include "Clusters/PCGExCluster.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataTags.h"
#include "Utils/PCGExPointIOMerger.h"

#define LOCTEXT_NAMESPACE "PCGExPackClusters"
#define PCGEX_NAMESPACE PackClusters

PCGExData::EIOInit UPCGExPackClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExPackClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

TArray<FPCGPinProperties> UPCGExPackClustersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputPackedClustersLabel, "Individually packed clusters", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PackClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(PackClusters)

bool FPCGExPackClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PackClusters)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	Context->PackedClusters = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->PackedClusters->OutputPin = PCGExClusters::Labels::OutputPackedClustersLabel;

	return true;
}

bool FPCGExPackClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPackClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PackClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([&](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
		                                      {
			                                      return true;
		                                      }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		                                      {
		                                      }))
		{
			Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)
	Context->PackedClusters->StageOutputs();
	return Context->TryComplete();
}

namespace PCGExPackClusters
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!TProcessor::Process(InTaskManager))
		{
			return false;
		}

		// TODO : Refactor because we're actually partitioning indices, which is bad as we don't preserve the original data layout

		EPCGPointNativeProperties AllocateProperties = VtxDataFacade->GetAllocations() | EdgeDataFacade->GetAllocations();

		VtxPointSelection.SetNumUninitialized(NumNodes);
		for (int i = 0; i < NumNodes; i++)
		{
			VtxPointSelection[i] = Cluster->GetNodePointIndex(i);
		}

		VtxStartIndex = EdgeDataFacade->GetNum();
		NumVtx = VtxPointSelection.Num();

		if (VtxStartIndex <= 0)
		{
			return false;
		}
		if (NumVtx <= 0)
		{
			return false;
		}

		PackedIO = Context->PackedClusters->Emplace_GetRef(EdgeDataFacade->Source, PCGExData::EIOInit::Duplicate);
		PackedIOFacade = MakeShared<PCGExData::FFacade>(PackedIO.ToSharedRef());

		PackedIO->Tags->Set<int64>(PCGExClusters::Labels::TagStr_PCGExCluster, EdgeDataFacade->GetIn()->GetUniqueID());
		WriteMark(PackedIO.ToSharedRef(), PCGExClusters::Labels::Tag_PackedClusterEdgeCount, NumEdges);

		// Copy vtx points after edge points
		const UPCGBasePointData* VtxPoints = VtxDataFacade->GetIn();
		UPCGBasePointData* PackedPoints = PackedIO->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(PackedPoints, VtxStartIndex + NumVtx, AllocateProperties);

		TArray<int32> WriteIndices;
		PCGExArrayHelpers::ArrayOfIndices(WriteIndices, NumVtx, VtxStartIndex);
		VtxPoints->CopyPropertiesTo(PackedPoints, VtxPointSelection, WriteIndices, AllocateProperties & ~EPCGPointNativeProperties::MetadataEntry);

		// The following may be redundant
		TPCGValueRange<int64> MetadataEntries = PackedPoints->GetMetadataEntryValueRange(false);
		for (int32 Index : WriteIndices)
		{
			MetadataEntries[Index] = PCGInvalidEntryKey;
		}

		//
		VtxAttributes = PCGExData::FAttributesInfos::Get(VtxDataFacade->GetIn()->Metadata);
		if (VtxAttributes->Identities.IsEmpty())
		{
			return true;
		}

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, CopyVtxAttributes)

		CopyVtxAttributes->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			const PCGExData::FAttributeIdentity& Identity = This->VtxAttributes->Identities[Index];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TArray<T> RawValues;

					TSharedPtr<PCGExData::TBuffer<T>> InValues = This->VtxDataFacade->GetReadable<T>(Identity.GetIdentifier());
					TSharedPtr<PCGExData::TBuffer<T>> OutValues = This->PackedIOFacade->GetWritable<T>(InValues->InAttribute, PCGExData::EBufferInit::New);

					const TArray<int32>& VtxSelection = This->VtxPointSelection;
					for (int i = 0; i < VtxSelection.Num(); i++)
					{
						OutValues->SetValue(This->VtxStartIndex + i, InValues->Read(VtxSelection[i]));
					}
				},
				[&]()
				{
					// Property-backed: scatter selected vtx values into packed output via FProperty.
					if (!Identity.Attribute)
					{
						return;
					}

					TSharedPtr<PCGExData::IBuffer> InBuf = This->VtxDataFacade->GetReadable(Identity, PCGExData::EIOSide::In, false);
					TSharedPtr<PCGExData::IBuffer> OutBuf = This->PackedIOFacade->GetWritable(Identity.GetType(), Identity.Attribute, PCGExData::EBufferInit::New);
					if (!InBuf || !OutBuf)
					{
						return;
					}

					PCGExTypes::FScopedTypedValue Scratch = OutBuf->MakeScopedValue();
					const TArray<int32>& VtxSelection = This->VtxPointSelection;
					for (int i = 0; i < VtxSelection.Num(); i++)
					{
						InBuf->ReadVoid(VtxSelection[i], Scratch);
						OutBuf->SetVoid(This->VtxStartIndex + i, Scratch);
					}
				});
		};

		CopyVtxAttributes->StartIterations(VtxAttributes->Identities.Num(), 1, false);

		Context->CarryOverDetails.Prune(PackedIO->Tags.Get());

		return true;
	}

	void FProcessor::CompleteWork()
	{
		TProcessor<FPCGExPackClustersContext, UPCGExPackClustersSettings>::CompleteWork();
		PackedIOFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
