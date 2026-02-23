// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleNearestPoint.h"

#include "Blenders/PCGExUnionBlender.h"
#include "Blenders/PCGExUnionOpsManager.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExOpStats.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Sampling/PCGExSamplingUnionData.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"


#define LOCTEXT_NAMESPACE "PCGExSampleNearestPointElement"
#define PCGEX_NAMESPACE SampleNearestPoint

PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExSampleNearestPointSettings, LookAtUp, FVector, LookAtUpSelection != EPCGExSampleSource::Constant, LookAtUpSource, LookAtUpConstant)

UPCGExSampleNearestPointSettings::UPCGExSampleNearestPointSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (LookAtUpSource.GetName() == FName("@Last")) { LookAtUpSource.Update(TEXT("$Transform.Up")); }
	if (!WeightOverDistance) { WeightOverDistance = PCGExCurves::WeightDistributionLinear; }
}

#if WITH_EDITOR
void UPCGExSampleNearestPointSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_UPDATE_TO_DATA_VERSION(1, 74, 3)
	{
		// Rewire Range Min
		PCGEX_SHORTHAND_RENAME_PIN(RangeMinAttribute, RangeMin, MinRange)
		MinRange.Update(RangeMinInput_DEPRECATED, RangeMinAttribute_DEPRECATED, RangeMin_DEPRECATED);

		// Rewire Range Max
		PCGEX_SHORTHAND_RENAME_PIN(RangeMaxAttribute, RangeMax, MaxRange)
		MaxRange.Update(RangeMaxInput_DEPRECATED, RangeMaxAttribute_DEPRECATED, RangeMax_DEPRECATED);
	}

	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

TArray<FPCGPinProperties> UPCGExSampleNearestPointSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, "The point data set to check against.", Required)

	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal, BlendingInterface);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);

	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceUseValueIfFilters, "Filter which points values will be processed.", Advanced)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSampleNearestPointSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExSampleNearestPointSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules) { return SampleMethod == EPCGExSampleMethod::BestCandidate; }
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceBlendingLabel) { return BlendingInterface == EPCGExBlendingInterface::Individual && InPin->EdgeCount() > 0; }
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(SampleNearestPoint)

PCGExData::EIOInit UPCGExSampleNearestPointSettings::GetMainDataInitializationPolicy() const { return PCGExData::EIOInit::Duplicate; }

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleNearestPoint)

bool FPCGExSampleNearestPointElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestPoint)

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	PCGEX_FOREACH_FIELD_NEARESTPOINT(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->BlendingInterface == EPCGExBlendingInterface::Individual)
	{
		PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {PCGExFactories::EType::Blending}, false);
	}

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->TargetsHandler->Init(Context, PCGExCommon::Labels::SourceTargetsLabel);

	Context->NumMaxTargets = Context->TargetsHandler->GetMaxNumTargets();
	if (!Context->NumMaxTargets)
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("No targets (empty datasets)"))
		return false;
	}

	Context->TargetsHandler->SetDistances(Settings->DistanceDetails);

	if (Settings->SampleMethod == EPCGExSampleMethod::BestCandidate)
	{
		Context->Sorter = MakeShared<PCGExSorting::FSorter>(PCGExSorting::GetSortingRules(Context, PCGExSorting::Labels::SourceSortingRules));
		Context->Sorter->SortDirection = Settings->SortDirection;
	}

	Context->TargetsHandler->ForEachPreloader([&](PCGExData::FFacadePreloader& Preloader)
	{
		if (Settings->WeightMode != EPCGExSampleWeightMode::Distance) { Preloader.Register<double>(Context, Settings->WeightAttribute); }
		PCGExBlending::RegisterBuffersDependencies_SourceA(Context, Preloader, Context->BlendingFactories);
	});

	Context->WeightCurve = Settings->WeightCurveLookup.MakeLookup(
		Settings->bUseLocalCurve, Settings->LocalWeightOverDistance, Settings->WeightOverDistance,
		[](FRichCurve& CurveData)
		{
			CurveData.AddKey(0, 0);
			CurveData.AddKey(1, 1);
		});

	return true;
}

bool FPCGExSampleNearestPointElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleNearestPointElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestPoint)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetOrCreateHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

			const bool bError = Context->TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32 TargetIndex, bool& bBreak)
			{
				// Prep weights
				if (Settings->WeightMode != EPCGExSampleWeightMode::Distance)
				{
					TSharedPtr<PCGExData::TBuffer<double>> Weight = Target->GetBroadcaster<double>(Settings->WeightAttribute);
					if (!Weight)
					{
						PCGEX_LOG_INVALID_SELECTOR_C(Context, Target Weight, Settings->WeightAttribute)
						bBreak = true;
						return;
					}

					Context->TargetWeights.Add(Weight);
				}

				// Prep look up getters
				if (Settings->LookAtUpSelection == EPCGExSampleSource::Target)
				{
					// TODO : Preload if relevant
					TSharedPtr<PCGExDetails::TSettingValue<FVector>> LookAtUpGetter = Settings->GetValueSettingLookAtUp();
					if (!LookAtUpGetter->Init(Target, false))
					{
						bBreak = true;
						return;
					}

					Context->TargetLookAtUpGetters.Add(LookAtUpGetter);
				}
			});

			if (bError)
			{
				Context->CancelExecution();
				return;
			}

			Context->TargetsHandler->SetMatchingDetails(Context, &Settings->DataMatching);

			if (Context->Sorter && !Context->Sorter->Init(Context, Context->TargetsHandler->GetFacades()))
			{
				Context->CancelExecution(TEXT("Invalid sort rules"));
				return;
			}

			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				Context->CancelExecution(TEXT("Could not find any points to sample."));
			}
		};

		Context->TargetsHandler->StartLoading(Context->GetTaskManager());
		if (Context->IsWaitingForTasks()) { return false; }
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleNearestPoint
{
	FProcessor::~FProcessor()
	{
	}

	void FProcessor::SamplingFailed(const int32 Index)
	{
		SamplingMask[Index] = false;

		const TConstPCGValueRange<FTransform> Transforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		const double FailSafeDist = RangeMaxGetter->Read(Index);
		PCGEX_OUTPUT_VALUE(Success, Index, false)
		PCGEX_OUTPUT_VALUE(Transform, Index, Transforms[Index])
		PCGEX_OUTPUT_VALUE(LookAtTransform, Index, Transforms[Index])
		PCGEX_OUTPUT_VALUE(Distance, Index, Settings->bOutputNormalizedDistance ? FailSafeDist : FailSafeDist * Settings->DistanceScale)
		PCGEX_OUTPUT_VALUE(SignedDistance, Index, FailSafeDist * Settings->SignedDistanceScale)
		PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, FVector(FailSafeDist))
		PCGEX_OUTPUT_VALUE(NumSamples, Index, 0)
		PCGEX_OUTPUT_VALUE(SampledIndex, Index, -1)
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleNearestPoint::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		if (Settings->bIgnoreSelf) { IgnoreList.Add(PointDataFacade->GetIn()); }

		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true); !Context->TargetsHandler->PopulateIgnoreList(PointDataFacade->Source, MatchingScope, IgnoreList))
		{
			(void)Context->TargetsHandler->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		if (Context->ApplySampling.WantsApply()) { AllocateFor |= EPCGPointNativeProperties::Transform; }
		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		SamplingMask.SetNumUninitialized(PointDataFacade->GetNum());

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_NEARESTPOINT(PCGEX_OUTPUT_INIT)
		}

		if (!Context->BlendingFactories.IsEmpty())
		{
			UnionBlendOpsManager = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, Context->TargetsHandler->GetDistances());
			if (!UnionBlendOpsManager->Init(Context, PointDataFacade, Context->TargetsHandler->GetFacades())) { return false; }
			DataBlender = UnionBlendOpsManager;
		}
		else if (Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic)
		{
			TSet<FName> MissingAttributes;
			PCGExBlending::AssembleBlendingDetails(Settings->PointPropertiesBlendingSettings, Settings->TargetAttributes, Context->TargetsHandler->GetFacades(), BlendingDetails, MissingAttributes);

			UnionBlender = MakeShared<PCGExBlending::FUnionBlender>(&BlendingDetails, nullptr, Context->TargetsHandler->GetDistances());
			UnionBlender->AddSources(Context->TargetsHandler->GetFacades());
			if (!UnionBlender->Init(Context, PointDataFacade)) { return false; }
			DataBlender = UnionBlender;
		}

		if (!DataBlender)
		{
			TSharedPtr<PCGExBlending::FDummyUnionBlender> DummyUnionBlender = MakeShared<PCGExBlending::FDummyUnionBlender>();
			DummyUnionBlender->Init(PointDataFacade, Context->TargetsHandler->GetFacades());
			DataBlender = DummyUnionBlender;
		}

		if (Settings->bWriteLookAtTransform)
		{
			if (Settings->LookAtUpSelection != EPCGExSampleSource::Target)
			{
				LookAtUpGetter = Settings->GetValueSettingLookAtUp();
				if (!LookAtUpGetter->Init(PointDataFacade)) { return false; }
			}
		}
		else
		{
			LookAtUpGetter = PCGExDetails::MakeSettingValue(Settings->LookAtUpConstant);
		}

		RangeMinGetter = Settings->MinRange.GetValueSetting();
		if (!RangeMinGetter->Init(PointDataFacade)) { return false; }

		RangeMaxGetter = Settings->MaxRange.GetValueSetting();
		if (!RangeMaxGetter->Init(PointDataFacade)) { return false; }

		bSingleSample = Settings->SampleMethod != EPCGExSampleMethod::WithinRange;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExSampleNearestPointContext, UPCGExSampleNearestPointSettings>::PrepareLoopScopesForPoints(Loops);
		MaxSampledDistanceScoped = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleNearestPoint::ProcessPoints);

		const bool bWeightUseAttr = Settings->WeightMode == EPCGExSampleWeightMode::Attribute;
		const bool bWeightUseAttrMult = Settings->WeightMode == EPCGExSampleWeightMode::AttributeMult;
		const bool bSampleClosest = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget;
		const bool bSampleFarthest = Settings->SampleMethod == EPCGExSampleMethod::FarthestTarget;
		const bool bSampleBest = Settings->SampleMethod == EPCGExSampleMethod::BestCandidate;

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		bool bLocalAnySuccess = false;

		TArray<PCGExData::FWeightedPoint> OutWeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		DataBlender->InitTrackers(Trackers);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();

		const bool bProcessFilteredOutAsFails = Settings->bProcessFilteredOutAsFails;
		const double DefaultDet = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget ? MAX_dbl : MIN_dbl;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				if (bProcessFilteredOutAsFails) { SamplingFailed(Index); }
				continue;
			}

			double RangeMin = FMath::Square(RangeMinGetter->Read(Index));
			double RangeMax = FMath::Square(RangeMaxGetter->Read(Index));

			if (RangeMin > RangeMax) { std::swap(RangeMin, RangeMax); }

			Union->Reset();
			Union->Reserve(Context->TargetsHandler->Num(), RangeMax || bSingleSample ? 8 : Context->NumMaxTargets);

			const PCGExData::FConstPoint Point = PointDataFacade->GetInPoint(Index);
			const FVector Origin = InTransforms[Index].GetLocation();

			PCGExData::FElement SinglePick(-1, -1);
			double Det = DefaultDet;

			auto SampleSingleTarget = [&](const PCGExData::FConstPoint& Target)
			{
				double DistSquared = Context->TargetsHandler->GetDistSquared(Point, Target);
				if (RangeMax > 0 && (DistSquared < RangeMin || DistSquared > RangeMax)) { return; }
				if (bWeightUseAttr) { DistSquared = Context->TargetWeights[Target.IO]->Read(Target.Index); }
				else if (bWeightUseAttrMult) { DistSquared *= Context->TargetWeights[Target.IO]->Read(Target.Index); }

				bool bReplaceWithCurrent = Union->IsEmpty();

				if (bSampleBest)
				{
					if (SinglePick.Index != -1) { bReplaceWithCurrent = Context->Sorter->Sort(static_cast<PCGExData::FElement>(Target), SinglePick); }
				}
				else if ((bSampleClosest && Det > DistSquared) || (bSampleFarthest && Det < DistSquared))
				{
					bReplaceWithCurrent = true;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = static_cast<PCGExData::FElement>(Target);
					Det = DistSquared;
					Union->Reset();
					Union->AddWeighted_Unsafe(Target, DistSquared);
				}
			};

			auto SampleMultiTarget = [&](const PCGExData::FConstPoint& Target)
			{
				double DistSquared = Context->TargetsHandler->GetDistSquared(Point, Target);
				if (RangeMax > 0 && (DistSquared < RangeMin || DistSquared > RangeMax)) { return; }
				if (bWeightUseAttr) { DistSquared = Context->TargetWeights[Target.IO]->Read(Target.Index); }
				else if (bWeightUseAttrMult) { DistSquared *= Context->TargetWeights[Target.IO]->Read(Target.Index); }

				Union->AddWeighted_Unsafe(Target, DistSquared);
			};

			if (RangeMax > 0)
			{
				const FBox Box = FBoxCenterAndExtent(Origin, FVector(FMath::Sqrt(RangeMax))).GetBox();
				if (bSingleSample) { Context->TargetsHandler->FindElementsWithBoundsTest(Box, SampleSingleTarget, &IgnoreList); }
				else { Context->TargetsHandler->FindElementsWithBoundsTest(Box, SampleMultiTarget, &IgnoreList); }
			}
			else
			{
				if (bSingleSample) { Context->TargetsHandler->ForEachTargetPoint(SampleSingleTarget, &IgnoreList); }
				else { Context->TargetsHandler->ForEachTargetPoint(SampleMultiTarget, &IgnoreList); }
			}

			if (Union->IsEmpty())
			{
				SamplingFailed(Index);
				continue;
			}

			if (Settings->WeightMethod == EPCGExRangeType::FullRange && RangeMax > 0) { Union->WeightRange = RangeMax; }
			DataBlender->ComputeWeights(Index, Union, OutWeightedPoints);

			FTransform WeightedTransform = FTransform::Identity;
			WeightedTransform.SetScale3D(FVector::ZeroVector);

			FVector WeightedUp = SafeUpVector;
			if (Settings->LookAtUpSelection == EPCGExSampleSource::Source) { WeightedUp = LookAtUpGetter->Read(Index); }

			FVector WeightedSignAxis = FVector::ZeroVector;
			FVector WeightedAngleAxis = FVector::ZeroVector;

			double WeightedDistance = Union->GetSqrtWeightAverage();

			// Post-process weighted points and compute local data
			PCGEx::FOpStats SampleTracker{};
			for (PCGExData::FWeightedPoint& P : OutWeightedPoints)
			{
				const double W = Context->WeightCurve->Eval(P.Weight);

				// Don't remap blending if we use external blend ops; they have their own curve
				if (Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic) { P.Weight = W; }

				SampleTracker.Count++;
				SampleTracker.TotalWeight += W;

				const FTransform& TargetTransform = Context->TargetsHandler->GetPoint(P).GetTransform();
				const FQuat TargetRotation = TargetTransform.GetRotation();

				WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::WeightedAdd(WeightedTransform, TargetTransform, W);

				if (Settings->LookAtUpSelection == EPCGExSampleSource::Target)
				{
					WeightedUp = PCGExTypeOps::FTypeOps<FVector>::WeightedAdd(WeightedUp, Context->TargetLookAtUpGetters[P.IO]->Read(P.Index), W);
				}

				WeightedSignAxis += PCGExMath::GetDirection(TargetRotation, Settings->SignAxis) * W;
				WeightedAngleAxis += PCGExMath::GetDirection(TargetRotation, Settings->AngleAxis) * W;
			}

			// Blend using updated weighted points
			DataBlender->Blend(Index, OutWeightedPoints, Trackers);

			if (SampleTracker.TotalWeight != 0) // Dodge NaN
			{
				WeightedUp = PCGExTypeOps::FTypeOps<FVector>::NormalizeWeight(WeightedUp, SampleTracker.TotalWeight);
				WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::NormalizeWeight(WeightedTransform, SampleTracker.TotalWeight);
			}
			else
			{
				WeightedTransform = InTransforms[Index];
			}

			WeightedUp.Normalize();

			const FVector CWDistance = Origin - WeightedTransform.GetLocation();
			FVector LookAt = CWDistance.GetSafeNormal();

			FTransform LookAtTransform = PCGExMath::MakeLookAtTransform(LookAt, WeightedUp, Settings->LookAtAxisAlign);
			if (Context->ApplySampling.WantsApply())
			{
				PCGExData::FMutablePoint MutablePoint(OutPointData, Index);
				Context->ApplySampling.Apply(MutablePoint, WeightedTransform, LookAtTransform);
			}

			SamplingMask[Index] = !Union->IsEmpty();
			PCGEX_OUTPUT_VALUE(Success, Index, !Union->IsEmpty())
			PCGEX_OUTPUT_VALUE(Transform, Index, WeightedTransform)
			PCGEX_OUTPUT_VALUE(LookAtTransform, Index, LookAtTransform)
			PCGEX_OUTPUT_VALUE(Distance, Index, Settings->bOutputNormalizedDistance ? WeightedDistance : WeightedDistance * Settings->DistanceScale)
			PCGEX_OUTPUT_VALUE(SignedDistance, Index, FMath::Sign(WeightedSignAxis.Dot(LookAt)) * WeightedDistance * Settings->SignedDistanceScale)
			PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, Settings->bAbsoluteComponentWiseDistance ? PCGExTypes::Abs(CWDistance) : CWDistance)
			PCGEX_OUTPUT_VALUE(Angle, Index, PCGExSampling::Helpers::GetAngle(Settings->AngleRange, WeightedAngleAxis, LookAt))
			PCGEX_OUTPUT_VALUE(NumSamples, Index, SampleTracker.Count)
			PCGEX_OUTPUT_VALUE(SampledIndex, Index, SinglePick.Index)

			MaxSampledDistanceScoped->Set(Scope, FMath::Max(MaxSampledDistanceScoped->Get(Scope), WeightedDistance));
			bLocalAnySuccess = true;
		}

		if (bLocalAnySuccess) { FPlatformAtomics::InterlockedExchange(&bAnySuccess, 1); }
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (Settings->bOutputNormalizedDistance && DistanceWriter)
		{
			MaxSampledDistance = MaxSampledDistanceScoped->Max();

			const int32 NumPoints = PointDataFacade->GetNum();

			if (Settings->bOutputOneMinusDistance)
			{
				const double InvMaxDist = 1.0 / MaxSampledDistance;
				const double Scale = Settings->DistanceScale;

				for (int i = 0; i < NumPoints; i++)
				{
					const double D = DistanceWriter->GetValue(i);
					DistanceWriter->SetValue(i, (1.0 - D * InvMaxDist) * Scale);
				}
			}
			else
			{
				const double Scale = (1.0 / MaxSampledDistance) * Settings->DistanceScale;

				for (int i = 0; i < NumPoints; i++)
				{
					const double D = DistanceWriter->GetValue(i);
					DistanceWriter->SetValue(i, D * Scale);
				}
			}
		}

		if (UnionBlendOpsManager) { UnionBlendOpsManager->Cleanup(Context); }
		PointDataFacade->WriteFastest(TaskManager);

		if (Settings->bTagIfHasSuccesses && bAnySuccess) { PointDataFacade->Source->Tags->AddRaw(Settings->HasSuccessesTag); }
		if (Settings->bTagIfHasNoSuccesses && !bAnySuccess) { PointDataFacade->Source->Tags->AddRaw(Settings->HasNoSuccessesTag); }
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->bPruneFailedSamples) { (void)PointDataFacade->Source->Gather(SamplingMask); }
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExSampleNearestPointContext, UPCGExSampleNearestPointSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
