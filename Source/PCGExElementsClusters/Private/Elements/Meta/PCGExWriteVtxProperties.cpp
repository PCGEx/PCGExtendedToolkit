// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/PCGExWriteVtxProperties.h"


#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExData.h"
#include "Elements/Meta/VtxProperties/PCGExVtxPropertyFactoryProvider.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Math/PCGExBestFitPlane.h"

#define LOCTEXT_NAMESPACE "WriteVtxProperties"
#define PCGEX_NAMESPACE WriteVtxProperties

TArray<FPCGPinProperties> UPCGExWriteVtxPropertiesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExVtxProperty::SourcePropertyLabel, "Extra attribute handlers.", Normal, FPCGExDataTypeInfoVtxProperty::AsId())
	return PinProperties;
}

PCGExData::EIOInit UPCGExWriteVtxPropertiesSettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExWriteVtxPropertiesSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGEX_INITIALIZE_ELEMENT(WriteVtxProperties)

bool UPCGExWriteVtxPropertiesSettings::WantsOOB() const
{
	return bWriteVtxNormal || bMutateVtxToOOB;
}

PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(WriteVtxProperties)

bool FPCGExWriteVtxPropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(WriteVtxProperties)

	PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_OUTPUT_VALIDATE_NAME)

	PCGExFactories::GetInputFactories(InContext, PCGExVtxProperty::SourcePropertyLabel, Context->ExtraFactories, {FPCGExDataTypeInfoVtxProperty::AsId()}, false);

	return true;
}

bool FPCGExWriteVtxPropertiesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteVtxPropertiesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WriteVtxProperties)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

namespace PCGExWriteVtxProperties
{
	FProcessor::~FProcessor()
	{
		Operations.Empty();
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteVtxProperties::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		for (const UPCGExVtxPropertyFactoryData* Factory : Context->ExtraFactories)
		{
			TSharedPtr<FPCGExVtxPropertyOperation> NewOperation = Factory->CreateOperation(Context);

			if (!NewOperation->PrepareForCluster(Context, Cluster, VtxDataFacade, EdgeDataFacade))
			{
				return false;
			}
			Operations.Add(NewOperation);
			if (NewOperation->WantsBFP())
			{
				bWantsOOB = true;
			}
		}

		switch (Settings->NormalAxis)
		{
		case EPCGExMinimalAxis::None:
		case EPCGExMinimalAxis::X:
			NormalAxis = EAxis::Type::X;
			break;
		case EPCGExMinimalAxis::Y:
			NormalAxis = EAxis::Type::Y;
			break;
		case EPCGExMinimalAxis::Z:
			NormalAxis = EAxis::Type::Z;
			break;
		}

		if (!bWantsOOB)
		{
			bWantsOOB = Settings->WantsOOB();
		}

		StartParallelLoopForNodes();

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		TArray<PCGExClusters::FAdjacencyData> Adjacency;

		TConstPCGValueRange<FTransform> InTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();
		TPCGValueRange<FTransform> OutTransforms;
		TPCGValueRange<FVector> OutBoundsMin;
		TPCGValueRange<FVector> OutBoundsMax;

		if (Settings->bMutateVtxToOOB)
		{
			OutTransforms = VtxDataFacade->GetOut()->GetTransformValueRange(false);
			OutBoundsMin = VtxDataFacade->GetOut()->GetBoundsMinValueRange(false);
			OutBoundsMax = VtxDataFacade->GetOut()->GetBoundsMaxValueRange(false);
		}

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];
			if (VtxEdgeCountWriter)
			{
				VtxEdgeCountWriter->SetValue(Node.PointIndex, Node.Num());
			}

			Adjacency.Reset();
			PCGExClusters::Helpers::GetAdjacencyData(Cluster.Get(), Node, Adjacency);

			const PCGExMath::FBestFitPlane BestFitPlane =
				bWantsOOB
				? Settings->bIncludeVtxInOOB
				? PCGExMath::FBestFitPlane(
					Adjacency.Num(),
					[&](int32 i)
					{
						return InTransforms[Adjacency[i].NodePointIndex].GetLocation();
					}, Cluster->GetPos(Node), Settings->bUseMinBoxFit)
				: PCGExMath::FBestFitPlane(Adjacency.Num(), [&](int32 i)
				{
					return InTransforms[Adjacency[i].NodePointIndex].GetLocation();
				}, Settings->bUseMinBoxFit)
				: PCGExMath::FBestFitPlane();

			const FTransform BFPT = BestFitPlane.GetTransform(Settings->AxisOrder);

			if (VtxNormalWriter)
			{
				VtxNormalWriter->SetValue(Node.PointIndex, BFPT.GetUnitAxis(NormalAxis));
			}

			if (Settings->bMutateVtxToOOB)
			{
				const int32 PtIndex = Node.PointIndex;
				FVector Extents = BestFitPlane.GetExtents(Settings->AxisOrder);
				OutTransforms[PtIndex] = BFPT;
				OutBoundsMin[PtIndex] = Extents * -1;
				OutBoundsMax[PtIndex] = Extents;
			}

			for (const TSharedPtr<FPCGExVtxPropertyOperation>& Op : Operations)
			{
				Op->ProcessNode(Node, Adjacency, BestFitPlane);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExWriteVtxPropertiesContext, UPCGExWriteVtxPropertiesSettings>::Cleanup();
		Operations.Empty();
	}

	//////// BATCH

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteVtxProperties)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;
			PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_OUTPUT_INIT)
		}

		if (Settings->bMutateVtxToOOB)
		{
			VtxDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::BoundsMin);
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}

		PCGEX_TYPED_PROCESSOR

#define PCGEX_FWD_VTX(_NAME, _TYPE, _DEFAULT_VALUE) TypedProcessor->_NAME##Writer = _NAME##Writer;
		PCGEX_FOREACH_FIELD_VTXEXTRAS(PCGEX_FWD_VTX)
#undef PCGEX_ASSIGN_AXIS_GETTER

		return true;
	}

	void FBatch::Write()
	{
		VtxDataFacade->WriteFastest(TaskManager);
		TBatch<FProcessor>::Write();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
