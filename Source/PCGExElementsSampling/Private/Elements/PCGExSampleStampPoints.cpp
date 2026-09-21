// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleStampPoints.h"

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
#include "Helpers/PCGExAsyncHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Helpers/PCGExTargetsRangeIndex.h"
#include "Sampling/PCGExSampleAccumulator.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"


#define LOCTEXT_NAMESPACE "PCGExSampleStampPointsElement"
#define PCGEX_NAMESPACE SampleStampPoints

PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExSampleStampPointsSettings, LookAtUp, FVector, LookAtUpSelection != EPCGExSampleSource::Constant, LookAtUpSource, LookAtUpConstant)

UPCGExSampleStampPointsSettings::UPCGExSampleStampPointsSettings(const FObjectInitializer& ObjectInitializer)
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

FName UPCGExSampleStampPointsSettings::GetMainInputPin() const
{
	return PCGExSampling::Labels::SourceSourceLabel;
}

TArray<FPCGPinProperties> UPCGExSampleStampPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, "The points that stamp their values onto sources within their range.", Required)

	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal, BlendingInterface);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);

	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceUseValueIfFilters, "Filter which points values will be processed.", Advanced)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSampleStampPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExSampleStampPointsSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
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

PCGEX_INITIALIZE_ELEMENT(SampleStampPoints)

PCGExData::EIOInit UPCGExSampleStampPointsSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleStampPoints)

bool FPCGExSampleStampPointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleStampPoints)

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	PCGEX_FOREACH_FIELD_STAMPPOINTS(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->BlendingInterface == EPCGExBlendingInterface::Individual)
	{
		PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);
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

		Settings->TargetMinRange.RegisterBufferDependencies(Context, Preloader);
		Settings->TargetMaxRange.RegisterBufferDependencies(Context, Preloader);
		Settings->TargetRangeScale.RegisterBufferDependencies(Context, Preloader);

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

bool FPCGExSampleStampPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleStampPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleStampPoints)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

			const int32 NumTargets = Context->TargetsHandler->Num();

			// Range getters live only until the index has baked them into per-point arrays.
			TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> MinRanges;
			TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> MaxRanges;
			TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> RangeScales;
			MinRanges.SetNum(NumTargets);
			MaxRanges.SetNum(NumTargets);
			RangeScales.SetNum(NumTargets);

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
					TSharedPtr<PCGExDetails::TSettingValue<FVector>> LookAtUpGetter = Settings->GetValueSettingLookAtUp();
					if (!LookAtUpGetter->Init(Target, false))
					{
						bBreak = true;
						return;
					}

					Context->TargetLookAtUpGetters.Add(LookAtUpGetter);
				}

				// Prep per-target ranges
				TSharedPtr<PCGExDetails::TSettingValue<double>> MinRange = Settings->TargetMinRange.GetValueSetting();
				TSharedPtr<PCGExDetails::TSettingValue<double>> MaxRange = Settings->TargetMaxRange.GetValueSetting();
				TSharedPtr<PCGExDetails::TSettingValue<double>> RangeScale = Settings->TargetRangeScale.GetValueSetting();

				if (!MinRange->Init(Target, false) || !MaxRange->Init(Target, false) || !RangeScale->Init(Target, false))
				{
					bBreak = true;
					return;
				}

				MinRanges[TargetIndex] = MinRange;
				MaxRanges[TargetIndex] = MaxRange;
				RangeScales[TargetIndex] = RangeScale;
			});

			if (bError)
			{
				Context->CancelExecution();
				return;
			}

			// Per-target range octrees. The range is an attribute, so this has to follow the preload; each task writes
			// only its own entry, touches only the by-value captures, and the scope blocks until every target is indexed.
			Context->RangeIndex = MakeShared<PCGExMatching::FTargetsRangeIndex>(Context->TargetsHandler.ToSharedRef());
			{
				PCGExAsyncHelpers::FAsyncExecutionScope BuildTasks(NumTargets);
				const EPCGExDistance TargetDistanceMode = Settings->DistanceDetails.Target;

				for (int32 IO = 0; IO < NumTargets; IO++)
				{
					BuildTasks.Execute(
						[IO, TargetDistanceMode, RangeIndex = Context->RangeIndex, MinRange = MinRanges[IO], MaxRange = MaxRanges[IO], RangeScale = RangeScales[IO]]()
						{
							RangeIndex->BuildTarget(
								IO, TargetDistanceMode,
								[&](const int32 PointIndex, double& OutMin, double& OutMax)
								{
									const double Scale = FMath::Abs(RangeScale->Read(PointIndex));
									OutMin = MinRange->Read(PointIndex) * Scale;
									OutMax = MaxRange->Read(PointIndex) * Scale;
								});
						});
				}
			}
			Context->RangeIndex->BuildDataOctree();

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

namespace PCGExSampleStampPoints
{
	FProcessor::~FProcessor()
	{
	}

	void FProcessor::SamplingFailed(const int32 Index)
	{
		SamplingMask[Index] = false;

		const TConstPCGValueRange<FTransform> Transforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		Outputs.WriteFailure(Index, Transforms[Index], Settings->FailedSampleDistance);
		PCGEX_OUTPUT_VALUE(SampledIndex, Index, -1)
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleStampPoints::Process);

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
			Config.bScaleFailDistance = false;
			Config.bWriteAngleOnFailure = true;
			Config.bNormalizeFailedDistance = false;
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

		if (Settings->RangeMode == EPCGExStampRangeMode::Combined)
		{
			SourceMinGetter = Settings->SourceMinRange.GetValueSetting();
			SourceMaxGetter = Settings->SourceMaxRange.GetValueSetting();
			SourceScaleGetter = Settings->SourceRangeScale.GetValueSetting();

			if (!SourceMinGetter->Init(PointDataFacade) || !SourceMaxGetter->Init(PointDataFacade) || !SourceScaleGetter->Init(PointDataFacade))
			{
				return false;
			}
		}
		else
		{
			SourceMinGetter = PCGExDetails::MakeSettingValue<double>(0.0);
			SourceMaxGetter = PCGExDetails::MakeSettingValue<double>(0.0);
			SourceScaleGetter = PCGExDetails::MakeSettingValue<double>(1.0);
		}

		bSingleSample = Settings->SampleMethod != EPCGExSampleMethod::WithinRange;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExSampleStampPointsContext, UPCGExSampleStampPointsSettings>::PrepareLoopScopesForPoints(Loops);
		MaxSampledDistanceScoped = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleStampPoints::ProcessPoints);

		const bool bWeightUseAttr = Settings->WeightMode == EPCGExSampleWeightMode::Attribute;
		const bool bWeightUseAttrMult = Settings->WeightMode == EPCGExSampleWeightMode::AttributeMult;
		const bool bReadsAttr = bWeightUseAttr || bWeightUseAttrMult;
		const bool bFullRange = Settings->WeightMethod == EPCGExRangeType::FullRange;
		const bool bSampleClosest = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget;
		const bool bSampleFarthest = Settings->SampleMethod == EPCGExSampleMethod::FarthestTarget;
		const bool bSampleBest = Settings->SampleMethod == EPCGExSampleMethod::BestCandidate;
		const bool bMonolithic = Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic;
		const bool bSourceUp = Settings->LookAtUpSelection == EPCGExSampleSource::Source;
		const bool bTargetUp = Settings->LookAtUpSelection == EPCGExSampleSource::Target;
		const EPCGExDistance SourceDistanceMode = Settings->DistanceDetails.Source;

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

			Acc.Reset(bSourceUp ? LookAtUpGetter->Read(Index) : SafeUpVector);

			// Source-side contribution to the effective range; 0 unless RangeMode is Combined. Ordered here so the
			// per-pair sum of two ordered ranges stays ordered and the query box only needs the source max.
			const double SourceScale = FMath::Abs(SourceScaleGetter->Read(Index));
			double SourceMin = FMath::Max(0.0, SourceMinGetter->Read(Index) * SourceScale);
			double SourceMax = FMath::Max(0.0, SourceMaxGetter->Read(Index) * SourceScale);
			if (SourceMin > SourceMax)
			{
				std::swap(SourceMin, SourceMax);
			}

			const PCGExData::FConstPoint Point = PointDataFacade->GetInPoint(Index);
			const FVector Origin = InTransforms[Index].GetLocation();

			PCGExData::FElement SinglePick(-1, -1);
			double Det = DefaultDet;

			auto ResolveFullRangeWeight = [&](PCGExSampling::FSampleEntry& Entry, const double Min, const double Max)
			{
				const double Width = Max - Min;
				const double T = Width > 0 ? FMath::Clamp((Entry.Dist - Min) / Width, 0.0, 1.0) : 0.0;
				const double Attr = bReadsAttr ? Context->TargetWeights[Entry.Target.IO]->Read(Entry.Target.Index) : 1.0;
				Entry.Weight = bWeightUseAttr ? Attr : (1.0 - T) * Attr;
			};

			auto SampleTarget = [&](const PCGExData::FConstPoint& Target)
			{
				const double Dist = FMath::Sqrt(Context->TargetsHandler->GetDistSquared(Point, Target));

				double Min = 0;
				double Max = 0;
				Context->RangeIndex->GetRange(Target.IO, Target.Index, Min, Max);
				Min += SourceMin;
				Max += SourceMax;

				if (Dist < Min || Dist > Max)
				{
					return;
				}

				PCGExSampling::FSampleEntry Entry;
				Entry.Target = static_cast<PCGExData::FElement>(Target);
				Entry.Dist = Dist;

				if (!bSingleSample)
				{
					// Per-pair range: the weight is known here. Effective Range resolves after collection instead.
					if (bFullRange)
					{
						ResolveFullRangeWeight(Entry, Min, Max);
					}
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
				else if ((bSampleClosest && Det > Dist) || (bSampleFarthest && Det < Dist))
				{
					bReplaceWithCurrent = true;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = Entry.Target;
					Det = Dist;
					Acc.Entries.Reset();
					Acc.Entries.Add(Entry);
				}
			};

			const FBox QueryBox = PCGExMatching::FTargetsRangeIndex::GetSpatializedBox(Point, SourceDistanceMode).ExpandBy(SourceMax);
			Context->RangeIndex->ForEachElementWithBoundsTest(FBoxCenterAndExtent(QueryBox), SampleTarget, &IgnoreList);

			if (Acc.Entries.IsEmpty())
			{
				SamplingFailed(Index);
				continue;
			}

			if (bFullRange && bSingleSample)
			{
				// Single pick: resolve the survivor only, instead of every candidate that lost the pick.
				PCGExSampling::FSampleEntry& Entry = Acc.Entries[0];
				double Min = 0;
				double Max = 0;
				Context->RangeIndex->GetRange(Entry.Target.IO, Entry.Target.Index, Min, Max);
				ResolveFullRangeWeight(Entry, Min + SourceMin, Max + SourceMax);
			}
			else if (!bFullRange)
			{
				// Effective Range: linear falloff over the sampled span.
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
		TProcessor<FPCGExSampleStampPointsContext, UPCGExSampleStampPointsSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
