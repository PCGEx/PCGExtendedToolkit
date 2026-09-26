// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleNearestPoint.h"

#include "Blenders/PCGExUnionBlender.h"
#include "Blenders/PCGExUnionOpsManager.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Sampling/PCGExSampleAccumulator.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"


#define LOCTEXT_NAMESPACE "PCGExSampleNearestPointElement"
#define PCGEX_NAMESPACE SampleNearestPoint

PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExSampleNearestPointSettings, LookAtUp, FVector, LookAtUpSelection != EPCGExSampleSource::Constant, LookAtUpSource, LookAtUpConstant)

UPCGExSampleNearestPointSettings::UPCGExSampleNearestPointSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (LookAtUpSource.GetName() == FName("@Last"))
	{
		LookAtUpSource.Update(TEXT("$Transform.Up"));
	}
	if (!WeightOverDistance)
	{
		WeightOverDistance = PCGExCurves::WeightDistributionLinear;
	}
}

#if WITH_EDITOR
void UPCGExSampleNearestPointSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameInputPin(PCGPinConstants::DefaultInputLabel, PCGExSampling::Labels::SourceSourceLabel);

	PCGEX_IF_VERSION_LOWER(1, 76, 2)
	{
		// Rewire Range Min
		PCGEX_SHORTHAND_RENAME_PIN(RangeMinAttribute, RangeMin, MinRange)

		// Rewire Range Max
		PCGEX_SHORTHAND_RENAME_PIN(RangeMaxAttribute, RangeMax, MaxRange)
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}

	RetireInputPin(InOutNode, PCGExFilters::Labels::SourceUseValueIfFilters);

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleNearestPointSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 2)
	{
		MinRange.Update(RangeMinInput_DEPRECATED, RangeMinAttribute_DEPRECATED, RangeMin_DEPRECATED);
		MaxRange.Update(RangeMaxInput_DEPRECATED, RangeMaxAttribute_DEPRECATED, RangeMax_DEPRECATED);
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.ApplyDeprecation();
	}
	
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

FName UPCGExSampleNearestPointSettings::GetMainInputPin() const
{
	return PCGExSampling::Labels::SourceSourceLabel;
}

TArray<FPCGPinProperties> UPCGExSampleNearestPointSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, "The point data set to check against.", Required)

	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal, BlendingInterface);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);

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
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SampleMethod == EPCGExSampleMethod::BestCandidate;
	}
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceBlendingLabel)
	{
		return BlendingInterface == EPCGExBlendingInterface::Individual && InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(SampleNearestPoint)

PCGExData::EIOInit UPCGExSampleNearestPointSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleNearestPoint)

bool FPCGExSampleNearestPointElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

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

	if (!Context->BlendingFactories.IsEmpty())
	{
		// Resolve blend op configs once, here, single-threaded: per-processor blender init then
		// only instantiates ops (no concurrent metadata enumeration on shared target facades),
		// and the preloader warms exactly the attribute set the ops will read.
		Context->BlendOpsSchema = MakeShared<PCGExBlending::FBlendOpsSchema>();
		if (!Context->BlendOpsSchema->Init(Context, Context->BlendingFactories, Context->TargetsHandler->GetFacades()))
		{
			return false;
		}
	}

	Context->TargetsHandler->ForEachPreloader([&](PCGExData::FFacadePreloader& Preloader)
	{
		if (Settings->WeightMode != EPCGExSampleWeightMode::Distance)
		{
			Preloader.Register<double>(Context, Settings->WeightAttribute);
		}
		if (Context->BlendOpsSchema)
		{
			Context->BlendOpsSchema->RegisterBuffersDependencies(Context, Preloader);
		}
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

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
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
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				Context->CancelExecution(TEXT("Could not find any points to sample."));
			}
		};

		Context->TargetsHandler->StartLoading(Context->GetTaskManager());
		if (Context->IsWaitingForTasks())
		{
			return false;
		}
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

		Outputs.WriteFailure(Index, Transforms[Index], RangeMaxGetter->Read(Index));
		PCGEX_OUTPUT_VALUE(SampledIndex, Index, -1)
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleNearestPoint::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->bIgnoreSelf)
		{
			IgnoreList.Add(PointDataFacade->GetIn());
		}

		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true);
			!Context->TargetsHandler->PopulateIgnoreList(PointDataFacade->Source, MatchingScope, IgnoreList))
		{
			(void)Context->TargetsHandler->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		if (Context->ApplySampling.WantsApply())
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}
		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		// Filtered-out points that are not processed as fails keep the point: mask 1, never read as garbage.
		SamplingMask.Init(1, PointDataFacade->GetNum());

		{
			PCGExSampling::FCommonOutputConfig Config;
			PCGEX_OUTPUT_CONFIG_FWD_COMMON
			Config.bScaleFailDistance = true;
			Config.bWriteAngleOnFailure = false;
			Config.bNormalizeFailedDistance = true;
			Outputs.Init(PointDataFacade, Config);

			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_OUTPUT_INIT(SampledIndex, int32, -1)
		}

		if (!Context->BlendingFactories.IsEmpty())
		{
			UnionBlendOpsManager = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, Context->TargetsHandler->GetDistances());
			if (!UnionBlendOpsManager->Init(Context, PointDataFacade, Context->TargetsHandler->GetFacades(), Context->BlendOpsSchema))
			{
				return false;
			}
			DataBlender = UnionBlendOpsManager;
		}
		else if (Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic)
		{
			TSet<FName> MissingAttributes;
			PCGExBlending::AssembleBlendingDetails(Settings->PointPropertiesBlendingSettings, Settings->TargetAttributes, Context->TargetsHandler->GetFacades(), BlendingDetails, MissingAttributes);

			UnionBlender = MakeShared<PCGExBlending::FUnionBlender>(&BlendingDetails, nullptr, Context->TargetsHandler->GetDistances());
			UnionBlender->AddSources(Context->TargetsHandler->GetFacades());
			if (!UnionBlender->Init(Context, PointDataFacade))
			{
				return false;
			}
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
				if (!LookAtUpGetter->Init(PointDataFacade))
				{
					return false;
				}
			}
		}
		else
		{
			LookAtUpGetter = PCGExDetails::MakeSettingValue(Settings->LookAtUpConstant);
		}

		RangeMinGetter = Settings->MinRange.GetValueSetting();
		if (!RangeMinGetter->Init(PointDataFacade))
		{
			return false;
		}

		RangeMaxGetter = Settings->MaxRange.GetValueSetting();
		if (!RangeMaxGetter->Init(PointDataFacade))
		{
			return false;
		}

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
		const bool bReadsAttr = bWeightUseAttr || bWeightUseAttrMult;
		const bool bSampleClosest = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget;
		const bool bSampleFarthest = Settings->SampleMethod == EPCGExSampleMethod::FarthestTarget;
		const bool bSampleBest = Settings->SampleMethod == EPCGExSampleMethod::BestCandidate;
		const bool bFullRange = Settings->WeightMethod == EPCGExRangeType::FullRange;
		const bool bMonolithic = Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic;
		const bool bSourceUp = Settings->LookAtUpSelection == EPCGExSampleSource::Source;
		const bool bTargetUp = Settings->LookAtUpSelection == EPCGExSampleSource::Target;

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		bool bLocalAnySuccess = false;

		TArray<PCGEx::FOpStats> Trackers;
		DataBlender->InitTrackers(Trackers);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		PCGExSampling::FSampleAccumulator Acc(Settings->SignAxis, Settings->AngleAxis, Settings->LookAtAxisAlign);

		const bool bProcessFilteredOutAsFails = Settings->bProcessFilteredOutAsFails;
		const double DefaultDet = bSampleClosest ? TNumericLimits<double>::Max() : TNumericLimits<double>::Min();

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				if (bProcessFilteredOutAsFails)
				{
					SamplingFailed(Index);
				}
				continue;
			}

			// Ranges compare by magnitude: Abs both, then order.
			double RangeMin = FMath::Abs(RangeMinGetter->Read(Index));
			double RangeMax = FMath::Abs(RangeMaxGetter->Read(Index));

			if (RangeMin > RangeMax)
			{
				std::swap(RangeMin, RangeMax);
			}

			const double RangeMinSquared = FMath::Square(RangeMin);
			const double RangeMaxSquared = FMath::Square(RangeMax);
			const bool bDeclaredRange = bFullRange && RangeMax > 0;
			const double DeclaredWidth = RangeMax - RangeMin;

			Acc.Reset(bSourceUp ? LookAtUpGetter->Read(Index) : SafeUpVector);

			const PCGExData::FConstPoint Point = PointDataFacade->GetInPoint(Index);
			const FVector Origin = InTransforms[Index].GetLocation();

			PCGExData::FElement SinglePick(-1, -1);
			double Det = DefaultDet;

			auto SampleTarget = [&](const PCGExData::FConstPoint& Target)
			{
				const double DistSquared = Context->TargetsHandler->GetDistSquared(Point, Target);
				if (RangeMax > 0 && (DistSquared < RangeMinSquared || DistSquared > RangeMaxSquared))
				{
					return;
				}

				PCGExSampling::FSampleEntry Entry;
				Entry.Target = static_cast<PCGExData::FElement>(Target);
				Entry.Dist = FMath::Sqrt(DistSquared);

				// Declared range: the weight is known here. The sampled-span case resolves after collection instead.
				if (bDeclaredRange)
				{
					const double T = DeclaredWidth > 0 ? FMath::Clamp((Entry.Dist - RangeMin) / DeclaredWidth, 0.0, 1.0) : 0.0;
					const double Attr = bReadsAttr ? Context->TargetWeights[Target.IO]->Read(Target.Index) : 1.0;
					Entry.Weight = bWeightUseAttr ? Attr : (1.0 - T) * Attr;
				}

				if (!bSingleSample)
				{
					Acc.Entries.Add(Entry);
					return;
				}

				bool bReplaceWithCurrent = Acc.Entries.IsEmpty();

				if (bSampleBest)
				{
					if (SinglePick.Index != -1)
					{
						bReplaceWithCurrent = Context->Sorter->Sort(Entry.Target, SinglePick);
					}
				}
				else if ((bSampleClosest && Det > DistSquared) || (bSampleFarthest && Det < DistSquared))
				{
					bReplaceWithCurrent = true;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = Entry.Target;
					Det = DistSquared;
					Acc.Entries.Reset();
					Acc.Entries.Add(Entry);
				}
			};

			if (RangeMax > 0)
			{
				Context->TargetsHandler->ForEachElementWithBoundsTest(FBoxCenterAndExtent(Origin, FVector(RangeMax)), SampleTarget, &IgnoreList);
			}
			else
			{
				Context->TargetsHandler->ForEachTargetPoint(SampleTarget, &IgnoreList);
			}

			if (Acc.Entries.IsEmpty())
			{
				SamplingFailed(Index);
				continue;
			}

			if (!bDeclaredRange)
			{
				// Linear falloff over the sampled span (Effective Range, or Full Range with no declared max).
				double SampledMin = TNumericLimits<double>::Max();
				double SampledMax = 0;
				for (const PCGExSampling::FSampleEntry& Entry : Acc.Entries)
				{
					SampledMin = FMath::Min(SampledMin, Entry.Dist);
					SampledMax = FMath::Max(SampledMax, Entry.Dist);
				}

				const double Width = SampledMax - SampledMin;
				for (PCGExSampling::FSampleEntry& Entry : Acc.Entries)
				{
					const double T = Width > 0 ? FMath::Clamp((Entry.Dist - SampledMin) / Width, 0.0, 1.0) : 0.0;
					const double Attr = bReadsAttr ? Context->TargetWeights[Entry.Target.IO]->Read(Entry.Target.Index) : 1.0;
					Entry.Weight = bWeightUseAttr ? Attr : (1.0 - T) * Attr;
				}
			}

			Acc.ResolveWeightedPoints();

			for (int32 i = 0; i < Acc.Entries.Num(); i++)
			{
				const PCGExSampling::FSampleEntry& Entry = Acc.Entries[i];
				const double W = Context->WeightCurve->Eval(Entry.Weight);

				// Individual blend ops carry their own curve and keep the raw weight.
				if (bMonolithic)
				{
					Acc.WeightedPoints[i].Weight = W;
				}

				Acc.Add(Context->TargetsHandler->GetTransform(Entry.Target), W, Entry.Dist);
				if (bTargetUp)
				{
					Acc.AddUp(Context->TargetLookAtUpGetters[Entry.Target.IO]->Read(Entry.Target.Index), W);
				}
			}

			DataBlender->Blend(Index, Acc.WeightedPoints, Trackers);
			Acc.Finalize(InTransforms[Index], Origin);

			if (Context->ApplySampling.WantsApply())
			{
				PCGExData::FMutablePoint MutablePoint(OutPointData, Index);
				Context->ApplySampling.Apply(MutablePoint, Acc.WeightedTransform, Acc.LookAtTransform);
			}

			SamplingMask[Index] = true;
			Outputs.WriteSuccess(Index, Acc);
			PCGEX_OUTPUT_VALUE(SampledIndex, Index, SinglePick.Index)

			MaxSampledDistanceScoped->Set(Scope, FMath::Max(MaxSampledDistanceScoped->Get(Scope), Acc.Distance));
			bLocalAnySuccess = true;
		}

		if (bLocalAnySuccess)
		{
			FPlatformAtomics::InterlockedExchange(&bAnySuccess, 1);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		MaxSampledDistance = MaxSampledDistanceScoped->Max();
		Outputs.NormalizeDistances(SamplingMask, MaxSampledDistance);

		if (UnionBlendOpsManager)
		{
			UnionBlendOpsManager->Cleanup(Context);
		}
		PointDataFacade->WriteFastest(TaskManager);

		PCGExSampling::Helpers::ApplySuccessTags(PointDataFacade, bAnySuccess != 0, Settings->bTagIfHasSuccesses, Settings->HasSuccessesTag, Settings->bTagIfHasNoSuccesses, Settings->HasNoSuccessesTag);
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->bPruneFailedSamples)
		{
			(void)PointDataFacade->Source->Gather(SamplingMask);
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExSampleNearestPointContext, UPCGExSampleNearestPointSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
