// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBevelPath.h"

#include "Algo/BinarySearch.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathProfile.h"


#define LOCTEXT_NAMESPACE "PCGExBevelPathElement"
#define PCGEX_NAMESPACE BevelPath

PCGEX_SETTING_VALUE_IMPL(UPCGExBevelPathSettings, Subdivisions, double, SubdivisionAmountInput, SubdivisionAmount, SubdivideMethod == EPCGExSubdivideMode::Count ? SubdivisionCount : SubdivisionDistance)

#if WITH_EDITOR
void UPCGExBevelPathSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		// Rewire Width
		PCGEX_SHORTHAND_RENAME_PIN(WidthAttribute, WidthConstant, Width)
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		ManhattanDetails.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExBevelPathSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 8)
	{
		Width.Update(WidthInput_DEPRECATED, WidthAttribute_DEPRECATED, WidthConstant_DEPRECATED);
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		ManhattanDetails.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExBevelPathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (Type == EPCGExBevelProfileType::Custom)
	{
		PCGEX_PIN_POINT(PCGExBevelPath::SourceCustomProfile, "Single path used as bevel profile", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BevelPath)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BevelPath)

void UPCGExBevelPathSettings::InitOutputFlags(const TSharedPtr<PCGExData::FPointIO>& InPointIO) const
{
	if (bFlagPoles)
	{
		InPointIO->FindOrCreateAttribute(PoleFlagName, false);
	}
	if (bFlagStartPoint)
	{
		InPointIO->FindOrCreateAttribute(StartPointFlagName, false);
	}
	if (bFlagEndPoint)
	{
		InPointIO->FindOrCreateAttribute(EndPointFlagName, false);
	}
	if (bFlagSubdivision)
	{
		InPointIO->FindOrCreateAttribute(SubdivisionFlagName, false);
	}
}

bool FPCGExBevelPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BevelPath)

	if (Settings->bFlagPoles)
	{
		PCGEX_VALIDATE_NAME(Settings->PoleFlagName)
	}
	if (Settings->bFlagStartPoint)
	{
		PCGEX_VALIDATE_NAME(Settings->StartPointFlagName)
	}
	if (Settings->bFlagEndPoint)
	{
		PCGEX_VALIDATE_NAME(Settings->EndPointFlagName)
	}
	if (Settings->bFlagSubdivision)
	{
		PCGEX_VALIDATE_NAME(Settings->SubdivisionFlagName)
	}

	if (Settings->Type == EPCGExBevelProfileType::Custom &&
		!PCGExPaths::Profile::TryBuildCustomProfile(Context, PCGExBevelPath::SourceCustomProfile, Context->CustomProfileFacade, Context->CustomProfilePositions))
	{
		return false;
	}

	return true;
}

bool FPCGExBevelPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBevelPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BevelPath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 3 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				PCGEX_SKIP_INVALID_PATH_ENTRY

				if (Entry->GetNum() < 3)
				{
					Entry->InitializeOutput(PCGExData::EIOInit::Duplicate);
					Settings->InitOutputFlags(Entry);
					bHasInvalidInputs = true;
					return false;
				}

				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = (Settings->bFlagPoles || Settings->bFlagSubdivision || Settings->bFlagEndPoint || Settings->bFlagStartPoint);
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to Bevel."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExBevelPath
{
	FBevel::FBevel(const int32 InIndex, const FProcessor* InProcessor)
		: Index(InIndex)
	{
		const UPCGBasePointData* InPoints = InProcessor->PointDataFacade->GetIn();
		TConstPCGValueRange<FTransform> InTransforms = InPoints->GetConstTransformValueRange();

		const int32 PointCount = InTransforms.Num();
		ArriveIdx = InProcessor->WrapIndex(Index - 1);
		LeaveIdx = InProcessor->WrapIndex(Index + 1);

		// Handle open paths - should not happen as endpoints are filtered, but safety check
		if (ArriveIdx < 0)
		{
			ArriveIdx = 0;
		}
		if (LeaveIdx < 0)
		{
			LeaveIdx = PointCount - 1;
		}

		Corner = InTransforms[InIndex].GetLocation();

		const FVector PrevLocation = InTransforms[ArriveIdx].GetLocation();
		const FVector NextLocation = InTransforms[LeaveIdx].GetLocation();

		ArriveDir = (PrevLocation - Corner).GetSafeNormal();
		LeaveDir = (NextLocation - Corner).GetSafeNormal();

		// Get initial width from attribute or constant
		InitialWidth = InProcessor->WidthGetter->Read(Index);
		Width = InitialWidth;

		// Get edge lengths from cache
		const double ArriveLen = InProcessor->PathLength->Get(ArriveIdx);
		const double LeaveLen = InProcessor->PathLength->Get(Index);
		const double SmallestLength = FMath::Min(ArriveLen, LeaveLen);

		// Initialize sliding limits to immediate neighbors (will be updated if sliding enabled)
		ArriveSlidingLimit = ArriveLen;
		LeaveSlidingLimit = LeaveLen;

		// Apply width measure (relative vs absolute)
		if (InProcessor->Settings->WidthMeasure == EPCGExMeanMeasure::Relative)
		{
			Width *= SmallestLength;
		}

		// Apply radius mode conversion
		if (InProcessor->Settings->Mode == EPCGExBevelMode::Radius)
		{
			const double DotProduct = FMath::Clamp(FVector::DotProduct(ArriveDir, LeaveDir), -1.0, 1.0);
			const double HalfAngle = FMath::Acos(DotProduct) / 2.0;
			const double SinHalfAngle = FMath::Sin(HalfAngle);
			if (!FMath::IsNearlyZero(SinHalfAngle))
			{
				Width = Width / SinHalfAngle;
			}
		}

		// Apply basic limiting (ClosestNeighbor) - will be refined in Balance() if Balanced mode
		// Skip this limit if sliding is enabled - will be applied after ComputeSlidingLimits
		if (InProcessor->Settings->Limit != EPCGExBevelLimit::None && !InProcessor->bSlideAlongPath)
		{
			Width = FMath::Min(Width, SmallestLength);
		}

		// Compute alpha values for balancing
		ArriveAlpha = (ArriveLen > KINDA_SMALL_NUMBER) ? Width / ArriveLen : 1.0;
		LeaveAlpha = (LeaveLen > KINDA_SMALL_NUMBER) ? Width / LeaveLen : 1.0;
	}

	void FBevel::ComputeSlidingLimits(const FProcessor* InProcessor, const TConstPCGValueRange<FTransform>& InTransforms)
	{
		// Walk backwards to find limiting bevel or path end
		ArriveSlidingLimit = 0.0;
		ArriveBevelIdx = -1;
		int32 CurrentIdx = Index;

		ArrivePathPoints.Reset();
		ArrivePathDistances.Reset();
		ArrivePathIndices.Reset();
		ArrivePathPoints.Add(Corner);
		ArrivePathDistances.Add(0.0);

		for (int32 i = 0; i < InProcessor->NumPoints; ++i)
		{
			const int32 PrevIdx = InProcessor->WrapIndex(CurrentIdx - 1);
			if (PrevIdx < 0)
			{
				// Hit path start
				break;
			}

			// Add edge length (edge from PrevIdx to CurrentIdx)
			ArriveSlidingLimit += InProcessor->PathLength->Get(PrevIdx);

			ArrivePathPoints.Add(InTransforms[PrevIdx].GetLocation());
			ArrivePathDistances.Add(ArriveSlidingLimit);
			ArrivePathIndices.Add(PrevIdx);

			// Check if previous point has a bevel
			if (InProcessor->Bevels[PrevIdx])
			{
				ArriveBevelIdx = PrevIdx;
				break;
			}

			CurrentIdx = PrevIdx;

			// For closed loops, stop if we've come back around
			if (CurrentIdx == Index)
			{
				break;
			}
		}

		// Walk forwards to find limiting bevel or path end
		LeaveSlidingLimit = 0.0;
		LeaveBevelIdx = -1;
		CurrentIdx = Index;

		LeavePathPoints.Reset();
		LeavePathDistances.Reset();
		LeavePathIndices.Reset();
		LeavePathPoints.Add(Corner);
		LeavePathDistances.Add(0.0);

		for (int32 i = 0; i < InProcessor->NumPoints; ++i)
		{
			const int32 NextIdx = InProcessor->WrapIndex(CurrentIdx + 1);
			if (NextIdx < 0)
			{
				// Hit path end
				break;
			}

			// Add edge length (edge from CurrentIdx to NextIdx)
			LeaveSlidingLimit += InProcessor->PathLength->Get(CurrentIdx);

			LeavePathPoints.Add(InTransforms[NextIdx].GetLocation());
			LeavePathDistances.Add(LeaveSlidingLimit);
			LeavePathIndices.Add(NextIdx);

			// Check if next point has a bevel
			if (InProcessor->Bevels[NextIdx])
			{
				LeaveBevelIdx = NextIdx;
				break;
			}

			CurrentIdx = NextIdx;

			// For closed loops, stop if we've come back around
			if (CurrentIdx == Index)
			{
				break;
			}
		}
	}

	FVector FBevel::GetPositionAlongPath(const TArray<FVector>& PathPoints, const TArray<double>& PathDistances, double Distance) const
	{
		if (PathPoints.Num() < 2 || Distance <= 0.0)
		{
			return PathPoints.Num() > 0 ? PathPoints[0] : Corner;
		}

		// Distance exceeds path length - return last point
		if (Distance >= PathDistances.Last())
		{
			return PathPoints.Last();
		}

		// Find the segment containing our distance. Distances are cumulative, hence sorted.
		const int32 SegmentEndIdx = Algo::UpperBound(PathDistances, Distance);

		const double SegmentStart = PathDistances[SegmentEndIdx - 1];
		const double SegmentEnd = PathDistances[SegmentEndIdx];
		const double SegmentLength = SegmentEnd - SegmentStart;

		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			return PathPoints[SegmentEndIdx];
		}

		const double Alpha = (Distance - SegmentStart) / SegmentLength;
		return FMath::Lerp(PathPoints[SegmentEndIdx - 1], PathPoints[SegmentEndIdx], Alpha);
	}

	void FBevel::Balance(const FProcessor* InProcessor)
	{
		double EffectiveArriveLimit = ArriveSlidingLimit;
		double EffectiveLeaveLimit = LeaveSlidingLimit;

		// When sliding, we compete with neighboring bevels for the shared path distance
		if (InProcessor->bSlideAlongPath)
		{
			// Calculate our proportion of the available space on arrive side
			if (ArriveBevelIdx >= 0)
			{
				const TSharedPtr<FBevel>& ArriveBevel = InProcessor->Bevels[ArriveBevelIdx];
				if (ArriveBevel)
				{
					// Both bevels want part of this path segment
					// Split proportionally based on their initial widths
					const double TotalWidth = InitialWidth + ArriveBevel->InitialWidth;
					if (TotalWidth > KINDA_SMALL_NUMBER)
					{
						const double MyProportion = InitialWidth / TotalWidth;
						EffectiveArriveLimit = ArriveSlidingLimit * MyProportion;
					}
					else
					{
						EffectiveArriveLimit = ArriveSlidingLimit * 0.5;
					}
				}
			}

			// Calculate our proportion of the available space on leave side
			if (LeaveBevelIdx >= 0)
			{
				const TSharedPtr<FBevel>& LeaveBevel = InProcessor->Bevels[LeaveBevelIdx];
				if (LeaveBevel)
				{
					const double TotalWidth = InitialWidth + LeaveBevel->InitialWidth;
					if (TotalWidth > KINDA_SMALL_NUMBER)
					{
						const double MyProportion = InitialWidth / TotalWidth;
						EffectiveLeaveLimit = LeaveSlidingLimit * MyProportion;
					}
					else
					{
						EffectiveLeaveLimit = LeaveSlidingLimit * 0.5;
					}
				}
			}
		}
		else
		{
			// Original balance logic for non-sliding mode
			const TSharedPtr<FBevel>& PrevBevel = InProcessor->Bevels[ArriveIdx];
			const TSharedPtr<FBevel>& NextBevel = InProcessor->Bevels[LeaveIdx];

			const double ArriveLen = InProcessor->PathLength->Get(ArriveIdx);
			const double LeaveLen = InProcessor->PathLength->Get(Index);

			double ArriveAlphaSum = ArriveAlpha;
			double LeaveAlphaSum = LeaveAlpha;

			if (PrevBevel)
			{
				ArriveAlphaSum += PrevBevel->LeaveAlpha;
			}
			else
			{
				ArriveAlphaSum = 1.0;
			}

			EffectiveArriveLimit = ArriveLen * (ArriveAlpha * (1.0 / ArriveAlphaSum));

			if (NextBevel)
			{
				LeaveAlphaSum += NextBevel->ArriveAlpha;
			}
			else
			{
				LeaveAlphaSum = 1.0;
			}

			EffectiveLeaveLimit = LeaveLen * (LeaveAlpha * (1.0 / LeaveAlphaSum));
		}

		// Apply the most restrictive limit
		Width = FMath::Min(Width, FMath::Min(EffectiveArriveLimit, EffectiveLeaveLimit));
	}

	void FBevel::Compute(const FProcessor* InProcessor)
	{
		// Balance is now called separately in the processor

		// Compute Arrive and Leave positions
		if (InProcessor->bSlideAlongPath && ArrivePathPoints.Num() >= 2)
		{
			// Use path traversal - positions slide along the actual path geometry
			Arrive = GetPositionAlongPath(ArrivePathPoints, ArrivePathDistances, Width);
			Leave = GetPositionAlongPath(LeavePathPoints, LeavePathDistances, Width);

			// Recompute directions based on actual positions
			ArriveDir = (Arrive - Corner).GetSafeNormal();
			LeaveDir = (Leave - Corner).GetSafeNormal();
		}
		else
		{
			// Original behavior - positions along immediate neighbor directions
			Arrive = Corner + Width * ArriveDir;
			Leave = Corner + Width * LeaveDir;
		}

		Length = PCGExMath::GetPerpendicularDistance(Arrive, Leave, Corner);

		if (InProcessor->Settings->Type == EPCGExBevelProfileType::Custom)
		{
			SubdivideCustom(InProcessor);
			return;
		}

		if (!InProcessor->bSubdivide)
		{
			return;
		}

		if (InProcessor->ManhattanDetails.IsValid())
		{
			SubdivideManhattan(InProcessor);
			return;
		}

		const double Amount = InProcessor->SubdivAmountGetter->Read(Index);

		if (!InProcessor->bArc)
		{
			SubdivideLine(Amount, InProcessor->bSubdivideCount, InProcessor->bKeepCorner);
		}
		else
		{
			SubdivideArc(Amount, InProcessor->bSubdivideCount);
		}
	}

	void FBevel::SubdivideLine(const double Factor, const bool bIsCount, const bool bKeepCorner)
	{
		if (bKeepCorner)
		{
			PCGExPaths::Profile::SubdivideLineKeepCorner(Subdivisions, Arrive, Corner, Leave, Factor, bIsCount);
		}
		else
		{
			PCGExPaths::Profile::SubdivideLine(Subdivisions, Arrive, Leave, Factor, bIsCount);
		}
	}

	void FBevel::SubdivideArc(const double Factor, const bool bIsCount)
	{
		const PCGExMath::Geo::FExCenterArc Arc = PCGExMath::Geo::FExCenterArc(Arrive, Corner, Leave);
		PCGExPaths::Profile::SubdivideArc(Subdivisions, Arc, Arrive, Leave, Factor, bIsCount);
	}

	void FBevel::SubdivideCustom(const FProcessor* InProcessor)
	{
		const UPCGExBevelPathSettings* Settings = InProcessor->Settings;

		// EPCGExBevelCustomProfileScaling predates the shared profile enum and is kept for serialization
		// compatibility; their layouts must stay in sync for the casts below.
		static_assert(
			static_cast<uint8>(EPCGExBevelCustomProfileScaling::Uniform) == static_cast<uint8>(EPCGExPathProfileScaling::Uniform) &&
			static_cast<uint8>(EPCGExBevelCustomProfileScaling::Scale) == static_cast<uint8>(EPCGExPathProfileScaling::Scale) &&
			static_cast<uint8>(EPCGExBevelCustomProfileScaling::Distance) == static_cast<uint8>(EPCGExPathProfileScaling::Distance),
			"EPCGExBevelCustomProfileScaling must mirror EPCGExPathProfileScaling");

		// Uniform scaling defaults to the projected profile span; Scale is relative to the bevel depth (Length)
		const double Span = FVector::Dist(Leave, Arrive);

		const double MainAxisSize = PCGExPaths::Profile::ResolveAxisSize(
			static_cast<EPCGExPathProfileScaling>(Settings->MainAxisScaling), Settings->MainAxisScale, Span, Length);
		const double CrossAxisSize = PCGExPaths::Profile::ResolveAxisSize(
			static_cast<EPCGExPathProfileScaling>(Settings->CrossAxisScaling), Settings->CrossAxisScale, Span, Length);

		PCGExPaths::Profile::SubdivideCustom(
			Subdivisions, InProcessor->Context->CustomProfilePositions,
			Arrive, Leave, PCGExMath::GetNormal(Arrive, Leave, Corner) * -1,
			MainAxisSize, CrossAxisSize);
	}

	void FBevel::SubdivideManhattan(const FProcessor* InProcessor)
	{
		PCGExPaths::Profile::SubdivideManhattan(
			Subdivisions, InProcessor->ManhattanDetails, Index,
			Arrive, Leave, InProcessor->bKeepCorner ? &Corner : nullptr);
	}

	void FProcessor::ComputeSlidingLimits()
	{
		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		// Compute sliding limits for all bevels
		for (int32 i = 0; i < NumPoints; ++i)
		{
			if (const TSharedPtr<FBevel>& Bevel = Bevels[i])
			{
				Bevel->ComputeSlidingLimits(this, InTransforms);
			}
		}
	}

	void FProcessor::ApplySlidingLimits()
	{
		// Apply sliding limits to all bevels for ClosestNeighbor mode
		// Balanced mode is handled in Balance()
		if (Settings->Limit != EPCGExBevelLimit::ClosestNeighbor)
		{
			return;
		}

		for (int32 i = 0; i < NumPoints; ++i)
		{
			if (const TSharedPtr<FBevel>& Bevel = Bevels[i])
			{
				// Apply the sliding limit as the maximum width
				const double SlidingLimit = FMath::Min(Bevel->ArriveSlidingLimit, Bevel->LeaveSlidingLimit);
				Bevel->Width = FMath::Min(Bevel->Width, SlidingLimit);
			}
		}
	}

	void FProcessor::MarkConsumedPoints()
	{
		if (!bSlideAlongPath)
		{
			return;
		}

		// Initialize consumed array
		ConsumedByBevel.Init(false, NumPoints);

		// For each bevel, mark points that are consumed (passed through) by the bevel
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const TSharedPtr<FBevel>& Bevel = Bevels[i];
			if (!Bevel)
			{
				continue;
			}

			const double BevelWidth = Bevel->Width;

			// Check arrive side - mark points whose cumulative distance is less than Width
			// (meaning the bevel has passed through them)
			for (int32 j = 0; j < Bevel->ArrivePathIndices.Num(); ++j)
			{
				// ArrivePathDistances[j+1] because index 0 is the corner with distance 0
				const double PointDistance = Bevel->ArrivePathDistances[j + 1];
				const int32 PointIdx = Bevel->ArrivePathIndices[j];

				// If this point is before where the bevel ends, and it's not another bevel point, consume it
				if (PointDistance < BevelWidth - KINDA_SMALL_NUMBER && !Bevels[PointIdx])
				{
					ConsumedByBevel[PointIdx] = true;
				}
			}

			// Check leave side - mark points whose cumulative distance is less than Width
			for (int32 j = 0; j < Bevel->LeavePathIndices.Num(); ++j)
			{
				const double PointDistance = Bevel->LeavePathDistances[j + 1];
				const int32 PointIdx = Bevel->LeavePathIndices[j];

				if (PointDistance < BevelWidth - KINDA_SMALL_NUMBER && !Bevels[PointIdx])
				{
					ConsumedByBevel[PointIdx] = true;
				}
			}
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBevelPath::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const UPCGBasePointData* InPoints = PointDataFacade->GetIn();
		NumPoints = PointDataFacade->GetNum();

		Path = MakeShared<PCGExPaths::FPath>(InPoints, 0);
		PathLength = Path->AddExtra<PCGExPaths::FPathEdgeLength>();

		Path->ComputeAllEdgeExtra();

		bIsClosedLoop = Path->IsClosedLoop();

		Bevels.Init(nullptr, NumPoints);

		WidthGetter = Settings->Width.GetValueSetting();
		if (!WidthGetter->Init(PointDataFacade))
		{
			return false;
		}

		bKeepCorner = Settings->bKeepCornerPoint;
		bSlideAlongPath = Settings->bSlideAlongPath && (Settings->Limit != EPCGExBevelLimit::None);

		if (Settings->bSubdivide)
		{
			bSubdivide = Settings->Type != EPCGExBevelProfileType::Custom;
			if (bSubdivide)
			{
				bSubdivideCount = Settings->SubdivideMethod != EPCGExSubdivideMode::Distance;
				if (Settings->SubdivideMethod != EPCGExSubdivideMode::Manhattan)
				{
					SubdivAmountGetter = Settings->GetValueSettingSubdivisions();
					if (!SubdivAmountGetter->Init(PointDataFacade))
					{
						return false;
					}
				}
			}
		}
		if (bKeepCorner && Settings->Type == EPCGExBevelProfileType::Line)
		{
			// This is to force line to go through subdiv flow
			bSubdivide = true;
			bSubdivideCount = true;
			SubdivAmountGetter = PCGExDetails::MakeSettingValue<double>(0);
		}

		if (Settings->SubdivideMethod == EPCGExSubdivideMode::Manhattan)
		{
			ManhattanDetails = Settings->ManhattanDetails;
			if (!ManhattanDetails.Init(Context, PointDataFacade))
			{
				return false;
			}
		}

		bArc = Settings->Type == EPCGExBevelProfileType::Arc;

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, Preparation)

		Preparation->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS

			// Compute sliding limits after all bevels are created
			if (This->bSlideAlongPath)
			{
				This->ComputeSlidingLimits();
				This->ApplySlidingLimits();
			}

			// Now balance all bevels (requires sliding limits to be computed first)
			if (This->Settings->Limit == EPCGExBevelLimit::Balanced)
			{
				for (int32 i = 0; i < This->NumPoints; ++i)
				{
					if (const TSharedPtr<FBevel>& Bevel = This->Bevels[i])
					{
						Bevel->Balance(This.Get());
					}
				}
			}

			// Mark points consumed by sliding bevels
			This->MarkConsumedPoints();

			This->StartParallelLoopForPoints(PCGExData::EIOSide::In);
		};

		Preparation->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			This->PointDataFacade->Fetch(Scope);
			This->FilterScope(Scope);

			if (!This->bIsClosedLoop)
			{
				// Bevel is not supported on open path endpoints
				if (Scope.Start == 0)
				{
					This->PointFilterCache[0] = false;
				}
				if (Scope.End == This->NumPoints)
				{
					This->PointFilterCache[This->NumPoints - 1] = false;
				}
			}

			PCGEX_SCOPE_LOOP(i)
			{
				This->PrepareSinglePoint(i);
			}
		};

		Preparation->StartSubLoops(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());

		return true;
	}

	void FProcessor::PrepareSinglePoint(const int32 Index)
	{
		if (!PointFilterCache[Index])
		{
			return;
		}

		Bevels[Index] = MakeShared<FBevel>(Index, this);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BevelPath::ProcessPoints);

		PCGEX_SCOPE_LOOP(Index)
		{
			const TSharedPtr<FBevel>& Bevel = Bevels[Index];
			if (!Bevel)
			{
				continue;
			}

			Bevel->Compute(this);
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		// Only pin properties we will not be inheriting
		TConstPCGValueRange<FTransform> InTransform = InPointData->GetConstTransformValueRange();
		TConstPCGValueRange<int32> InSeeds = InPointData->GetConstSeedValueRange();

		TPCGValueRange<FTransform> OutTransform = OutPointData->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = OutPointData->GetSeedValueRange(false);

		TArray<int32>& IdxMapping = PointDataFacade->Source->GetIdxMapping();

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 StartIndex = StartIndices[Index];

			// Skip consumed points
			if (StartIndex < 0)
			{
				continue;
			}

			const TSharedPtr<FBevel>& Bevel = Bevels[Index];

			if (!Bevel)
			{
				IdxMapping[StartIndex] = Index;
				OutTransform[StartIndex] = InTransform[Index];
				OutSeeds[StartIndex] = InSeeds[Index];
				continue;
			}

			const int32 A = Bevel->StartOutputIndex;
			const int32 B = Bevel->EndOutputIndex;

			for (int i = A; i <= B; i++)
			{
				IdxMapping[i] = Index;
				OutTransform[i] = InTransform[Index];
			}

			OutTransform[A].SetLocation(Bevel->Arrive);
			OutTransform[B].SetLocation(Bevel->Leave);

			OutSeeds[A] = PCGExRandomHelpers::ComputeSpatialSeed(Bevel->Arrive);
			OutSeeds[B] = PCGExRandomHelpers::ComputeSpatialSeed(Bevel->Leave);

			if (Bevel->Subdivisions.IsEmpty())
			{
				continue;
			}

			for (int i = 0; i < Bevel->Subdivisions.Num(); i++)
			{
				const int32 SubIndex = A + i + 1;
				OutTransform[SubIndex].SetLocation(Bevel->Subdivisions[i]);
				OutSeeds[SubIndex] = PCGExRandomHelpers::ComputeSpatialSeed(Bevel->Subdivisions[i]);
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		// Seed is excluded from the carry-over: ProcessRange writes spatial seeds for bevel points and copies source seeds for pass-through points
		constexpr EPCGPointNativeProperties CarryOverProperties = static_cast<EPCGPointNativeProperties>(static_cast<uint8>(EPCGPointNativeProperties::All) & ~static_cast<uint8>(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry));

		PointDataFacade->Source->ConsumeIdxMapping(CarryOverProperties);
	}

	void FProcessor::WriteFlags(const int32 Index)
	{
		const TSharedPtr<FBevel>& Bevel = Bevels[Index];
		if (!Bevel)
		{
			return;
		}

		if (EndpointsWriter)
		{
			EndpointsWriter->SetValue(Bevel->StartOutputIndex, true);
			EndpointsWriter->SetValue(Bevel->EndOutputIndex, true);
		}

		if (StartPointWriter)
		{
			StartPointWriter->SetValue(Bevel->StartOutputIndex, true);
		}

		if (EndPointWriter)
		{
			EndPointWriter->SetValue(Bevel->EndOutputIndex, true);
		}

		if (SubdivisionWriter)
		{
			for (int i = 1; i <= Bevel->Subdivisions.Num(); i++)
			{
				SubdivisionWriter->SetValue(Bevel->StartOutputIndex + i, true);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		PCGExArrayHelpers::InitArray(StartIndices, NumPoints);

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		int32 NumBevels = 0;
		int32 NumOutPoints = 0;

		const bool bHasConsumedPoints = ConsumedByBevel.Num() > 0;

		for (int i = 0; i < StartIndices.Num(); i++)
		{
			// Skip consumed points (not bevels that were passed through by sliding)
			if (bHasConsumedPoints && ConsumedByBevel[i])
			{
				StartIndices[i] = -1; // Mark as skipped
				continue;
			}

			StartIndices[i] = NumOutPoints;

			if (const TSharedPtr<FBevel>& Bevel = Bevels[i])
			{
				NumBevels++;

				Bevel->StartOutputIndex = NumOutPoints;
				NumOutPoints += Bevel->Subdivisions.Num() + 1;
				Bevel->EndOutputIndex = NumOutPoints;
			}

			NumOutPoints++;
		}

		if (NumBevels == 0)
		{
			PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::Duplicate)
			Settings->InitOutputFlags(PointIO);
			return;
		}

		PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::New)
		Settings->InitOutputFlags(PointIO);

		// Build output points

		UPCGBasePointData* MutablePoints = PointDataFacade->GetOut();

		// Transform & Seed are written for every output point in ProcessRange, ensure they're allocated even when absent on the input
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumOutPoints, PointDataFacade->GetAllocations() | EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed);

		// Initialize metadata entries at once, too expensive on thread

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		UPCGMetadata* Metadata = OutPointData->Metadata;

		// Only pin properties we will not be inheriting
		TConstPCGValueRange<int64> InMetadataEntry = InPointData->GetConstMetadataEntryValueRange();
		TPCGValueRange<int64> OutMetadataEntry = OutPointData->GetMetadataEntryValueRange();

		for (int Index = 0; Index < NumPoints; Index++)
		{
			const int32 StartIndex = StartIndices[Index];

			// Skip consumed points
			if (StartIndex < 0)
			{
				continue;
			}

			const TSharedPtr<FBevel>& Bevel = Bevels[Index];

			if (!Bevel)
			{
				OutMetadataEntry[StartIndex] = InMetadataEntry[Index];
				Metadata->InitializeOnSet(OutMetadataEntry[StartIndex]);
				continue;
			}

			const int32 A = Bevel->StartOutputIndex;
			const int32 B = Bevel->EndOutputIndex;

			for (int i = A; i <= B; i++)
			{
				OutMetadataEntry[i] = InMetadataEntry[Index];
				Metadata->InitializeOnSet(OutMetadataEntry[i]);
			}
		}

		StartParallelLoopForRange(NumPoints);
	}

	void FProcessor::Write()
	{
		if (Settings->bFlagPoles)
		{
			EndpointsWriter = PointDataFacade->GetWritable<bool>(Settings->PoleFlagName, false, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bFlagStartPoint)
		{
			StartPointWriter = PointDataFacade->GetWritable<bool>(Settings->StartPointFlagName, false, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bFlagEndPoint)
		{
			EndPointWriter = PointDataFacade->GetWritable<bool>(Settings->EndPointFlagName, false, true, PCGExData::EBufferInit::New);
		}

		if (Settings->bFlagSubdivision)
		{
			SubdivisionWriter = PointDataFacade->GetWritable<bool>(Settings->SubdivisionFlagName, false, true, PCGExData::EBufferInit::New);
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, WriteFlagsTask)
		WriteFlagsTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->PointDataFacade->WriteFastest(This->TaskManager);
		};

		WriteFlagsTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			PCGEX_SCOPE_LOOP(i)
			{
				if (!This->PointFilterCache[i])
				{
					continue;
				}
				This->WriteFlags(i);
			}
		};

		WriteFlagsTask->StartSubLoops(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());

		IProcessor::Write();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
