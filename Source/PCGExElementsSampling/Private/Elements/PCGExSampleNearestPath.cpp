// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleNearestPath.h"

#include "Blenders/PCGExUnionOpsManager.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Math/PCGExMathDistances.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Paths/PCGExPolyPath.h"
#include "Sampling/PCGExSampleAccumulator.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"


#define LOCTEXT_NAMESPACE "PCGExSampleNearestPathElement"
#define PCGEX_NAMESPACE SampleNearestPath

PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExSampleNearestPathSettings, LookAtUp, FVector, LookAtUpSelection != EPCGExSampleSource::Constant, LookAtUpSource, LookAtUpConstant)

UPCGExSampleNearestPathSettings::UPCGExSampleNearestPathSettings(const FObjectInitializer& ObjectInitializer)
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
void UPCGExSampleNearestPathSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	
	InOutNode->RenameInputPin(PCGPinConstants::DefaultInputLabel, PCGExSampling::Labels::SourceSourceLabel);
	
	PCGEX_IF_VERSION_LOWER(1, 74, 3)
	{
		// Rewire alpha
		PCGEX_SHORTHAND_RENAME_PIN(SampleAlphaAttribute, SampleAlphaConstant, SampleAlpha)

		PCGEX_SHORTHAND_RENAME_PIN(RangeMinAttribute, RangeMin, MinRange)

		PCGEX_SHORTHAND_RENAME_PIN(RangeMaxAttribute, RangeMax, MaxRange)
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleNearestPathSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 74, 3)
	{
		SampleAlpha.Update(SampleAlphaInput_DEPRECATED, SampleAlphaAttribute_DEPRECATED, SampleAlphaConstant_DEPRECATED);
		MinRange.Update(RangeMinInput_DEPRECATED, RangeMinAttribute_DEPRECATED, RangeMin_DEPRECATED);
		MaxRange.Update(RangeMaxInput_DEPRECATED, RangeMaxAttribute_DEPRECATED, RangeMax_DEPRECATED);
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.ApplyDeprecation();
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 16)
	{
		bLegacyCurveInput = true;
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

FName UPCGExSampleNearestPathSettings::GetMainInputPin() const
{
	return PCGExSampling::Labels::SourceSourceLabel;
}

TArray<FPCGPinProperties> UPCGExSampleNearestPathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExPaths::Labels::SourcePathsLabel, "The paths to sample.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSampleNearestPathSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExSampleNearestPathSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SampleMethod == EPCGExSampleMethod::BestCandidate;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(SampleNearestPath)

PCGExData::EIOInit UPCGExSampleNearestPathSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleNearestPath)

bool FPCGExSampleNearestPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestPath)

	PCGEX_FOREACH_FIELD_NEARESTPATH(PCGEX_OUTPUT_VALIDATE_NAME)

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->NumMaxTargets = Context->TargetsHandler->InitWithBounds(Context, PCGExPaths::Labels::SourcePathsLabel, [&](const TSharedPtr<PCGExData::FPointIO>& IO, const int32 Idx)-> FBox
	{
		if (IO->GetNum() < 2)
		{
			return FBox(ForceInit);
		}

		const bool bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(IO->GetIn());

		switch (Settings->SampleInputs)
		{
		default: case EPCGExPathSamplingIncludeMode::All:
			break;
		case EPCGExPathSamplingIncludeMode::ClosedLoopOnly:
			if (!bClosedLoop)
			{
				return FBox(ForceInit);
			}
			break;
		case EPCGExPathSamplingIncludeMode::OpenLoopsOnly:
			if (bClosedLoop)
			{
				return FBox(ForceInit);
			}
			break;
		}

		// TODO : We could support per-point project here but ugh
		TSharedPtr<PCGExPaths::FPolyPath> Path = MakeShared<PCGExPaths::FPolyPath>(IO, Settings->ProjectionDetails, 1, Settings->HeightInclusion);
		Path->OffsetProjection(Settings->InclusionOffset);

		if (!Path->Bounds.IsValid)
		{
			return FBox(ForceInit);
		}

		Path->IOIndex = IO->IOIndex;
		Path->Idx = Idx;

		Context->Paths.Add(Path);

		return Path->Bounds;
	});

	Context->NumMaxTargets = Context->TargetsHandler->GetMaxNumTargets();
	if (!Context->NumMaxTargets)
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No targets (no input matches criteria)"))
		return false;
	}

	Context->TargetsHandler->SetDistances(Settings->DistanceSettings, Settings->DistanceSettings, false);

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

		Context->TargetsHandler->ForEachPreloader([&](PCGExData::FFacadePreloader& Preloader)
		{
			Context->BlendOpsSchema->RegisterBuffersDependencies(Context, Preloader);
		});
	}

	Context->WeightCurve = Settings->WeightCurveLookup.MakeLookup(
		Settings->bUseLocalCurve, Settings->LocalWeightOverDistance, Settings->WeightOverDistance,
		[](FRichCurve& CurveData)
		{
			CurveData.AddKey(0, 0);
			CurveData.AddKey(1, 1);
		});

	return true;
}

bool FPCGExSampleNearestPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleNearestPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestPath)
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
				Context->CancelExecution(TEXT("LookUp Attribute on Targets is invalid."));
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
				Context->CancelExecution(TEXT("Could not find any paths to split."));
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

namespace PCGExSampleNearestPath
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleNearestPath::Process)

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

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEX_INIT_IO)
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		}

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		if (Context->ApplySampling.WantsApply())
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		// Filtered-out points that are not processed as fails keep the point: mask 1, never read as garbage.
		SamplingMask.Init(1, PointDataFacade->GetNum());

		if (Settings->SampleInputs != EPCGExPathSamplingIncludeMode::All)
		{
			bOnlySignIfClosed = Settings->bOnlySignIfClosed;
			bOnlyIncrementInsideNumIfClosed = Settings->bOnlyIncrementInsideNumIfClosed;
		}
		else
		{
			bOnlySignIfClosed = false;
			bOnlyIncrementInsideNumIfClosed = false;
		}

		SafeUpVector = Settings->LookAtUpConstant;

		if (!Context->BlendingFactories.IsEmpty())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(InitBlendingFactories)
			UnionBlendOpsManager = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, Context->TargetsHandler->GetDistances());
			if (!UnionBlendOpsManager->Init(Context, PointDataFacade, Context->TargetsHandler->GetFacades(), Context->BlendOpsSchema))
			{
				return false;
			}
			DataBlender = UnionBlendOpsManager;
		}

		if (!DataBlender)
		{
			TSharedPtr<PCGExBlending::FDummyUnionBlender> DummyUnionBlender = MakeShared<PCGExBlending::FDummyUnionBlender>();
			DummyUnionBlender->Init(PointDataFacade, Context->TargetsHandler->GetFacades());
			DataBlender = DummyUnionBlender;
		}

		{
			PCGExSampling::FCommonOutputConfig Config;
			PCGEX_OUTPUT_CONFIG_FWD_COMMON
			Config.bScaleFailDistance = true;
			Config.bWriteAngleOnFailure = true;
			Config.bNormalizeFailedDistance = true;
			Outputs.Init(PointDataFacade, Config);

			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_NEARESTPATH_EXTRA(PCGEX_OUTPUT_INIT)
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

		if (Settings->bSampleSpecificAlpha)
		{
			SampleAlphaGetter = Settings->SampleAlpha.GetValueSetting();
			if (!SampleAlphaGetter->Init(PointDataFacade))
			{
				return false;
			}
		}

		if (Settings->LookAtUpSelection == EPCGExSampleSource::Source)
		{
			LookAtUpGetter = PointDataFacade->GetBroadcaster<FVector>(Settings->LookAtUpSource, true);
			if (!LookAtUpGetter)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("LookAtUp is invalid."));
			}
		}

		bSingleSample = Settings->SampleMethod != EPCGExSampleMethod::WithinRange;
		bClosestSample = Settings->SampleMethod != EPCGExSampleMethod::FarthestTarget;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExSampleNearestPathContext, UPCGExSampleNearestPathSettings>::PrepareLoopScopesForPoints(Loops);
		MaxSampledDistanceScoped = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
	}

	void FProcessor::SamplingFailed(const int32 Index)
	{
		SamplingMask[Index] = false;

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		Outputs.WriteFailure(Index, InTransforms[Index], RangeMaxGetter->Read(Index));
		PCGEX_OUTPUT_VALUE(SegmentTime, Index, -1)
		PCGEX_OUTPUT_VALUE(Time, Index, -1)
		PCGEX_OUTPUT_VALUE(NumInside, Index, -1)
		PCGEX_OUTPUT_VALUE(ClosedLoop, Index, false)
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleNearestPath::ProcessPoints);

		const bool bSampleClosest = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget;
		const bool bSampleFarthest = Settings->SampleMethod == EPCGExSampleMethod::FarthestTarget;
		const bool bSampleBest = Settings->SampleMethod == EPCGExSampleMethod::BestCandidate;

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		bool bAnySuccessLocal = false;

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		TArray<PCGEx::FOpStats> Trackers;
		DataBlender->InitTrackers(Trackers);

		const PCGExMath::IDistances* Distances = Context->TargetsHandler->GetDistances();

		const bool bTargetUp = Settings->LookAtUpSelection == EPCGExSampleSource::Target;
		PCGExSampling::FSampleAccumulator Acc(Settings->SignAxis, Settings->AngleAxis, Settings->LookAtAxisAlign);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				if (Settings->bProcessFilteredOutAsFails)
				{
					SamplingFailed(Index);
				}
				continue;
			}

			int32 NumInside = 0;
			int32 NumInClosed = 0;

			bool bSampledClosedLoop = false;

			Acc.Reset(LookAtUpGetter ? LookAtUpGetter->Read(Index).GetSafeNormal() : SafeUpVector);

			double RangeMin = RangeMinGetter->Read(Index);
			double RangeMax = RangeMaxGetter->Read(Index);

			if (RangeMin > RangeMax)
			{
				std::swap(RangeMin, RangeMax);
			}

			PCGExData::FConstPoint Point = PointDataFacade->GetInPoint(Index);
			const FTransform& Transform = InTransforms[Index];
			FVector Origin = Transform.GetLocation();

			PCGExData::FElement SinglePick(-1, -1);
			double BestDist = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget ? TNumericLimits<double>::Max() : TNumericLimits<double>::Min();

			double WeightedTime = 0;
			double WeightedSegmentTime = 0;

			// Samples are collected first so weights can be resolved against the sampled range.
			struct FSampleEntry
			{
				FTransform SampleTransform;
				PCGExData::FElement A;
				PCGExData::FElement B;
				double Dist;
				double EdgeLerp;
				double Time;
				double SegmentTime;
				bool bInside;
			};

			TArray<FSampleEntry, TInlineAllocator<8>> SampleEntries;

			auto SampleSingle = [&](const PCGExData::FElement& EdgeElement, const FSampleEntry& Entry, const int32 NumInsideIncrement, const bool bClosedLoop)
			{
				bool bReplaceWithCurrent = SampleEntries.IsEmpty();

				if (bSampleBest)
				{
					if (SinglePick.Index != -1)
					{
						bReplaceWithCurrent = Context->Sorter->Sort(EdgeElement, SinglePick);
					}
				}
				else if ((bSampleClosest && BestDist > Entry.Dist) || (bSampleFarthest && BestDist < Entry.Dist))
				{
					bReplaceWithCurrent = true;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = EdgeElement;
					BestDist = Entry.Dist;

					SampleEntries.Reset();
					SampleEntries.Add(Entry);

					NumInside = NumInsideIncrement;
					NumInClosed = bSampledClosedLoop = bClosedLoop;
				}
			};

			auto SampleMulti = [&](const FSampleEntry& Entry, const int32 NumInsideIncrement, const bool bClosedLoop)
			{
				SampleEntries.Add(Entry);

				if (bClosedLoop)
				{
					bSampledClosedLoop = true;
					NumInClosed += NumInsideIncrement;
				}

				NumInside += NumInsideIncrement;
			};

			auto SampleTarget = [&](const int32 EdgeIndex, const float Lerp, const TSharedPtr<PCGExPaths::FPolyPath>& InPath, const FTransform& SampleTransform)
			{
				PCGExData::FElement EdgeElement;
				PCGExData::FElement A;
				PCGExData::FElement B;
				InPath->GetEdgeElements(EdgeIndex, EdgeElement, A, B);

				const bool bClosedLoop = InPath->IsClosedLoop();
				const bool bIsInside = InPath->IsInsideProjection(Transform.GetLocation());

				if (Settings->bOnlySampleWhenInside && !bIsInside)
				{
					return;
				}

				const int32 NumInsideIncrement = bIsInside && (!bOnlyIncrementInsideNumIfClosed || bClosedLoop);
				const FVector SampleLocation = SampleTransform.GetLocation();
				const FVector ModifiedOrigin = Distances->GetSourceCenter(Point, Origin, SampleLocation);
				const double Dist = Distances->GetDist(ModifiedOrigin, SampleLocation);

				if (RangeMax > 0
					&& (Dist < RangeMin || Dist > RangeMax)
					&& (!Settings->bAlwaysSampleWhenInside || !bIsInside))
				{
					return;
				}

				const double Time = (static_cast<double>(EdgeIndex) + Lerp) / static_cast<double>(InPath->NumEdges);
				const FSampleEntry Entry{SampleTransform, A, B, Dist, Lerp, Time, Lerp, bIsInside};

				if (bSingleSample)
				{
					SampleSingle(EdgeElement, Entry, NumInsideIncrement, bClosedLoop);
				}
				else
				{
					SampleMulti(Entry, NumInsideIncrement, bClosedLoop);
				}
			};


			const FBox QueryBounds = FBox(Origin - FVector(RangeMax), Origin + FVector(RangeMax));

			// First: Sample all possible targets
			if (!Settings->bSampleSpecificAlpha)
			{
				// At closest alpha
				Context->TargetsHandler->FindTargetsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Target)
				{
					if (!Context->Paths.IsValidIndex(Target.Index))
					{
						return;
					} // TODO : Look into why there's a discrepency between paths & targets

					const TSharedPtr<PCGExPaths::FPolyPath> Path = Context->Paths[Target.Index];
					float Lerp = 0;
					int32 EdgeIndex = 0;
					const FTransform SampleTransform = Path->GetClosestTransform(Origin, EdgeIndex, Lerp);
					SampleTarget(EdgeIndex, Lerp, Path, SampleTransform);
				}, &IgnoreList);
			}
			else
			{
				// At specific alpha
				const double InputKey = SampleAlphaGetter->Read(Index);
				Context->TargetsHandler->FindTargetsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Target)
				{
					if (!Context->Paths.IsValidIndex(Target.Index))
					{
						return;
					} // TODO : Look into why there's a discrepency between paths & targets

					const TSharedPtr<PCGExPaths::FPolyPath>& Path = Context->Paths[Target.Index];
					double Time = 0;

					switch (Settings->SampleAlphaMode)
					{
					default: case EPCGExPathSampleAlphaMode::Alpha:
						Time = InputKey;
						break;
					case EPCGExPathSampleAlphaMode::Time:
						Time = InputKey / Path->NumEdges;
						break;
					case EPCGExPathSampleAlphaMode::Distance:
						Time = InputKey / Path->TotalLength;
						break;
					}

					if (Settings->bWrapClosedLoopAlpha && Path->IsClosedLoop())
					{
						Time = PCGExMath::Tile(Time, 0.0, 1.0);
					}

					float Lerp = 0;
					const int32 EdgeIndex = Path->GetClosestEdge(Time, Lerp);
					const FTransform SampleTransform = Path->GetTransformAtInputKey(EdgeIndex + Lerp);

					SampleTarget(EdgeIndex, Lerp, Path, SampleTransform);
				});
			}

			if (SampleEntries.IsEmpty())
			{
				SamplingFailed(Index);
				continue;
			}

			// Compute per-sample range stats for weight curve
			double SampledRangeMin = TNumericLimits<double>::Max();
			double SampledRangeMax = 0;
			for (const FSampleEntry& Entry : SampleEntries)
			{
				SampledRangeMin = FMath::Min(SampledRangeMin, Entry.Dist);
				SampledRangeMax = FMath::Max(SampledRangeMax, Entry.Dist);
			}

			if (Settings->WeightMethod == EPCGExRangeType::FullRange && RangeMax > 0)
			{
				SampledRangeMin = RangeMin;
				SampledRangeMax = RangeMax;
			}

			const double SampledRangeWidth = SampledRangeMax - SampledRangeMin;

			Acc.WeightedPoints.Reset(SampleEntries.Num() * 2);

			for (const FSampleEntry& Entry : SampleEntries)
			{
				const double W = Settings->InsideWeighting.GetWeight(Entry.Dist, Entry.bInside, SampledRangeMin, SampledRangeMax);
				const double Scale = Settings->InsideWeighting.GetScale(Entry.bInside);

				// Blend ops curve the sample weight; the edge lerp rides in Split so the curve never bends the endpoint ratio.
				Acc.WeightedPoints.Emplace(Entry.A.Index, W * Scale, Entry.A.IO, 1.0 - Entry.EdgeLerp);
				Acc.WeightedPoints.Emplace(Entry.B.Index, W * Scale, Entry.B.IO, Entry.EdgeLerp);

				// Legacy nodes fed the curve the normalized distance (0 at the closest edge) instead of the weight.
				const double CurveInput = Settings->bLegacyCurveInput
					? (SampledRangeWidth > 0 ? FMath::Clamp(Entry.Dist - SampledRangeMin, 0.0, SampledRangeWidth) / SampledRangeWidth : 0.0)
					: W;
				const double Weight = Context->WeightCurve->Eval(CurveInput) * Scale;

				Acc.Add(Entry.SampleTransform, Weight, Entry.Dist);
				if (bTargetUp)
				{
					Acc.AddUp(PCGExMath::GetDirection(Entry.SampleTransform.GetRotation(), Settings->LookAtUpAxis), Weight);
				}

				WeightedTime += Entry.Time * Weight;
				WeightedSegmentTime += Entry.SegmentTime * Weight;
			}

			// The uniform fallback only touches Weight; Split keeps every sample on its segment.
			Acc.ApplyZeroTotalFallback();
			DataBlender->Blend(Index, Acc.WeightedPoints, Trackers);
			Acc.Finalize(InTransforms[Index], Origin);

			if (Acc.TotalWeight != 0) // Dodge NaN
			{
				WeightedTime /= Acc.TotalWeight;
				WeightedSegmentTime /= Acc.TotalWeight;
			}

			if (Context->ApplySampling.WantsApply())
			{
				PCGExData::FMutablePoint MutablePoint = PointDataFacade->GetOutPoint(Index);
				Context->ApplySampling.Apply(MutablePoint, Acc.WeightedTransform, Acc.LookAtTransform);
			}

			SamplingMask[Index] = true;
			Outputs.WriteSuccess(Index, Acc, !bOnlySignIfClosed || NumInClosed > 0);
			PCGEX_OUTPUT_VALUE(SegmentTime, Index, WeightedSegmentTime)
			PCGEX_OUTPUT_VALUE(Time, Index, WeightedTime)
			PCGEX_OUTPUT_VALUE(NumInside, Index, NumInside)
			PCGEX_OUTPUT_VALUE(ClosedLoop, Index, bSampledClosedLoop)

			MaxSampledDistanceScoped->Set(Scope, FMath::Max(MaxSampledDistanceScoped->Get(Scope), Acc.Distance));
			bAnySuccessLocal = true;
		}

		if (bAnySuccessLocal)
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
		TProcessor<FPCGExSampleNearestPathContext, UPCGExSampleNearestPathSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
