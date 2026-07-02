// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExExtrudeTensors.h"

#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

#define LOCTEXT_NAMESPACE "PCGExExtrudeTensorsElement"
#define PCGEX_NAMESPACE ExtrudeTensors

PCGEX_SETTING_VALUE_IMPL(UPCGExExtrudeTensorsSettings, MaxLength, double, MaxLengthInput, MaxLengthAttribute, MaxLength)
PCGEX_SETTING_VALUE_IMPL(UPCGExExtrudeTensorsSettings, MaxPointsCount, int32, MaxPointsCountInput, MaxPointsCountAttribute, MaxPointsCount)
PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExExtrudeTensorsSettings, Iterations, int32, bUsePerPointMaxIterations, IterationsAttribute, Iterations)

//
// Node-specific config initialization
//

namespace PCGExExtrudeTensors
{
	void InitExtrusionConfigFromSettings(
		FExtrusionConfig& OutConfig,
		const FPCGExExtrudeTensorsContext* InContext,
		const UPCGExExtrudeTensorsSettings* InSettings,
		bool bHasStopFilters)
	{
		// Transform settings
		OutConfig.bTransformRotation = InSettings->bTransformRotation;
		OutConfig.RotationMode = InSettings->Rotation;
		OutConfig.AlignAxis = InSettings->AlignAxis;

		// Limits
		OutConfig.FuseDistance = InSettings->FuseDistance;
		OutConfig.FuseDistanceSquared = OutConfig.FuseDistance * OutConfig.FuseDistance;
		OutConfig.StopHandling = InSettings->StopConditionHandling;
		OutConfig.bAllowChildExtrusions = InSettings->bAllowChildExtrusions;

		// External intersection
		OutConfig.bDoExternalIntersections = InSettings->bDoExternalPathIntersections;
		OutConfig.bIgnoreIntersectionOnOrigin = InSettings->bIgnoreIntersectionOnOrigin;

		// Self intersection
		OutConfig.bDoSelfIntersections = InSettings->bDoSelfPathIntersections;
		OutConfig.bMergeOnProximity = InSettings->bMergeOnProximity;
		OutConfig.ProximitySegmentBalance = InSettings->ProximitySegmentBalance;

		// Closed loop detection
		OutConfig.bDetectClosedLoops = InSettings->bDetectClosedLoops;
		OutConfig.ClosedLoopSquaredDistance = FMath::Square(InSettings->ClosedLoopSearchDistance);
		OutConfig.ClosedLoopSearchDot = PCGExMath::DegreesToDot(InSettings->ClosedLoopSearchAngle);

		// Copy intersection details
		OutConfig.ExternalPathIntersections = InSettings->ExternalPathIntersections;
		OutConfig.SelfPathIntersections = InSettings->SelfPathIntersections;
		OutConfig.MergeDetails = InSettings->MergeDetails;

		// Initialize intersection details
		OutConfig.InitIntersectionDetails();

		// Compute flags
		OutConfig.ComputeFlags(bHasStopFilters, !InContext->ExternalPaths.IsEmpty());
	}
}

//
// Settings Implementation
//

TArray<FPCGPinProperties> UPCGExExtrudeTensorsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExTensor::SourceTensorsLabel, "Tensors", Required, FPCGExDataTypeInfoTensor::AsId())
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceStopConditionLabel, "Extruded points will be tested against those filters. If a filter returns true, the extrusion point is considered 'out-of-bounds'.", Normal)

	if (bDoExternalPathIntersections)
	{
		PCGEX_PIN_POINTS(PCGExPaths::Labels::SourcePathsLabel, "Paths that will be checked for intersections while extruding.", Normal)
	}
	else
	{
		PCGEX_PIN_POINTS(PCGExPaths::Labels::SourcePathsLabel, "(This is only there to preserve connections, enable it in the settings.)", Advanced)
	}

	PCGExSorting::DeclareSortingRulesInputs(PinProperties, bDoSelfPathIntersections ? EPCGPinStatus::Normal : EPCGPinStatus::Advanced);

	return PinProperties;
}

bool UPCGExExtrudeTensorsSettings::GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const
{
	OutRules.Append(PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules));
	return !OutRules.IsEmpty();
}

PCGEX_INITIALIZE_ELEMENT(ExtrudeTensors)
PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(ExtrudeTensors)

FName UPCGExExtrudeTensorsSettings::GetMainInputPin() const
{
	return PCGExCommon::Labels::SourceSeedsLabel;
}

FName UPCGExExtrudeTensorsSettings::GetMainOutputPin() const
{
	return PCGExPaths::Labels::OutputPathsLabel;
}

//
// Element Implementation
//

bool FPCGExExtrudeTensorsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ExtrudeTensors)

	if (!PCGExFactories::GetInputFactories(InContext, PCGExTensor::SourceTensorsLabel, Context->TensorFactories, {FPCGExDataTypeInfoTensor::AsId()}))
	{
		return false;
	}

	PCGExFactories::GetInputFactories(Context, PCGExFilters::Labels::SourceStopConditionLabel, Context->StopFilterFactories, PCGExFactories::PointFilters(), false);
	PCGExPointFilter::PruneForDirectEvaluation(Context, Context->StopFilterFactories);

	if (Context->TensorFactories.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing tensors."))
		return false;
	}

	return true;
}

bool FPCGExExtrudeTensorsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExExtrudeTensorsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ExtrudeTensors)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->AddConsumableAttributeName(Settings->IterationsAttribute);

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bPrefetchData = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to subdivide."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

//
// FProcessor Implementation
//

namespace PCGExExtrudeTensors
{
	FProcessor::~FProcessor()
	{
	}

	void FProcessor::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TProcessor<FPCGExExtrudeTensorsContext, UPCGExExtrudeTensorsSettings>::RegisterBuffersDependencies(FacadePreloader);

		TArray<FPCGExSortRuleConfig> RuleConfigs;
		if (Settings->GetSortingRules(ExecutionContext, RuleConfigs) && !RuleConfigs.IsEmpty())
		{
			Sorter = MakeShared<PCGExSorting::FSorter>(Context, PointDataFacade, RuleConfigs);
			Sorter->SortDirection = Settings->SortDirection;
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExExtrudeTensors::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Sorter && !Sorter->Init(Context))
		{
			Sorter.Reset();
		}

		StaticPaths = MakeShared<TArray<TSharedPtr<PCGExPaths::FPath>>>();

		// Initialize stop filters if present
		if (!Context->StopFilterFactories.IsEmpty())
		{
			StopFilters = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!StopFilters->Init(Context, Context->StopFilterFactories))
			{
				StopFilters.Reset();
			}
		}

		// Initialize config using node-specific settings
		InitExtrusionConfigFromSettings(Context->ExtrusionConfig, Context, Settings, StopFilters.IsValid());

		// Initialize tensor handler
		TensorsHandler = MakeShared<PCGExTensor::FTensorsHandler>(Settings->TensorHandlerDetails);
		if (!TensorsHandler->Init(Context, Context->TensorFactories, PointDataFacade))
		{
			return false;
		}

		AttributesToPathTags = Settings->AttributesToPathTags;
		if (!AttributesToPathTags.Init(Context, PointDataFacade))
		{
			return false;
		}

		// Initialize per-point settings
		PerPointIterations = Settings->GetValueSettingIterations();
		if (!PerPointIterations->Init(PointDataFacade, false, true))
		{
			return false;
		}
		if (!PerPointIterations->IsConstant())
		{
			if (Settings->bUseMaxFromPoints)
			{
				RemainingIterations = FMath::Max(RemainingIterations, PerPointIterations->Max());
			}
		}
		else
		{
			RemainingIterations = Settings->Iterations;
		}

		if (Settings->bUseMaxLength)
		{
			MaxLength = Settings->GetValueSettingMaxLength();
			if (!MaxLength->Init(PointDataFacade, false))
			{
				return false;
			}
		}

		if (Settings->bUseMaxPointsCount)
		{
			MaxPointsCount = Settings->GetValueSettingMaxPointsCount();
			if (!MaxPointsCount->Init(PointDataFacade, false))
			{
				return false;
			}
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		PCGExArrayHelpers::InitArray(ExtrusionQueue, NumPoints);
		PointFilterCache.Init(true, NumPoints);

		Context->MainPoints->IncreaseReserve(NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::SetupExtrusionCallbacks(const TSharedPtr<FExtrusion>& Extrusion)
	{
		// Callback for creating child extrusions
		Extrusion->Callbacks.OnCreateChild = [this](const TSharedRef<FExtrusion>& Parent) -> TSharedPtr<FExtrusion>
		{
			return InitExtrusionFromExtrusion(Parent);
		};

		// Callback for applying tags based on stop reason
		Extrusion->Callbacks.OnApplyTags = [this](FExtrusion& E, PCGExData::FPointIO& Source)
		{
			if (Settings->bTagIfIsStoppedByFilters && E.HasStopReason(EStopReason::StopFilter))
			{
				Source.Tags->AddRaw(Settings->IsStoppedByFiltersTag);
			}

			if (Settings->bTagIfIsStoppedByIntersection && (E.HasStopReason(EStopReason::ExternalPath) || E.HasStopReason(EStopReason::SelfIntersection)))
			{
				Source.Tags->AddRaw(Settings->IsStoppedByIntersectionTag);
			}

			if (Settings->bTagIfIsStoppedBySelfIntersection && E.HasStopReason(EStopReason::SelfIntersection))
			{
				Source.Tags->AddRaw(Settings->IsStoppedBySelfIntersectionTag);
			}

			if (Settings->bTagIfSelfMerged && E.HasStopReason(EStopReason::SelfMerge))
			{
				Source.Tags->AddRaw(Settings->IsSelfMergedTag);
			}

			if (Settings->bTagIfChildExtrusion && E.bIsChildExtrusion)
			{
				Source.Tags->AddRaw(Settings->IsChildExtrusionTag);
			}

			if (Settings->bTagIfIsFollowUp && E.bIsFollowUp)
			{
				Source.Tags->AddRaw(Settings->IsFollowUpTag);
			}
		};

		// Callback for validating path point count
		Extrusion->Callbacks.OnValidatePath = [this](int32 PointCount) -> bool
		{
			return Settings->PathOutputDetails.Validate(PointCount);
		};
	}

	void FProcessor::InitExtrusionFromSeed(const int32 InSeedIndex)
	{
		const int32 Iterations = PerPointIterations->Read(InSeedIndex);
		if (Iterations < 1)
		{
			return;
		}

		bool bIsStopped = false;
		if (StopFilters)
		{
			const PCGExData::FProxyPoint ProxyPoint(PointDataFacade->Source->GetInPoint(InSeedIndex));
			bIsStopped = StopFilters->Test(ProxyPoint);
			if (Settings->bIgnoreStoppedSeeds && bIsStopped)
			{
				return;
			}
		}

		TSharedPtr<FExtrusion> NewExtrusion = CreateExtrusion(InSeedIndex, Iterations);
		if (!NewExtrusion)
		{
			return;
		}

		// If starting stopped, stay in probing state
		if (bIsStopped)
		{
			NewExtrusion->State = EExtrusionState::Probing;
		}

		if (Settings->bUseMaxLength)
		{
			NewExtrusion->MaxLength = MaxLength->Read(InSeedIndex);
		}
		if (Settings->bUseMaxPointsCount)
		{
			NewExtrusion->MaxPointCount = MaxPointsCount->Read(InSeedIndex);
		}

		ExtrusionQueue[InSeedIndex] = NewExtrusion;
	}

	TSharedPtr<FExtrusion> FProcessor::InitExtrusionFromExtrusion(const TSharedRef<FExtrusion>& InExtrusion)
	{
		if (!Settings->bAllowChildExtrusions)
		{
			return nullptr;
		}

		TSharedPtr<FExtrusion> NewExtrusion = CreateExtrusion(InExtrusion->SeedIndex, InExtrusion->RemainingIterations);
		if (!NewExtrusion)
		{
			return nullptr;
		}

		NewExtrusion->SetHead(InExtrusion->Head);
		NewExtrusion->ParentExtrusion = InExtrusion;

		{
			FWriteScopeLock WriteScopeLock(NewExtrusionLock);
			NewExtrusions.Add(NewExtrusion);
		}

		return NewExtrusion;
	}

	TSharedPtr<FExtrusion> FProcessor::CreateExtrusion(const int32 InSeedIndex, const int32 InMaxIterations)
	{
		TSharedPtr<PCGExData::FPointIO> NewIO = Context->MainPoints->Emplace_GetRef(PointDataFacade->Source->GetIn(), PCGExData::EIOInit::NoInit);
		if (!NewIO)
		{
			return nullptr;
		}

		PCGEX_MAKE_SHARED(Facade, PCGExData::FFacade, NewIO.ToSharedRef());
		if (!Facade->Source->InitializeOutput(PCGExData::EIOInit::New))
		{
			return nullptr;
		}

		TSharedPtr<FExtrusion> NewExtrusion = MakeShared<FExtrusion>(InSeedIndex, Facade.ToSharedRef(), InMaxIterations, Context->ExtrusionConfig);

		if (Settings->bUseMaxLength)
		{
			NewExtrusion->MaxLength = MaxLength->Read(InSeedIndex);
		}
		if (Settings->bUseMaxPointsCount)
		{
			NewExtrusion->MaxPointCount = MaxPointsCount->Read(InSeedIndex);
		}

		NewExtrusion->PointDataFacade->Source->IOIndex = BatchIndex * 1000000 + InSeedIndex;
		AttributesToPathTags.Tag(PointDataFacade->GetInPoint(InSeedIndex), Facade->Source);

		// Set up shared resources
		NewExtrusion->TensorsHandler = TensorsHandler;
		NewExtrusion->StopFilters = StopFilters;
		NewExtrusion->SolidPaths = StaticPaths;
		NewExtrusion->ExternalPaths = &Context->ExternalPaths;

		// Set up callbacks for decoupled communication
		SetupExtrusionCallbacks(NewExtrusion);

		return NewExtrusion;
	}

	void FProcessor::SortQueue()
	{
		switch (Settings->SelfIntersectionMode)
		{
		case EPCGExSelfIntersectionMode::PathLength:
			if (Sorter)
			{
				if (Settings->SortDirection == EPCGExSortDirection::Ascending)
				{
					ExtrusionQueue.Sort([S = Sorter](const TSharedPtr<FExtrusion>& EA, const TSharedPtr<FExtrusion>& EB)
					{
						if (EA->Metrics.Length == EB->Metrics.Length)
						{
							return S->Sort(EA->SeedIndex, EB->SeedIndex);
						}
						return EA->Metrics.Length > EB->Metrics.Length;
					});
				}
				else
				{
					ExtrusionQueue.Sort([S = Sorter](const TSharedPtr<FExtrusion>& EA, const TSharedPtr<FExtrusion>& EB)
					{
						if (EA->Metrics.Length == EB->Metrics.Length)
						{
							return S->Sort(EA->SeedIndex, EB->SeedIndex);
						}
						return EA->Metrics.Length < EB->Metrics.Length;
					});
				}
			}
			else
			{
				ExtrusionQueue.Sort([](const TSharedPtr<FExtrusion>& EA, const TSharedPtr<FExtrusion>& EB)
				{
					return EA->Metrics.Length > EB->Metrics.Length;
				});
			}
			break;

		case EPCGExSelfIntersectionMode::SortingOnly:
			if (Sorter)
			{
				ExtrusionQueue.Sort([S = Sorter](const TSharedPtr<FExtrusion>& EA, const TSharedPtr<FExtrusion>& EB)
				{
					return S->Sort(EA->SeedIndex, EB->SeedIndex);
				});
			}
			break;
		}
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		CompletedExtrusions = MakeShared<PCGExMT::TScopedArray<TSharedPtr<FExtrusion>>>(Loops);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::ExtrudeTensors::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		PCGEX_SCOPE_LOOP(Index)
		{
			InitExtrusionFromSeed(Index);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// Drive the extrusion as a flat while-loop instead of recursing through
		// StartParallelLoopForRange / OnRangeProcessingComplete each iteration.
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExExtrudeTensors::ExtrusionLoop);

		while (UpdateExtrusionQueue())
		{
			const int32 NumQueued = ExtrusionQueue.Num();

			TArray<PCGExMT::FScope> Loops;
			const int32 NumScopes = PCGExMT::SubLoopScopes(
				Loops, NumQueued, FMath::Max(1, PCGExMT::GetSanitizedBatchSize(NumQueued, 32)));

			PrepareLoopScopesForRanges(Loops);

			if (NumScopes == 1)
			{
				ProcessRange(Loops[0]);
			}
			else
			{
				PCGExMT::ParallelOrSequential(
					NumScopes,
					[this, &Loops](const int32 i) { ProcessRange(Loops[i]); },
					/*Threshold=*/2, EParallelForFlags::Unbalanced);
			}

			RemainingIterations--;
			ProcessSelfIntersectionsAndCutoffs();
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			if (TSharedPtr<FExtrusion> Extrusion = ExtrusionQueue[Index];
				Extrusion && !Extrusion->Advance())
			{
				Extrusion->Complete();
				CompletedExtrusions->Get(Scope)->Add(Extrusion);
			}
		}
	}

	void FProcessor::ProcessSelfIntersectionsAndCutoffs()
	{
		if (Settings->bDoSelfPathIntersections)
		{
			const bool bMergeFirst = Settings->SelfIntersectionPriority == EPCGExSelfIntersectionPriority::Merge;
			const int32 NumQueuedExtrusions = ExtrusionQueue.Num();

			SortQueue();

			for (int32 i = 0; i < NumQueuedExtrusions; i++)
			{
				TSharedPtr<FExtrusion> E = ExtrusionQueue[i];

				if (E->AdvancedOnly() || !E->bIsExtruding)
				{
					continue;
				}

				const PCGExMath::FSegment HeadSegment = E->GetHeadSegment();
				PCGExMath::FClosestPosition Crossing(HeadSegment.A);
				PCGExMath::FClosestPosition Merge(HeadSegment.Lerp(Settings->ProximitySegmentBalance));
				PCGExMath::FClosestPosition PreMerge(Merge.Origin);

				for (int32 j = 0; j < ExtrusionQueue.Num(); j++)
				{
					const TSharedPtr<FExtrusion> OE = ExtrusionQueue[j];
					if (!OE->bIsExtruding)
					{
						continue;
					}
					if (!OE->Bounds.Intersect(HeadSegment.Bounds))
					{
						continue;
					}

					const int32 TruncateSearch = i == j ? 2 : 0;
					bool bIsLastSegment = false;
					if (j > i)
					{
						if (PCGExMath::FClosestPosition LocalCrossing = OE->FindCrossing(HeadSegment, bIsLastSegment, PreMerge, TruncateSearch))
						{
							if (bIsLastSegment)
							{
								// Lower priority path
								// Cut will happen the other way around
								continue;
							}

							Merge.Update(PreMerge);
							Crossing.Update(LocalCrossing, j);
						}
					}
					else
					{
						if (PCGExMath::FClosestPosition LocalCrossing = OE->FindCrossing(HeadSegment, bIsLastSegment, PreMerge, TruncateSearch))
						{
							// Crossing found
							if (bMergeFirst)
							{
								Merge.Update(PreMerge);
							}
							Crossing.Update(LocalCrossing, j);
						}
						else
						{
							// Update merge instead
							Merge.Update(PreMerge);
						}
					}
				}

				if (bMergeFirst)
				{
					if (E->TryMerge(HeadSegment, Merge))
					{
						E->CutOff(Merge);
						CompletedExtrusions->Arrays[0]->Add(E);
					}
					else if (Crossing)
					{
						E->CutOff(Crossing);
						CompletedExtrusions->Arrays[0]->Add(E);
					}
				}
				else
				{
					if (Crossing)
					{
						E->CutOff(Crossing);
						CompletedExtrusions->Arrays[0]->Add(E);
					}
					else if (E->TryMerge(HeadSegment, Merge))
					{
						E->CutOff(Merge);
						CompletedExtrusions->Arrays[0]->Add(E);
					}
				}
			}
		}
	}

	bool FProcessor::UpdateExtrusionQueue()
	{
		if (RemainingIterations <= 0)
		{
			return false;
		}

		int32 WriteIndex = 0;
		for (int32 i = 0; i < ExtrusionQueue.Num(); i++)
		{
			if (TSharedPtr<FExtrusion> E = ExtrusionQueue[i];
				E && E->IsActive())
			{
				ExtrusionQueue[WriteIndex++] = E;
			}
		}

		ExtrusionQueue.SetNum(WriteIndex);

		if (!NewExtrusions.IsEmpty())
		{
			ExtrusionQueue.Reserve(ExtrusionQueue.Num() + NewExtrusions.Num());
			ExtrusionQueue.Append(NewExtrusions);
			NewExtrusions.Reset();
		}

		if (ExtrusionQueue.IsEmpty())
		{
			return false;
		}

		// Convert completed paths to static collision constraints
		if (Settings->bDoSelfPathIntersections && CompletedExtrusions)
		{
			CompletedExtrusions->ForEach([&](TArray<TSharedPtr<FExtrusion>>& Completed)
			{
				StaticPaths.Get()->Reserve(StaticPaths.Get()->Num() + Completed.Num());
				for (const TSharedPtr<FExtrusion>& E : Completed)
				{
					E->Cleanup();

					if (!E->IsValidPath())
					{
						continue;
					}

					TSharedPtr<PCGExPaths::FPath> StaticPath = MakeShared<PCGExPaths::FPath>(E->PointDataFacade->GetOut(), Settings->ExternalPathIntersections.Tolerance);
					StaticPath->BuildEdgeOctree();
					StaticPaths.Get()->Add(StaticPath);
				}
			});

			CompletedExtrusions.Reset();
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
		for (const TSharedPtr<FExtrusion>& E : ExtrusionQueue)
		{
			if (E)
			{
				E->Complete();
			}
		}
		CompletedExtrusions.Reset();
		ExtrusionQueue.Empty();
		StaticPaths->Empty();
	}

	//
	// FBatch Implementation
	//

	FBatch::FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
		: TBatch<FProcessor>(InContext, InPointsCollection)
	{
	}

	void FBatch::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ExtrudeTensors)

		if (Settings->bDoExternalPathIntersections)
		{
			// Gather external paths synchronously
			TSharedPtr<PCGExData::FPointIOCollection> PathCollection = MakeShared<PCGExData::FPointIOCollection>(Context, PCGExPaths::Labels::SourcePathsLabel, PCGExData::EIOInit::NoInit, false);

			if (!PathCollection->Pairs.IsEmpty())
			{
				Context->ExternalPaths.Reserve(PathCollection->Pairs.Num());
				for (const TSharedPtr<PCGExData::FPointIO>& PathIO : PathCollection->Pairs)
				{
					if (TSharedPtr<PCGExPaths::FPath> Path = PCGExPaths::Helpers::MakePath(PathIO->GetIn(), Settings->ExternalPathIntersections.Tolerance))
					{
						Path->BuildEdgeOctree();
						Context->ExternalPaths.Add(Path);
					}
				}
			}
		}

		TBatch<FProcessor>::Process(InTaskManager);
	}

	void FBatch::OnPathsPrepared()
	{
		TBatch<FProcessor>::Process(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
