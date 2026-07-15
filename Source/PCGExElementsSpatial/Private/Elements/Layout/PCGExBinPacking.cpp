// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Layout/PCGExBinPacking.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/Layout/PCGExLayout.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/PCGExMathBounds.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

#define LOCTEXT_NAMESPACE "PCGExBinPackingElement"
#define PCGEX_NAMESPACE BinPacking

#if WITH_EDITOR
void UPCGExBinPackingSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	// OccupationPadding was migrated FVector-triplet -> shorthand in-place (same member name) with no version
	// gate or deprecation, so the old FVector constant value cannot be recovered (releases shipped since; the
	// serialized tag type no longer matches). We only rewire the override-pin connections; the constant value
	// is intentionally not rescued per project decision.
	PCGEX_IF_VERSION_LOWER(1, 76, 9)
	{
		PCGEX_SHORTHAND_RENAME_PIN(OccupationPaddingAttribute, OccupationPadding, OccupationPadding)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

bool UPCGExBinPackingSettings::GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const
{
	OutRules.Append(PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules));
	return !OutRules.IsEmpty();
}

TArray<FPCGPinProperties> UPCGExBinPackingSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExLayout::SourceBinsLabel, "List of bins to fit input points into. Each input collection is expected to have a matching collection of bins.", Required)
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Normal);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBinPackingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExLayout::OutputBinsLabel, "Input bins, with added statistics.", Required)
	PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discarded points, one that could not fit into any bin.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BinPacking)

PCGExData::EIOInit UPCGExBinPackingSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(BinPacking)

bool FPCGExBinPackingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BinPacking)

	Context->Bins = MakeShared<PCGExData::FPointIOCollection>(InContext, PCGExLayout::SourceBinsLabel, PCGExData::EIOInit::NoInit);
	Context->Bins->OutputPin = PCGExLayout::OutputBinsLabel;

	int32 NumBins = Context->Bins->Num();
	int32 NumInputs = Context->MainPoints->Num();

	Context->ValidIOIndices.Reserve(FMath::Max(NumBins, NumInputs));

	if (NumBins != NumInputs)
	{
		if (NumBins > NumInputs)
		{
			NumBins = NumInputs;
			if (!Settings->bQuietTooManyBinsWarning)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("There are more bins than there are inputs. Extra bins will be ignored."));
			}

			for (int i = 0; i < NumInputs; i++)
			{
				Context->ValidIOIndices.Add(Context->MainPoints->Pairs[i]->IOIndex);
			}
		}
		else if (NumInputs > NumBins)
		{
			NumInputs = NumBins;
			if (!Settings->bQuietTooFewBinsWarning)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("There are more inputs than there are bins. Extra inputs will be ignored."));
			}

			for (int i = 0; i < NumBins; i++)
			{
				Context->ValidIOIndices.Add(Context->MainPoints->Pairs[i]->IOIndex);
			}
		}
	}
	else
	{
		for (int i = 0; i < NumInputs; i++)
		{
			Context->ValidIOIndices.Add(Context->MainPoints->Pairs[i]->IOIndex);
		}
	}

	for (int i = 0; i < NumBins; i++)
	{
		Context->Bins->Pairs[i]->OutputPin = Context->Bins->OutputPin;
	}

	Context->Discarded = MakeShared<PCGExData::FPointIOCollection>(InContext);
	Context->Discarded->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;

	return true;
}

bool FPCGExBinPackingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBinPackingElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BinPacking)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return Context->ValidIOIndices.Contains(Entry->IOIndex);
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				TArray<FPCGExSortRuleConfig> OutRules;
				Settings->GetSortingRules(Context, OutRules);
				NewBatch->bPrefetchData = !OutRules.IsEmpty();
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	Context->Bins->StageOutputs();
	Context->Discarded->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBinPacking
{
	void FBin::AddSpace(const FBox& InBox)
	{
		FSpace& NewSpace = Spaces.Emplace_GetRef(InBox, Seed);
		NewSpace.DistanceScore /= MaxDist;
	}

	FBin::FBin(const PCGExData::FConstPoint& InBinPoint, const FVector& InSeed, const TSharedPtr<FBinSplit>& InSplitter)
	{
		Splitter = InSplitter;
		Seed = InSeed;
		Bounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(InBinPoint);

		Transform = InBinPoint.GetTransform();
		Transform.SetScale3D(FVector::OneVector); // Reset scale for later transform

		MaxVolume = Bounds.GetVolume();
		FVector FurthestLocation = InSeed;

		for (int C = 0; C < 3; C++)
		{
			const double DistToMin = FMath::Abs(Seed[C] - Bounds.Min[C]);
			const double DistToMax = FMath::Abs(Seed[C] - Bounds.Max[C]);
			FurthestLocation[C] = (DistToMin > DistToMax) ? Bounds.Min[C] : Bounds.Max[C];
		}

		MaxDist = FVector::DistSquared(FurthestLocation, Seed);

		AddSpace(Bounds);
	}

	int32 FBin::GetBestSpaceScore(const FItem& InItem, double& OutScore, FRotator& OutRotator) const
	{
		int32 BestIndex = -1;
		const double BoxVolume = InItem.Box.GetVolume();
		const FVector ItemSize = InItem.Box.GetSize();

		for (int i = 0; i < Spaces.Num(); i++)
		{
			const FSpace& Space = Spaces[i];

			// TODO : Rotate & try fit

			if (Space.CanFit(ItemSize))
			{
				const double SpaceScore = 1 - ((Space.Volume - BoxVolume) / MaxVolume);
				const double DistScore = Space.DistanceScore;
				const double Score = SpaceScore + DistScore;

				if (Score < OutScore)
				{
					BestIndex = i;
					OutScore = Score;
				}
			}
		}

		return BestIndex;
	}

	void FBin::AddItem(int32 SpaceIndex, FItem& InItem)
	{
		Items.Add(InItem);

		const FSpace& Space = Spaces[SpaceIndex];

		const FVector ItemSize = InItem.Box.GetSize();
		FVector ItemMin = Space.Box.Min;

		for (int C = 0; C < 3; C++)
		{
			ItemMin[C] = FMath::Clamp(Seed[C] - ItemSize[C] * 0.5, Space.Box.Min[C], Space.Box.Max[C] - ItemSize[C]);
		}

		FBox ItemBox = FBox(ItemMin, ItemMin + ItemSize);
		InItem.Box = ItemBox;

		Space.Expand(ItemBox, InItem.Padding);

		if (Settings->bAvoidWastedSpace)
		{
			const FVector Amplitude = Space.Inflate(ItemBox, WastedSpaceThresholds);
		}


		TArray<FBox> NewPartitions;
		Splitter->SplitSpace(Space, ItemBox, NewPartitions);

		Spaces.RemoveAt(SpaceIndex);
		Spaces.Reserve(Spaces.Num() + NewPartitions.Num());

		for (const FBox& Partition : NewPartitions)
		{
			AddSpace(Partition);
		}
	}

	bool FBin::Insert(FItem& InItem)
	{
		FRotator OutRotation = FRotator::ZeroRotator;
		double OutScore = TNumericLimits<double>::Max();

		const int32 BestIndex = GetBestSpaceScore(InItem, OutScore, OutRotation);

		if (BestIndex == -1)
		{
			return false;
		}

		// TODO : Check other bins as well, while it fits, this one may not be the ideal candidate

		AddItem(BestIndex, InItem);

		return true;
	}

	void FBin::UpdatePoint(PCGExData::FMutablePoint& InPoint, const FItem& InItem) const
	{
		const FTransform T = FTransform(FQuat::Identity, InItem.Box.GetCenter() - InPoint.GetLocalBounds().GetCenter(), InPoint.GetScale3D());
		InPoint.SetTransform(T * Transform);
	}

	void FProcessor::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TProcessor::RegisterBuffersDependencies(FacadePreloader);

		TArray<FPCGExSortRuleConfig> RuleConfigs;
		if (Settings->GetSortingRules(ExecutionContext, RuleConfigs) && !RuleConfigs.IsEmpty())
		{
			Sorter = MakeShared<PCGExSorting::FSorter>(Context, PointDataFacade, RuleConfigs);
			Sorter->SortDirection = Settings->SortDirection;
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBinPacking::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		PointDataFacade->Source->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		TSharedPtr<PCGExData::FPointIO> TargetBins = Context->Bins->Pairs[BatchIndex];
		PCGEX_INIT_IO(TargetBins, PCGExData::EIOInit::Duplicate)

		PaddingBuffer = Settings->OccupationPadding.GetValueSetting();
		if (!PaddingBuffer->Init(PointDataFacade))
		{
			return false;
		}

#define PCGEX_SWITCH_ON_SPLIT_MODE(_DIRECTION)\
		switch (Settings->SplitMode){\
		case EPCGExSpaceSplitMode::Minimal: Splitter = MakeShared<TBinSplit<_DIRECTION, EPCGExSpaceSplitMode::Minimal>>(); break;\
		case EPCGExSpaceSplitMode::MinimalCross: Splitter = MakeShared<TBinSplit<_DIRECTION, EPCGExSpaceSplitMode::MinimalCross>>(); break;\
		case EPCGExSpaceSplitMode::EqualSplit: Splitter = MakeShared<TBinSplit<_DIRECTION, EPCGExSpaceSplitMode::EqualSplit>>(); break;\
		case EPCGExSpaceSplitMode::Cone: Splitter = MakeShared<TBinSplit<_DIRECTION, EPCGExSpaceSplitMode::Cone>>(); break;\
		case EPCGExSpaceSplitMode::ConeCross: Splitter = MakeShared<TBinSplit<_DIRECTION, EPCGExSpaceSplitMode::ConeCross>>(); break;\
		}

#define PCGEX_SWITCH_ON_SPLIT_DIRECTION(_MACRO) \
		switch (Settings->SplitAxis){ \
		case EPCGExAxis::Forward: _MACRO(EPCGExAxis::Forward) break; \
		case EPCGExAxis::Backward: _MACRO(EPCGExAxis::Backward) break; \
		case EPCGExAxis::Right: _MACRO(EPCGExAxis::Right) break; \
		case EPCGExAxis::Left: _MACRO(EPCGExAxis::Left) break; \
		case EPCGExAxis::Up: _MACRO(EPCGExAxis::Up) break; \
		case EPCGExAxis::Down: _MACRO(EPCGExAxis::Down) break; \
		}

		PCGEX_SWITCH_ON_SPLIT_DIRECTION(PCGEX_SWITCH_ON_SPLIT_MODE)

		Fitted.Init(false, PointDataFacade->GetNum());
		Bins.Reserve(TargetBins->GetNum());

		bool bRelativeSeed = Settings->SeedMode == EPCGExBinSeedMode::UVWConstant;
		TSharedPtr<PCGExData::TAttributeBroadcaster<FVector>> SeedGetter = MakeShared<PCGExData::TAttributeBroadcaster<FVector>>();
		if (Settings->SeedMode == EPCGExBinSeedMode::PositionAttribute)
		{
			if (!SeedGetter->Prepare(Settings->SeedPositionAttribute, TargetBins.ToSharedRef()))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(Context, Seed Position, Settings->SeedPositionAttribute)
				return false;
			}
		}
		else if (Settings->SeedMode == EPCGExBinSeedMode::UVWAttribute)
		{
			bRelativeSeed = true;
			if (!SeedGetter->Prepare(Settings->SeedUVWAttribute, TargetBins.ToSharedRef()))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(Context, Seed UVW, Settings->SeedUVWAttribute)
				return false;
			}
		}
		else
		{
			SeedGetter.Reset();
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		PCGExArrayHelpers::ArrayOfIndices(ProcessingOrder, PointDataFacade->GetNum());

		if (Sorter && Sorter->Init(Context))
		{
			if (TSharedPtr<PCGExSorting::FSortCache> Cache = Sorter->BuildCache(NumPoints))
			{
				ProcessingOrder.Sort([&](const int32 A, const int32 B)
				{
					return Cache->Compare(A, B);
				});
			}
			else
			{
				ProcessingOrder.Sort([&](const int32 A, const int32 B)
				{
					return Sorter->Sort(A, B);
				});
			}
		}

		if (Settings->bAvoidWastedSpace)
		{
			MinOccupation = TNumericLimits<double>::Max();
			const UPCGBasePointData* InPoints = PointDataFacade->GetIn();
			for (int i = 0; i < InPoints->GetNumPoints(); i++)
			{
				const FVector Size = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(PCGExData::FConstPoint(InPoints, i)).GetSize();
				MinOccupation = FMath::Min(MinOccupation, FMath::Min3(Size.X, Size.Y, Size.Z));
			}
		}

		for (int i = 0; i < TargetBins->GetNum(); i++)
		{
			const PCGExData::FConstPoint BinPoint = TargetBins->GetInPoint(i);

			FVector Seed = FVector::ZeroVector;
			if (bRelativeSeed)
			{
				FBox Box = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(BinPoint);
				Seed = Box.GetCenter() + (SeedGetter ? SeedGetter->FetchSingle(BinPoint, FVector::ZeroVector) : Settings->SeedUVW) * Box.GetExtent();
			}
			else
			{
				Seed = BinPoint.GetTransform().InverseTransformPositionNoScale(SeedGetter ? SeedGetter->FetchSingle(BinPoint, FVector::ZeroVector) : Settings->SeedPosition);
			}

			PCGEX_MAKE_SHARED(NewBin, FBin, BinPoint, Seed, Splitter)

			NewBin->Settings = Settings;
			NewBin->WastedSpaceThresholds = FVector(MinOccupation);

			Bins.Add(NewBin);
		}

		// OPTIM : Find the smallest bound dimension possible to use as min threshold for free spaces later on

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BinPacking::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 PointIndex = ProcessingOrder[Index];
			PCGExData::FMutablePoint Point(OutPointData, PointIndex);

			FItem Item = FItem();

			Item.Index = PointIndex;
			Item.Box = FBox(FVector::ZeroVector, PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point).GetSize());
			Item.Padding = PaddingBuffer->Read(PointIndex);

			bool bPlaced = false;
			for (const TSharedPtr<FBin>& Bin : Bins)
			{
				if (Bin->Insert(Item))
				{
					bPlaced = true;
					Bin->UpdatePoint(Point, Item);
					break;
				}
			}

			Fitted[PointIndex] = bPlaced;
			if (!bPlaced)
			{
				bHasUnfitted = true;
			}

			// TODO : post process pass to move things around based on initial placement
		}
	}

	void FProcessor::CompleteWork()
	{
		if (bHasUnfitted)
		{
			(void)PointDataFacade->Source->Gather(Fitted);

			if (const TSharedPtr<PCGExData::FPointIO> Discarded = Context->Discarded->Emplace_GetRef(PointDataFacade->GetIn(), PCGExData::EIOInit::New))
			{
				(void)Discarded->InheritPoints(Fitted, true);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
