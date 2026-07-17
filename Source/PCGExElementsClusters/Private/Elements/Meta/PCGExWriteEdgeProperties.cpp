// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/PCGExWriteEdgeProperties.h"

#include "PCGExVersion.h"
#include "PCGExHeuristicsCommon.h"
#include "PCGExHeuristicsHandler.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExBlendOpFactoryProvider.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "WriteEdgeProperties"
#define PCGEX_NAMESPACE WriteEdgeProperties

PCGExData::EIOInit UPCGExWriteEdgePropertiesSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGExData::EIOInit UPCGExWriteEdgePropertiesSettings::GetEdgeOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

#if WITH_EDITOR
void UPCGExWriteEdgePropertiesSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		// Rewire Solidification Lerp
		PCGEX_SHORTHAND_RENAME_PIN(SolidificationLerpAttribute, SolidificationLerpConstant, SolidificationLerp)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExWriteEdgePropertiesSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		SolidificationLerp.Update(SolidificationLerpInput_DEPRECATED, SolidificationLerpAttribute_DEPRECATED, SolidificationLerpConstant_DEPRECATED);
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExWriteEdgePropertiesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, bEndpointsBlending ? EPCGPinStatus::Normal : EPCGPinStatus::Advanced);
	if (bWriteHeuristics)
	{
		PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics that will be computed and written.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	}
	return PinProperties;
}

bool UPCGExWriteEdgePropertiesSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceBlendingLabel)
	{
		return BlendingInterface == EPCGExBlendingInterface::Individual && bEndpointsBlending;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(WriteEdgeProperties)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(WriteEdgeProperties)

bool FPCGExWriteEdgePropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

	PCGEX_FOREACH_FIELD_EDGEEXTRAS(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->bEndpointsBlending && Settings->BlendingInterface == EPCGExBlendingInterface::Individual)
	{
		PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);
	}

	return true;
}

bool FPCGExWriteEdgePropertiesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteEdgePropertiesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WriteEdgeProperties)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->SetWantsHeuristics(Settings->bWriteHeuristics, Settings->HeuristicScoreMode);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}


namespace PCGExWriteEdgeProperties
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteEdgeProperties::Process);

		EdgeDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (!DirectionSettings.InitFromParent(ExecutionContext, GetParentBatch<FBatch>()->DirectionSettings, EdgeDataFacade))
		{
			return false;
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = EdgeDataFacade;
			PCGEX_FOREACH_FIELD_EDGEEXTRAS(PCGEX_OUTPUT_INIT)
		}

		bSolidify = Settings->SolidificationAxis != EPCGExMinimalAxis::None;

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		if (bSolidify)
		{
			AllocateFor |= EPCGPointNativeProperties::BoundsMin;
			AllocateFor |= EPCGPointNativeProperties::BoundsMax;
		}
		if (bSolidify || Settings->bWriteEdgePosition)
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}

		EdgeDataFacade->GetOut()->AllocateProperties(AllocateFor);

		if (bSolidify)
		{
#define PCGEX_CREATE_LOCAL_AXIS_SET_CONST(_AXIS) if (Settings->bWriteRadius##_AXIS){\
			SolidificationRad##_AXIS = PCGExDetails::MakeSettingValue(Settings->Radius##_AXIS##Input, Settings->Radius##_AXIS##SourceAttribute, Settings->Radius##_AXIS##Constant);\
			if(!SolidificationRad##_AXIS->Init(Settings->Radius##_AXIS##Source == EPCGExClusterElement::Edge ? EdgeDataFacade : VtxDataFacade, false)){ return false; }\
			SolidificationSlide##_AXIS = Settings->Radius##_AXIS##Slide.GetValueSetting();\
			if(!SolidificationSlide##_AXIS->Init(EdgeDataFacade, false)){ return false; } }
			PCGEX_FOREACH_XYZ(PCGEX_CREATE_LOCAL_AXIS_SET_CONST)
#undef PCGEX_CREATE_LOCAL_AXIS_SET_CONST

			SolidificationLerp = Settings->SolidificationLerp.GetValueSetting();
			if (!SolidificationLerp->Init(EdgeDataFacade, false))
			{
				return false;
			}
		}

		if (Settings->bEndpointsBlending)
		{
			if (Settings->BlendingInterface == EPCGExBlendingInterface::Individual)
			{
				if (!Context->BlendingFactories.IsEmpty())
				{
					BlendOpsManager = MakeShared<PCGExBlending::FBlendOpsManager>(EdgeDataFacade);
					BlendOpsManager->SetSources(VtxDataFacade); // We want operands A & B to be the vtx here

					if (!BlendOpsManager->Init(Context, Context->BlendingFactories))
					{
						return false;
					}
				}

				DataBlender = BlendOpsManager;
			}
			else
			{
				MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();
				MetadataBlender->SetTargetData(EdgeDataFacade);
				MetadataBlender->SetSourceData(VtxDataFacade, PCGExData::EIOSide::In, true);

				if (!MetadataBlender->Init(Context, Settings->BlendingSettings))
				{
					// Fail
					Context->CancelExecution(FString("Error initializing blending"));
					return false;
				}

				DataBlender = MetadataBlender;
			}
		}

		if (!DataBlender)
		{
			DataBlender = MakeShared<PCGExBlending::FDummyBlender>();
		}

		StartParallelLoopForEdges();

		return true;
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;
		EdgeDataFacade->Fetch(Scope);

		TPCGValueRange<FTransform> Transforms = (bSolidify || Settings->bWriteEdgePosition) ? EdgeDataFacade->GetOut()->GetTransformValueRange(false) : TPCGValueRange<FTransform>();
		TPCGValueRange<FVector> BoundsMin = bSolidify ? EdgeDataFacade->GetOut()->GetBoundsMinValueRange(false) : TPCGValueRange<FVector>();
		TPCGValueRange<FVector> BoundsMax = bSolidify ? EdgeDataFacade->GetOut()->GetBoundsMaxValueRange(false) : TPCGValueRange<FVector>();

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];
			const int32 EdgeIndex = Edge.PointIndex;

			DirectionSettings.SortEndpoints(Cluster.Get(), Edge);

			const PCGExClusters::FNode& StartNode = *Cluster->GetEdgeStart(Edge);
			const PCGExClusters::FNode& EndNode = *Cluster->GetEdgeEnd(Edge);

			const FVector A = Cluster->GetPos(StartNode);
			const FVector B = Cluster->GetPos(EndNode);

			const FVector EdgeDirection = (A - B).GetSafeNormal();
			const double EdgeLength = FVector::Distance(A, B);

			PCGEX_OUTPUT_VALUE(EdgeDirection, EdgeIndex, EdgeDirection);
			PCGEX_OUTPUT_VALUE(EdgeLength, EdgeIndex, EdgeLength);

			if (Settings->bWriteHeuristics)
			{
				switch (Settings->HeuristicsMode)
				{
				case EPCGExHeuristicsWriteMode::EndpointsOrder: PCGEX_OUTPUT_VALUE(Heuristics, EdgeIndex, HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode));
					break;
				case EPCGExHeuristicsWriteMode::Smallest: PCGEX_OUTPUT_VALUE(Heuristics, EdgeIndex, FMath::Min( HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode), HeuristicsHandler->GetEdgeScore(EndNode, StartNode, Edge, EndNode, StartNode)));
					break;
				case EPCGExHeuristicsWriteMode::Highest: PCGEX_OUTPUT_VALUE(Heuristics, EdgeIndex, FMath::Max( HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode), HeuristicsHandler->GetEdgeScore(EndNode, StartNode, Edge, EndNode, StartNode)));
					break;
				default: ;
				}
			}

			if (bSolidify)
			{
				FRotator EdgeRot;
				FVector TargetBoundsMin = BoundsMin[EdgeIndex];
				FVector TargetBoundsMax = BoundsMax[EdgeIndex];

				FVector TargetScale = Transforms[EdgeIndex].GetScale3D();

				const FVector InvScale = FVector::One() / TargetScale;

				double BlendWeightStart = FMath::Clamp(SolidificationLerp->Read(EdgeIndex), 0, 1);
				double BlendWeightEnd = 1 - BlendWeightStart;

#define PCGEX_SOLIDIFY_DIMENSION(_AXIS)\
if (Settings->SolidificationAxis == EPCGExMinimalAxis::_AXIS){\
TargetBoundsMin._AXIS = (-EdgeLength * BlendWeightEnd) * InvScale._AXIS;\
TargetBoundsMax._AXIS = (EdgeLength * BlendWeightStart) * InvScale._AXIS;\
}else if(SolidificationRad##_AXIS){\
double Rad = 0;\
if (Settings->Radius##_AXIS##Source == EPCGExClusterElement::Vtx) { Rad = FMath::Lerp(SolidificationRad##_AXIS->Read(Edge.Start), SolidificationRad##_AXIS->Read(Edge.End), BlendWeightStart); }\
else { Rad = SolidificationRad##_AXIS->Read(EdgeIndex); }\
Rad = FMath::Abs(Rad);\
const double Slide = FMath::Clamp(SolidificationSlide##_AXIS->Read(EdgeIndex), 0.0, 1.0);\
TargetBoundsMin._AXIS = (2.0 * (Slide - 1.0) * Rad) * InvScale._AXIS;\
TargetBoundsMax._AXIS = (2.0 * Slide * Rad) * InvScale._AXIS;\
}

				PCGEX_FOREACH_XYZ(PCGEX_SOLIDIFY_DIMENSION)
#undef PCGEX_SOLIDIFY_DIMENSION

				switch (Settings->SolidificationAxis)
				{
				default: case EPCGExMinimalAxis::X:
					EdgeRot = FRotationMatrix::MakeFromX(EdgeDirection).Rotator();
					break;
				case EPCGExMinimalAxis::Y:
					EdgeRot = FRotationMatrix::MakeFromY(EdgeDirection).Rotator();
					break;
				case EPCGExMinimalAxis::Z:
					EdgeRot = FRotationMatrix::MakeFromZ(EdgeDirection).Rotator();
					break;
				}

				Transforms[EdgeIndex] = FTransform(EdgeRot, FMath::Lerp(A, B, Settings->bWriteEdgePosition ? Settings->EdgePositionLerp : BlendWeightEnd), TargetScale);

				BoundsMin[EdgeIndex] = TargetBoundsMin;
				BoundsMax[EdgeIndex] = TargetBoundsMax;

				DataBlender->Blend(Edge.Start, Edge.End, EdgeIndex, BlendWeightEnd);
			}
			else if (Settings->bWriteEdgePosition)
			{
				Transforms[EdgeIndex].SetLocation(FMath::Lerp(A, B, Settings->EdgePositionLerp));
				DataBlender->Blend(Edge.Start, Edge.End, EdgeIndex, Settings->EndpointsWeights);
			}
			else
			{
				DataBlender->Blend(Edge.Start, Edge.End, EdgeIndex, Settings->EndpointsWeights);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		if (BlendOpsManager)
		{
			BlendOpsManager->Cleanup(Context);
		}
		EdgeDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExWriteEdgePropertiesContext, UPCGExWriteEdgePropertiesSettings>::Cleanup();
		BlendOpsManager.Reset();
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

		Settings->BlendingSettings.RegisterBuffersDependencies(Context, FacadePreloader);
		PCGExBlending::RegisterBuffersDependencies_SourceA(Context, FacadePreloader, Context->BlendingFactories);
		DirectionSettings.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

		DirectionSettings = Settings->DirectionSettings;

		if (!DirectionSettings.Init(ExecutionContext, VtxDataFacade, Context->GetEdgeSortingRules()))
		{
			bIsBatchValid = false;
			return;
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
