// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCreateSpline.h"


#include "PCGComponent.h"

#include "PCGExVersion.h"
#include "Core/PCGExMT.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateSplineElement"
#define PCGEX_NAMESPACE CreateSpline

#if WITH_EDITOR
void UPCGExCreateSplineSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 70, 11)
	{
		Tangents.ApplyDeprecation(bApplyCustomTangents_DEPRECATED, ArriveTangentAttribute_DEPRECATED, LeaveTangentAttribute_DEPRECATED);
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_INITIALIZE_ELEMENT(CreateSpline)

bool UPCGExCreateSplineSettings::ShouldCache() const
{
	if (Mode == EPCGCreateSplineMode::CreateComponent)
	{
		return false;
	}
	return Super::ShouldCache();
}

PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(CreateSpline)

TArray<FPCGPinProperties> UPCGExCreateSplineSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExTangents::DeclareTangentsInputs(PinProperties, RequiresTangentSources());
	return PinProperties;
}

bool UPCGExCreateSplineSettings::RequiresTangentSources() const
{
	return GetApplyTangents() && PCGExTangents::WantsTangentSources(Tangents);
}

bool UPCGExCreateSplineSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExTangents::SourceTangentSourcesLabel && !RequiresTangentSources())
	{
		return false;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExCreateSplineSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POLYLINES(GetMainOutputPin(), "Spline data.", Required)
	return PinProperties;
}

void FPCGExCreateSplineElement::DisabledPassThroughData(FPCGContext* Context) const
{
	// No passthrough
}

bool FPCGExCreateSplineElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CreateSpline)

	if (Settings->GetApplyTangents())
	{
		if (!Context->Tangents.Init(Context, Settings->Tangents))
		{
			return false;
		}
	}

	return true;
}

bool FPCGExCreateSplineElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCreateSplineElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CreateSpline)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any dataset to generate splines."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	// Output spawns spline components / creates UObjects -- illegal during a package save or GC (and the game-thread
	// marshal below can be pumped mid-SavePackage). Defer the whole output phase until the save/GC finishes.
	PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED

	if (Settings->Mode != EPCGCreateSplineMode::CreateDataOnly)
	{
		PCGExMT::ExecuteOnMainThreadAndWait([&]()
		{
			if (PCGExMT::IsObjectWorkBlocked()) { return; }
			Context->MainBatch->Output();
			Context->ExecuteOnNotifyActors(Settings->PostProcessFunctionNames);
		});
	}
	else
	{
		Context->MainBatch->Output();
		Context->ExecuteOnNotifyActors(Settings->PostProcessFunctionNames);
	}

	return Context->TryComplete();
}

bool FPCGExCreateSplineElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	if (Context)
	{
		const UPCGExCreateSplineSettings* Settings = Context->GetInputSettings<UPCGExCreateSplineSettings>();
		if (Settings && Settings->Mode != EPCGCreateSplineMode::CreateDataOnly)
		{
			return true;
		}
	}
	return FPCGExPathProcessorElement::CanExecuteOnlyOnMainThread(Context);
}

namespace PCGExCreateSpline
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCreateSpline::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn());
		bApplyTangents = Settings->GetApplyTangents();

		if (bApplyTangents)
		{
			TangentsHandler = MakeShared<PCGExTangents::FTangentsHandler>(bClosedLoop);
			if (!TangentsHandler->Init(Context, Context->Tangents, PointDataFacade))
			{
				return false;
			}
		}

		if (Settings->bApplyCustomPointType)
		{
			CustomPointType = PointDataFacade->GetBroadcaster<int32>(Settings->PointTypeAttribute, true);
			if (!CustomPointType)
			{
				PCGEX_LOG_INVALID_ATTR_C(Context, Point Type, Settings->PointTypeAttribute)
				return false;
			}
		}

		PositionOffset = SplineActor->GetTransform().GetLocation();
		SplineData = Context->ManagedObjects->New<UPCGSplineData>();
		SplineData->InitializeFromData(PointDataFacade->GetIn());
		PCGExArrayHelpers::InitArray(SplinePoints, PointDataFacade->GetNum());

		SplineEntryKeys.Init(PCGInvalidEntryKey, SplinePoints.Num());

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::CreateSpline::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		bool bHasAValidEntry = false;

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		TConstPCGValueRange<int64> InMetadataEntries = InPointData->GetConstMetadataEntryValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			FVector OutArrive = FVector::ZeroVector;
			FVector OutLeave = FVector::ZeroVector;

			if (bApplyTangents)
			{
				TangentsHandler->GetPointTangents(Index, OutArrive, OutLeave);
			}

			const FTransform& TR = InTransforms[Index];

			EPCGExSplinePointType PointTypeProxy = Settings->DefaultPointType;
			ESplinePointType::Type PointType = ESplinePointType::Curve;

			if (CustomPointType)
			{
				const int32 Value = CustomPointType->Read(Index);
				if (FMath::IsWithinInclusive(Value, 0, 4))
				{
					PointTypeProxy = static_cast<EPCGExSplinePointType>(static_cast<uint8>(Value));
				}
			}

			switch (PointTypeProxy)
			{
			case EPCGExSplinePointType::Linear:
				PointType = ESplinePointType::Linear;
				break;
			case EPCGExSplinePointType::Curve:
				PointType = ESplinePointType::Curve;
				break;
			case EPCGExSplinePointType::Constant:
				PointType = ESplinePointType::Constant;
				break;
			case EPCGExSplinePointType::CurveClamped:
				PointType = ESplinePointType::CurveClamped;
				break;
			case EPCGExSplinePointType::CurveCustomTangent:
				PointType = ESplinePointType::CurveCustomTangent;
				break;
			}

			SplinePoints[Index] = FSplinePoint(static_cast<float>(Index), TR.GetLocation() - PositionOffset, OutArrive, OutLeave, TR.GetRotation().Rotator(), TR.GetScale3D(), PointType);

			const int64 PointMetadataEntry = InMetadataEntries[Index];
			SplineEntryKeys[Index] = PointMetadataEntry;
			bHasAValidEntry |= (PointMetadataEntry != PCGInvalidEntryKey);
		}

		if (bHasAValidEntry)
		{
			FPlatformAtomics::InterlockedExchange(&HasAValidEntry, true);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		SplineData->Initialize(SplinePoints, bClosedLoop, FTransform(PositionOffset), std::move(SplineEntryKeys));
	}

	void FProcessor::Output()
	{
		TProcessor<FPCGExCreateSplineContext, UPCGExCreateSplineSettings>::Output();

		// Output spline data
		Context->StageOutput(
			SplineData, Settings->GetMainOutputPin(), PCGExData::EStaging::Managed,
			PointDataFacade->Source->Tags->Flatten());

		// Output spline component
		if (Settings->Mode != EPCGCreateSplineMode::CreateDataOnly)
		{
			const bool bIsPreviewMode = ExecutionContext->GetComponent()->IsInPreviewMode();

			const FString ComponentName = TEXT("PCGSplineComponent");
			const EObjectFlags ObjectFlags = (bIsPreviewMode ? RF_Transient : RF_NoFlags);
			USplineComponent* SplineComponent = NewObject<USplineComponent>(SplineActor, MakeUniqueObjectName(SplineActor, USplineComponent::StaticClass(), FName(ComponentName)), ObjectFlags);

			PointDataFacade->Source->Tags->DumpTo(SplineComponent->ComponentTags);

			SplineData->ApplyTo(SplineComponent);

			Context->AttachManagedComponent(SplineActor, SplineComponent, Settings->AttachmentRules.GetRules());
			Context->AddNotifyActor(SplineActor);
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExCreateSplineContext, UPCGExCreateSplineSettings>::Cleanup();
	}

	bool FBatch::PrepareSingle(const TSharedRef<PCGExPointsMT::IProcessor>& InProcessor)
	{
		if (!TargetActor)
		{
			return false;
		}
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}
		PCGEX_TYPED_PROCESSOR_REF
		TypedProcessor->SplineActor = TargetActor;
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
