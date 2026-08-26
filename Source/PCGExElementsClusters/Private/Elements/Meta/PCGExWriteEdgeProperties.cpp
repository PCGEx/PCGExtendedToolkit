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

	PCGEX_IF_VERSION_LOWER(1, 76, 13)
	{
		// Rewire the per-component radius pins onto the secondary/tertiary slot writing the same
		// local component — the mapping depends on the serialized solidification axis (see PCGExApplyDeprecation).
		auto RewireRadius = [&](const TCHAR* Axis, const TCHAR* Target)
		{
			const FName TargetName(Target);
			const FName EnabledSuffix[] = {TargetName, FName(TEXT("bEnabled"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("bWriteRadius%s"), Axis)), EnabledSuffix);
			const FName SourceSuffix[] = {TargetName, FName(TEXT("Source"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("Radius%sSource"), Axis)), SourceSuffix);
			const FName RadAttrSuffix[] = {TargetName, FName(TEXT("Radius")), FName(TEXT("Attribute"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("Radius%sSourceAttribute"), Axis)), RadAttrSuffix, FName(*FString::Printf(TEXT("Radius %s (Attr)"), Axis)));
			const FName RadConstSuffix[] = {TargetName, FName(TEXT("Radius")), FName(TEXT("Constant"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("Radius%sConstant"), Axis)), RadConstSuffix, FName(*FString::Printf(TEXT("Radius %s"), Axis)));
			// The old slide shorthands sat among clashing Attribute/Constant leaves, so their pins carry full-path labels.
			const FName SlideAttrSuffix[] = {TargetName, FName(TEXT("Slide")), FName(TEXT("Attribute"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("Radius%sSlide/Attribute"), Axis)), SlideAttrSuffix);
			const FName SlideConstSuffix[] = {TargetName, FName(TEXT("Slide")), FName(TEXT("Constant"))};
			PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(*FString::Printf(TEXT("Radius%sSlide/Constant"), Axis)), SlideConstSuffix);
		};

		switch (SolidificationAxis)
		{
		case EPCGExMinimalAxis::Y:
			RewireRadius(TEXT("Z"), TEXT("SecondaryAxis"));
			RewireRadius(TEXT("X"), TEXT("TertiaryAxis"));
			break;
		case EPCGExMinimalAxis::Z:
			RewireRadius(TEXT("X"), TEXT("SecondaryAxis"));
			RewireRadius(TEXT("Y"), TEXT("TertiaryAxis"));
			break;
		default:
			RewireRadius(TEXT("Y"), TEXT("SecondaryAxis"));
			RewireRadius(TEXT("Z"), TEXT("TertiaryAxis"));
			break;
		}

		const FName PositionLerpSuffix[] = {FName(TEXT("EdgePositionLerpValue")), FName(TEXT("Constant"))};
		PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(TEXT("EdgePositionLerp")), PositionLerpSuffix, FName(TEXT("Edge Position Lerp")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExWriteEdgePropertiesSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		SolidificationLerp.Update(SolidificationLerpInput_DEPRECATED, SolidificationLerpAttribute_DEPRECATED, SolidificationLerpConstant_DEPRECATED);
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 13)
	{
#define PCGEX_RADIUS_COPY_TO(_SOURCE, _TARGET)\
		_TARGET.bEnabled = bWriteRadius##_SOURCE##_DEPRECATED;\
		_TARGET.Source = Radius##_SOURCE##Source_DEPRECATED;\
		_TARGET.Radius.Update(Radius##_SOURCE##Input_DEPRECATED, Radius##_SOURCE##SourceAttribute_DEPRECATED, Radius##_SOURCE##Constant_DEPRECATED);\
		_TARGET.Slide = Radius##_SOURCE##Slide_DEPRECATED;

		switch (SolidificationAxis)
		{
		case EPCGExMinimalAxis::Y:
			PCGEX_RADIUS_COPY_TO(Z, SecondaryAxis)
			PCGEX_RADIUS_COPY_TO(X, TertiaryAxis)
			break;
		case EPCGExMinimalAxis::Z:
			PCGEX_RADIUS_COPY_TO(X, SecondaryAxis)
			PCGEX_RADIUS_COPY_TO(Y, TertiaryAxis)
			break;
		default: // X, or None (radii unused then — mapped as X purely to preserve values)
			PCGEX_RADIUS_COPY_TO(Y, SecondaryAxis)
			PCGEX_RADIUS_COPY_TO(Z, TertiaryAxis)
			break;
		}
#undef PCGEX_RADIUS_COPY_TO

		EdgePositionLerpValue.Constant = EdgePositionLerp_DEPRECATED;
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
			switch (Settings->SolidificationAxis)
			{
			default:
			case EPCGExMinimalAxis::X:
				PrimaryComponent = 0;
				SecondaryComponent = 1;
				TertiaryComponent = 2;
				break;
			case EPCGExMinimalAxis::Y:
				PrimaryComponent = 1;
				SecondaryComponent = 2;
				TertiaryComponent = 0;
				break;
			case EPCGExMinimalAxis::Z:
				PrimaryComponent = 2;
				SecondaryComponent = 0;
				TertiaryComponent = 1;
				break;
			}

			auto InitRadius = [&](const FPCGExEdgeSolidificationRadiusDetails& InDetails, TSharedPtr<PCGExDetails::TSettingValue<double>>& OutRadius, TSharedPtr<PCGExDetails::TSettingValue<double>>& OutSlide, bool& bOutFromVtx) -> bool
			{
				if (!InDetails.bEnabled)
				{
					return true;
				}

				bOutFromVtx = InDetails.Source == EPCGExClusterElement::Vtx;

				OutRadius = InDetails.Radius.GetValueSetting();
				if (!OutRadius->Init(bOutFromVtx ? VtxDataFacade : EdgeDataFacade, false))
				{
					return false;
				}

				OutSlide = InDetails.Slide.GetValueSetting();
				return OutSlide->Init(EdgeDataFacade, false);
			};

			if (!InitRadius(Settings->SecondaryAxis, SecondaryRadius, SecondarySlide, bSecondaryFromVtx))
			{
				return false;
			}
			if (!InitRadius(Settings->TertiaryAxis, TertiaryRadius, TertiarySlide, bTertiaryFromVtx))
			{
				return false;
			}

			SolidificationLerp = Settings->SolidificationLerp.GetValueSetting();
			if (!SolidificationLerp->Init(EdgeDataFacade, false))
			{
				return false;
			}
		}

		if (Settings->bWriteEdgePosition)
		{
			EdgePositionLerp = Settings->EdgePositionLerpValue.GetValueSetting();
			if (!EdgePositionLerp->Init(EdgeDataFacade, false))
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

				// Where the pivot lands along the edge is what splits the primary bounds around it, so
				// both read the same alpha. Lerp(A, B, a) sits a*L from A along -EdgeDirection, which
				// leaves a*L of the edge on the +EdgeDirection side and (1-a)*L on the other.
				const double PositionAlpha = EdgePositionLerp ? FMath::Clamp(EdgePositionLerp->Read(EdgeIndex), 0.0, 1.0) : BlendWeightEnd;

				TargetBoundsMin[PrimaryComponent] = (-EdgeLength * (1 - PositionAlpha)) * InvScale[PrimaryComponent];
				TargetBoundsMax[PrimaryComponent] = (EdgeLength * PositionAlpha) * InvScale[PrimaryComponent];

				auto SolidifyRadius = [&](const TSharedPtr<PCGExDetails::TSettingValue<double>>& InRadius, const TSharedPtr<PCGExDetails::TSettingValue<double>>& InSlide, const int32 Component, const bool bFromVtx)
				{
					if (!InRadius)
					{
						return;
					}

					const double Rad = FMath::Abs(bFromVtx ? FMath::Lerp(InRadius->Read(Edge.Start), InRadius->Read(Edge.End), BlendWeightStart) : InRadius->Read(EdgeIndex));
					const double Slide = FMath::Clamp(InSlide->Read(EdgeIndex), 0.0, 1.0);
					TargetBoundsMin[Component] = (2.0 * (Slide - 1.0) * Rad) * InvScale[Component];
					TargetBoundsMax[Component] = (2.0 * Slide * Rad) * InvScale[Component];
				};

				SolidifyRadius(SecondaryRadius, SecondarySlide, SecondaryComponent, bSecondaryFromVtx);
				SolidifyRadius(TertiaryRadius, TertiarySlide, TertiaryComponent, bTertiaryFromVtx);

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

				Transforms[EdgeIndex] = FTransform(EdgeRot, FMath::Lerp(A, B, PositionAlpha), TargetScale);

				BoundsMin[EdgeIndex] = TargetBoundsMin;
				BoundsMax[EdgeIndex] = TargetBoundsMax;

				DataBlender->Blend(Edge.Start, Edge.End, EdgeIndex, BlendWeightEnd);
			}
			else if (Settings->bWriteEdgePosition)
			{
				Transforms[EdgeIndex].SetLocation(FMath::Lerp(A, B, FMath::Clamp(EdgePositionLerp->Read(EdgeIndex), 0.0, 1.0)));
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
