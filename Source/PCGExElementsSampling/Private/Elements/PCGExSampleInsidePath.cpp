// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleInsidePath.h"

#include "Blenders/PCGExUnionOpsManager.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
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
#include "Sampling/PCGExSamplingUnionData.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"

#define LOCTEXT_NAMESPACE "PCGExSampleInsidePathElement"
#define PCGEX_NAMESPACE SampleInsidePath

UPCGExSampleInsidePathSettings::UPCGExSampleInsidePathSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void UPCGExSampleInsidePathSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 74, 3)
	{
		PCGEX_SHORTHAND_RENAME_PIN(RangeMinAttribute, RangeMin, MinRange)

		PCGEX_SHORTHAND_RENAME_PIN(RangeMaxAttribute, RangeMax, MaxRange)
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleInsidePathSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 74, 3)
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

FName UPCGExSampleInsidePathSettings::GetMainInputPin() const
{
	return PCGExPaths::Labels::SourcePathsLabel;
}

FName UPCGExSampleInsidePathSettings::GetMainOutputPin() const
{
	return PCGExPaths::Labels::OutputPathsLabel;
}

TArray<FPCGPinProperties> UPCGExSampleInsidePathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, "The points to sample.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSampleInsidePathSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (OutputMode == EPCGExSampleInsidePathOutput::Split)
	{
		PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discard inputs are paths that failed to sample any points, despite valid targets.", Normal)
	}
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExSampleInsidePathSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SampleMethod == EPCGExSampleMethod::BestCandidate;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(SampleInsidePath)

PCGExData::EIOInit UPCGExSampleInsidePathSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleInsidePath)

bool FPCGExSampleInsidePathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleInsidePath)

	PCGEX_FOREACH_FIELD_INSIDEPATH(PCGEX_OUTPUT_VALIDATE_NAME)

	if (!Settings->MinRange.CanSupportDataOnly())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Min Range attribute must be on the @Data domain"));
		return false;
	}

	if (!Settings->MaxRange.CanSupportDataOnly())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Max Range attribute must be on the @Data domain"));
		return false;
	}

	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->NumMaxTargets = Context->TargetsHandler->InitWithBounds(Context, PCGExCommon::Labels::SourceTargetsLabel, [&](const TSharedPtr<PCGExData::FPointIO>& IO, const int32 Idx)-> FBox
	{
		const bool bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(IO->GetIn());

		switch (Settings->ProcessInputs)
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

		return IO->GetIn()->GetBounds();
	});

	Context->NumMaxTargets = Context->TargetsHandler->GetMaxNumTargets();
	if (!Context->NumMaxTargets)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No targets (no input matches criteria)"));
		return false;
	}

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

	return true;
}

bool FPCGExSampleInsidePathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleInsidePathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleInsidePath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)
			if (Context->Sorter && !Context->Sorter->Init(Context, Context->TargetsHandler->GetFacades()))
			{
				Context->CancelExecution(TEXT("Invalid sort rules"));
				return;
			}

			Context->TargetsHandler->SetMatchingDetails(Context, &Settings->DataMatching);

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

namespace PCGExSampleInsidePath
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleInsidePath::Process);

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

		Path = MakeShared<PCGExPaths::FPolyPath>(PointDataFacade, Settings->ProjectionDetails, 1, Settings->HeightInclusion);
		Path->OffsetProjection(Settings->InclusionOffset);

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		if (Settings->ProcessInputs != EPCGExPathSamplingIncludeMode::All)
		{
			bOnlyIncrementInsideNumIfClosed = Settings->bOnlyIncrementInsideNumIfClosed;
		}
		else
		{
			bOnlyIncrementInsideNumIfClosed = false;
		}

		Distances = PCGExMath::GetDistances(EPCGExDistance::Center, EPCGExDistance::Center, false, Settings->DistanceType);

		if (!Context->BlendingFactories.IsEmpty())
		{
			UnionBlendOpsManager = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, Distances);
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
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_INSIDEPATH(PCGEX_OUTPUT_INIT)
		}

		if (!Settings->MinRange.TryReadDataValue(Context, PointDataFacade->GetIn(), RangeMin))
		{
			return false;
		}
		if (!Settings->MaxRange.TryReadDataValue(Context, PointDataFacade->GetIn(), RangeMax))
		{
			return false;
		}

		if (RangeMin > RangeMax)
		{
			std::swap(RangeMin, RangeMax);
		}


		bSingleSample = Settings->SampleMethod != EPCGExSampleMethod::WithinRange;
		bClosestSample = Settings->SampleMethod != EPCGExSampleMethod::FarthestTarget;

		SampleBox = PointDataFacade->GetIn()->GetBounds().ExpandBy(RangeMax);

		ProcessPath();

		return true;
	}

	void FProcessor::ProcessPath()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleInsidePath::ProcessPath);

		constexpr int32 Index = 0; // Only support writing to @Data domain, otherwise will write data to the first point of the path

		TArray<PCGExData::FWeightedPoint> OutWeightedPoints;
		OutWeightedPoints.Reserve(256);

		TArray<PCGEx::FOpStats> Trackers;
		DataBlender->InitTrackers(Trackers);

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();
		Union->Reserve(Context->TargetsHandler->Num(), RangeMax ? 8 : Context->NumMaxTargets);
		Union->Reset();
		Union->WeightRange = -2; // Weights are resolved below; the union passes them through verbatim

		// Samples are collected first so weights can be resolved against the sampled range.
		struct FSampleEntry
		{
			PCGExData::FElement Element;
			double Dist = 0;
			bool bInside = false;
		};

		TArray<FSampleEntry> Samples;
		Samples.Reserve(256);

		int32 NumInside = 0;

		PCGExData::FElement SinglePick(-1, -1);
		double BestDist = Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget ? TNumericLimits<double>::Max() : TNumericLimits<double>::Min();

		auto SampleTarget = [&](const PCGExData::FConstPoint& Target)
		{
			const FVector SampleLocation = Target.GetTransform().GetLocation();

			const bool bIsInside = Path->IsInsideProjection(SampleLocation);

			if (Settings->bOnlySampleWhenInside && !bIsInside)
			{
				return;
			}

			int32 NumInsideIncrement = 0;
			if (bIsInside)
			{
				if (!bOnlyIncrementInsideNumIfClosed || Path->IsClosedLoop())
				{
					NumInsideIncrement = 1;
				}
			}

			float Alpha = 0;
			const int32 EdgeIndex = Path->GetClosestEdge(SampleLocation, Alpha);

			const FVector PathLocation = FMath::Lerp(Path->GetPos(EdgeIndex), Path->GetPos(EdgeIndex + 1), Alpha);
			const double Dist = Distances->GetDist(PathLocation, SampleLocation);

			if (RangeMax > 0 && (Dist < RangeMin || Dist > RangeMax))
			{
				if (!Settings->bAlwaysSampleWhenInside || !bIsInside)
				{
					return;
				}
			}

			const FSampleEntry Entry{static_cast<PCGExData::FElement>(Target), Dist, bIsInside};

			if (bSingleSample)
			{
				bool bReplaceWithCurrent = Samples.IsEmpty();

				if (Settings->SampleMethod == EPCGExSampleMethod::BestCandidate)
				{
					if (SinglePick.Index != -1)
					{
						bReplaceWithCurrent = Context->Sorter->Sort(Entry.Element, SinglePick);
					}
				}
				else if (Settings->SampleMethod == EPCGExSampleMethod::ClosestTarget && BestDist > Dist)
				{
					bReplaceWithCurrent = true;
				}
				else if (Settings->SampleMethod == EPCGExSampleMethod::FarthestTarget && BestDist < Dist)
				{
					bReplaceWithCurrent = true;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = Entry.Element;
					BestDist = Dist;

					Samples.Reset();
					Samples.Add(Entry);

					NumInside = NumInsideIncrement;
				}
			}
			else
			{
				Samples.Add(Entry);
				NumInside += NumInsideIncrement;
			}
		};

		Context->TargetsHandler->FindElementsWithBoundsTest(SampleBox, SampleTarget, &IgnoreList);

		if (Samples.IsEmpty())
		{
			SamplingFailed(Index);
			return;
		}

		double WeightedDistance = 0;
		double SampledRangeMin = TNumericLimits<double>::Max();
		double SampledRangeMax = 0;
		for (const FSampleEntry& Entry : Samples)
		{
			WeightedDistance += Entry.Dist;
			SampledRangeMin = FMath::Min(SampledRangeMin, Entry.Dist);
			SampledRangeMax = FMath::Max(SampledRangeMax, Entry.Dist);
		}

		if (Settings->WeightMethod == EPCGExRangeType::FullRange && RangeMax > 0)
		{
			SampledRangeMin = RangeMin;
			SampledRangeMax = RangeMax;
		}

		// Blend ops carry their own weight curve, so they get the raw inside-aware weight.
		for (const FSampleEntry& Entry : Samples)
		{
			const double W = Settings->InsideWeighting.GetWeight(Entry.Dist, Entry.bInside, SampledRangeMin, SampledRangeMax);
			Union->AddWeighted_Unsafe(Entry.Element, W * Settings->InsideWeighting.GetScale(Entry.bInside));
		}

		NumSampled = Samples.Num();
		WeightedDistance /= NumSampled;

		DataBlender->ComputeWeights(Index, Union, OutWeightedPoints);
		DataBlender->Blend(Index, OutWeightedPoints, Trackers);

		PCGEX_OUTPUT_VALUE(Distance, Index, WeightedDistance)
		PCGEX_OUTPUT_VALUE(NumInside, Index, NumInside)
		PCGEX_OUTPUT_VALUE(NumSamples, Index, NumSampled)

		bAnySuccess = true;
	}

	void FProcessor::SamplingFailed(const int32 Index)
	{
		if (NumSampled == 0 && Settings->OutputMode == EPCGExSampleInsidePathOutput::SuccessOnly)
		{
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
			return;
		}

		const double FailSafeDist = RangeMax;
		PCGEX_OUTPUT_VALUE(Distance, Index, FailSafeDist)
		PCGEX_OUTPUT_VALUE(NumInside, Index, -1)
		PCGEX_OUTPUT_VALUE(NumSamples, Index, 0)
	}

	void FProcessor::CompleteWork()
	{
		if (NumSampled == 0 && Settings->OutputMode == EPCGExSampleInsidePathOutput::SuccessOnly)
		{
			return;
		}

		// bResetWithFirstValue collapses an array buffer to its first element as the attribute's
		// default value (TArrayBuffer<T>::Write special path). It's a no-op on Data-domain buffers
		// (single-value), so gate by domain to keep intent explicit and avoid setting a flag that
		// never fires for half the buffers it touches.
		for (const TSharedPtr<PCGExData::IBuffer>& Buffer : PointDataFacade->Buffers)
		{
			if (Buffer->IsWritable() && Buffer->GetUnderlyingDomain() == PCGExData::EDomainType::Elements)
			{
				Buffer->bResetWithFirstValue = true;
			}
		}

		if (UnionBlendOpsManager)
		{
			UnionBlendOpsManager->Cleanup(Context);
		}

		PointDataFacade->WriteFastest(TaskManager);

		if (Settings->bTagIfHasSuccesses && bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasSuccessesTag);
		}
		if (Settings->bTagIfHasNoSuccesses && !bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasNoSuccessesTag);
		}

		if (NumSampled == 0 && Settings->OutputMode == EPCGExSampleInsidePathOutput::Split)
		{
			PointDataFacade->Source->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExSampleInsidePathContext, UPCGExSampleInsidePathSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
