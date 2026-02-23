// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Adapted from https://www.nature.com/articles/s41598-023-39013-9
// Sebastián V. Romero, Eneko Osaba, Esther Villar-Rodriguez, Izaskun Oregi & Yue Ban
// Hybrid approach for solving real-world bin packing problem instances using quantum annealer

#include "Elements/Layout/PCGExBinPacking3D.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Elements/Layout/PCGExLayout.h"
#include "Math/PCGExMathBounds.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

#define LOCTEXT_NAMESPACE "PCGExBinPacking3DElement"
#define PCGEX_NAMESPACE BinPacking3D

#pragma region UPCGExBinPacking3DSettings

bool UPCGExBinPacking3DSettings::GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const
{
	OutRules.Append(PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules));
	return !OutRules.IsEmpty();
}

TArray<FPCGPinProperties> UPCGExBinPacking3DSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExLayout::SourceBinsLabel, "List of bins to fit input points into. Each input collection is expected to have a matching collection of bins.", Required)
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Normal);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBinPacking3DSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExLayout::OutputBinsLabel, "Input bins, with added statistics.", Required)
	PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discarded points, ones that could not fit into any bin.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BinPacking3D)

PCGExData::EIOInit UPCGExBinPacking3DSettings::GetMainDataInitializationPolicy() const { return PCGExData::EIOInit::Duplicate; }

#pragma endregion

#pragma region FPCGExBinPacking3DElement

PCGEX_ELEMENT_BATCH_POINT_IMPL(BinPacking3D)

bool FPCGExBinPacking3DElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BinPacking3D)

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

	for (int i = 0; i < NumBins; i++) { Context->Bins->Pairs[i]->OutputPin = Context->Bins->OutputPin; }

	Context->Discarded = MakeShared<PCGExData::FPointIOCollection>(InContext);
	Context->Discarded->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;

	return true;
}

bool FPCGExBinPacking3DElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBinPacking3DElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BinPacking3D)
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
				NewBatch->bPrefetchData = !OutRules.IsEmpty() || Settings->bSortByVolume;
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

#pragma endregion

namespace PCGExBinPacking3D
{
#pragma region FBP3DRotationHelper

	void FBP3DRotationHelper::GetPaper6Rotations(const FVector& ItemSize, TArray<FRotator>& OutRotations)
	{
		OutRotations.Empty();

		const double L = ItemSize.X;
		const double W = ItemSize.Y;
		const double H = ItemSize.Z;

		const bool bLW = FMath::IsNearlyEqual(L, W, KINDA_SMALL_NUMBER);
		const bool bLH = FMath::IsNearlyEqual(L, H, KINDA_SMALL_NUMBER);
		const bool bWH = FMath::IsNearlyEqual(W, H, KINDA_SMALL_NUMBER);

		// The 6 axis permutations map dimensions (L,W,H) to output AABB axes (X,Y,Z).
		// Verified against UE quaternion math: FRotator(P,Y,R) = Q_Z(Yaw) * Q_Y(Pitch) * Q_X(Roll)
		//
		// FRotator(0,0,0)   -> (L,W,H) identity
		// FRotator(0,0,90)  -> (L,H,W) roll 90: swaps W<->H
		// FRotator(0,90,0)  -> (W,L,H) yaw 90: swaps L<->W
		// FRotator(90,0,0)  -> (H,W,L) pitch 90: swaps L<->H
		// FRotator(0,90,90) -> (H,L,W) yaw 90 + roll 90
		// FRotator(90,90,0) -> (W,H,L) pitch 90 + yaw 90

		// Always include identity
		OutRotations.Add(FRotator(0, 0, 0)); // (L,W,H)

		if (bLW && bLH)
		{
			// Cube: all dimensions equal, only 1 unique orientation
			return;
		}

		if (bLW)
		{
			// L==W, H different: square prism along Z
			// Unique: (L,L,H), (H,L,L), (L,H,L) -> 3 orientations
			OutRotations.Add(FRotator(90, 0, 0));  // (H,W,L) = (H,L,L)
			OutRotations.Add(FRotator(0, 0, 90));  // (L,H,W) = (L,H,L)
			return;
		}

		if (bLH)
		{
			// L==H, W different: square prism along Y
			// Unique: (L,W,L), (L,L,W), (W,L,L) -> 3 orientations
			OutRotations.Add(FRotator(0, 0, 90));  // (L,H,W) = (L,L,W)
			OutRotations.Add(FRotator(0, 90, 0));  // (W,L,H) = (W,L,L)
			return;
		}

		if (bWH)
		{
			// W==H, L different: square prism along X
			// Unique: (L,W,W), (W,L,W), (W,W,L) -> 3 orientations
			OutRotations.Add(FRotator(0, 90, 0));   // (W,L,H) = (W,L,W)
			OutRotations.Add(FRotator(90, 90, 0));   // (W,H,L) = (W,W,L)
			return;
		}

		// All dimensions different: 6 unique orientations
		OutRotations.Add(FRotator(0, 0, 90));    // (L,H,W)
		OutRotations.Add(FRotator(0, 90, 0));    // (W,L,H)
		OutRotations.Add(FRotator(90, 90, 0));   // (W,H,L)
		OutRotations.Add(FRotator(0, 90, 90));   // (H,L,W)
		OutRotations.Add(FRotator(90, 0, 0));    // (H,W,L)
	}

	void FBP3DRotationHelper::GetRotationsToTest(EPCGExBP3DRotationMode Mode, TArray<FRotator>& OutRotations)
	{
		OutRotations.Empty();

		switch (Mode)
		{
		case EPCGExBP3DRotationMode::None:
			OutRotations.Add(FRotator::ZeroRotator);
			break;

		case EPCGExBP3DRotationMode::CardinalOnly:
			OutRotations.Add(FRotator(0, 0, 0));
			OutRotations.Add(FRotator(0, 90, 0));
			OutRotations.Add(FRotator(0, 180, 0));
			OutRotations.Add(FRotator(0, 270, 0));
			break;

		case EPCGExBP3DRotationMode::Paper6:
			OutRotations.Add(FRotator::ZeroRotator);
			break;

		case EPCGExBP3DRotationMode::AllOrthogonal:
			OutRotations.Add(FRotator(0, 0, 0));
			OutRotations.Add(FRotator(0, 0, 90));
			OutRotations.Add(FRotator(0, 0, 180));
			OutRotations.Add(FRotator(0, 0, 270));
			OutRotations.Add(FRotator(0, 180, 0));
			OutRotations.Add(FRotator(0, 180, 90));
			OutRotations.Add(FRotator(0, 180, 180));
			OutRotations.Add(FRotator(0, 180, 270));
			OutRotations.Add(FRotator(0, 90, 0));
			OutRotations.Add(FRotator(0, 90, 90));
			OutRotations.Add(FRotator(0, 90, 180));
			OutRotations.Add(FRotator(0, 90, 270));
			OutRotations.Add(FRotator(0, -90, 0));
			OutRotations.Add(FRotator(0, -90, 90));
			OutRotations.Add(FRotator(0, -90, 180));
			OutRotations.Add(FRotator(0, -90, 270));
			OutRotations.Add(FRotator(90, 0, 0));
			OutRotations.Add(FRotator(90, 0, 90));
			OutRotations.Add(FRotator(90, 0, 180));
			OutRotations.Add(FRotator(90, 0, 270));
			OutRotations.Add(FRotator(-90, 0, 0));
			OutRotations.Add(FRotator(-90, 0, 90));
			OutRotations.Add(FRotator(-90, 0, 180));
			OutRotations.Add(FRotator(-90, 0, 270));
			break;
		}
	}

	FVector FBP3DRotationHelper::RotateSize(const FVector& Size, const FRotator& Rotation)
	{
		if (Rotation.IsNearlyZero()) { return Size; }

		const FQuat Quat = Rotation.Quaternion();
		const FVector HalfSize = Size * 0.5;
		FVector Min = FVector(MAX_dbl);
		FVector Max = FVector(-MAX_dbl);

		for (int32 i = 0; i < 8; i++)
		{
			FVector Corner(
				(i & 1) ? HalfSize.X : -HalfSize.X,
				(i & 2) ? HalfSize.Y : -HalfSize.Y,
				(i & 4) ? HalfSize.Z : -HalfSize.Z
			);

			Corner = Quat.RotateVector(Corner);
			Min = Min.ComponentMin(Corner);
			Max = Max.ComponentMax(Corner);
		}

		return Max - Min;
	}

#pragma endregion

#pragma region FBP3DBin

	FBP3DBin::FBP3DBin(int32 InBinIndex, const PCGExData::FConstPoint& InBinPoint, const FVector& InSeed)
	{
		BinIndex = InBinIndex;
		Seed = InSeed;
		Bounds = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(InBinPoint);

		Transform = InBinPoint.GetTransform();
		Transform.SetScale3D(FVector::OneVector);

		MaxVolume = Bounds.GetVolume();
		UsedVolume = 0;
		CurrentWeight = 0;

		// Determine packing direction from seed position relative to bin center
		const FVector BinCenter = Bounds.GetCenter();
		for (int C = 0; C < 3; C++)
		{
			PackSign[C] = (Seed[C] <= BinCenter[C]) ? 1.0 : -1.0;
		}

		// Initial extreme point at the packing origin corner
		FVector PackOrigin;
		for (int C = 0; C < 3; C++)
		{
			PackOrigin[C] = (PackSign[C] > 0) ? Bounds.Min[C] : Bounds.Max[C];
		}
		ExtremePoints.Add(PackOrigin);
	}

	void FBP3DBin::AddExtremePoint(const FVector& Point)
	{
		// Deduplicate
		for (const FVector& EP : ExtremePoints)
		{
			if (FVector::DistSquared(EP, Point) < KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER)
			{
				return;
			}
		}
		ExtremePoints.Add(Point);
	}

	FVector FBP3DBin::ProjectPoint(const FVector& RawPoint) const
	{
		// Project each axis independently toward the pack origin (the nearest resting surface)
		FVector Result;

		for (int C = 0; C < 3; C++)
		{
			const int32 A = (C + 1) % 3;
			const int32 B = (C + 2) % 3;

			if (PackSign[C] > 0)
			{
				// Packing from Min: slide toward Min, stop at nearest item Max face
				double Best = Bounds.Min[C];
				for (const FBP3DItem& Item : Items)
				{
					if (Item.PaddedBox.Max[C] <= RawPoint[C] + KINDA_SMALL_NUMBER && Item.PaddedBox.Max[C] > Best)
					{
						// Point must be within item's footprint on the other two axes
						if (RawPoint[A] >= Item.PaddedBox.Min[A] - KINDA_SMALL_NUMBER &&
							RawPoint[A] < Item.PaddedBox.Max[A] + KINDA_SMALL_NUMBER &&
							RawPoint[B] >= Item.PaddedBox.Min[B] - KINDA_SMALL_NUMBER &&
							RawPoint[B] < Item.PaddedBox.Max[B] + KINDA_SMALL_NUMBER)
						{
							Best = Item.PaddedBox.Max[C];
						}
					}
				}
				Result[C] = Best;
			}
			else
			{
				// Packing from Max: slide toward Max, stop at nearest item Min face
				double Best = Bounds.Max[C];
				for (const FBP3DItem& Item : Items)
				{
					if (Item.PaddedBox.Min[C] >= RawPoint[C] - KINDA_SMALL_NUMBER && Item.PaddedBox.Min[C] < Best)
					{
						if (RawPoint[A] >= Item.PaddedBox.Min[A] - KINDA_SMALL_NUMBER &&
							RawPoint[A] < Item.PaddedBox.Max[A] + KINDA_SMALL_NUMBER &&
							RawPoint[B] >= Item.PaddedBox.Min[B] - KINDA_SMALL_NUMBER &&
							RawPoint[B] < Item.PaddedBox.Max[B] + KINDA_SMALL_NUMBER)
						{
							Best = Item.PaddedBox.Min[C];
						}
					}
				}
				Result[C] = Best;
			}
		}

		return Result;
	}

	bool FBP3DBin::IsInsideAnyItem(const FVector& Point) const
	{
		for (const FBP3DItem& Item : Items)
		{
			if (Point.X > Item.PaddedBox.Min.X + KINDA_SMALL_NUMBER &&
				Point.X < Item.PaddedBox.Max.X - KINDA_SMALL_NUMBER &&
				Point.Y > Item.PaddedBox.Min.Y + KINDA_SMALL_NUMBER &&
				Point.Y < Item.PaddedBox.Max.Y - KINDA_SMALL_NUMBER &&
				Point.Z > Item.PaddedBox.Min.Z + KINDA_SMALL_NUMBER &&
				Point.Z < Item.PaddedBox.Max.Z - KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
		return false;
	}

	void FBP3DBin::GenerateExtremePoints(const FBox& PaddedItemBox)
	{
		// Generate 3 new EPs at the far edges of the placed item (direction of packing growth)
		for (int C = 0; C < 3; C++)
		{
			FVector RawEP;
			for (int A = 0; A < 3; A++)
			{
				if (A == C)
				{
					// On this axis: item's far edge (direction of packing)
					RawEP[A] = (PackSign[A] > 0) ? PaddedItemBox.Max[A] : PaddedItemBox.Min[A];
				}
				else
				{
					// On other axes: item's near edge (toward pack origin)
					RawEP[A] = (PackSign[A] > 0) ? PaddedItemBox.Min[A] : PaddedItemBox.Max[A];
				}
			}

			// Project the EP to rest on the nearest surface
			const FVector Projected = ProjectPoint(RawEP);

			// Add projected EP if valid
			if (Bounds.IsInsideOrOn(Projected) && !IsInsideAnyItem(Projected))
			{
				AddExtremePoint(Projected);
			}

			// Also add raw EP if different from projected and valid (more candidate positions)
			if (!RawEP.Equals(Projected, KINDA_SMALL_NUMBER))
			{
				if (Bounds.IsInsideOrOn(RawEP) && !IsInsideAnyItem(RawEP))
				{
					AddExtremePoint(RawEP);
				}
			}
		}
	}

	void FBP3DBin::RemoveInvalidExtremePoints(const FBox& PaddedItemBox)
	{
		for (int32 i = ExtremePoints.Num() - 1; i >= 0; i--)
		{
			const FVector& EP = ExtremePoints[i];
			// Remove if EP is strictly inside the newly placed item's padded box
			if (EP.X > PaddedItemBox.Min.X + KINDA_SMALL_NUMBER &&
				EP.X < PaddedItemBox.Max.X - KINDA_SMALL_NUMBER &&
				EP.Y > PaddedItemBox.Min.Y + KINDA_SMALL_NUMBER &&
				EP.Y < PaddedItemBox.Max.Y - KINDA_SMALL_NUMBER &&
				EP.Z > PaddedItemBox.Min.Z + KINDA_SMALL_NUMBER &&
				EP.Z < PaddedItemBox.Max.Z - KINDA_SMALL_NUMBER)
			{
				ExtremePoints.RemoveAt(i);
			}
		}
	}

	bool FBP3DBin::HasOverlap(const FBox& TestBox) const
	{
		for (const FBP3DItem& Item : Items)
		{
			// Strict overlap check (touching faces is OK)
			if (TestBox.Min.X < Item.PaddedBox.Max.X - KINDA_SMALL_NUMBER &&
				TestBox.Max.X > Item.PaddedBox.Min.X + KINDA_SMALL_NUMBER &&
				TestBox.Min.Y < Item.PaddedBox.Max.Y - KINDA_SMALL_NUMBER &&
				TestBox.Max.Y > Item.PaddedBox.Min.Y + KINDA_SMALL_NUMBER &&
				TestBox.Min.Z < Item.PaddedBox.Max.Z - KINDA_SMALL_NUMBER &&
				TestBox.Max.Z > Item.PaddedBox.Min.Z + KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
		return false;
	}

	double FBP3DBin::ComputeContactScore(const FBox& TestBox) const
	{
		int32 Contacts = 0;

		// Check bin walls
		for (int C = 0; C < 3; C++)
		{
			if (FMath::IsNearlyEqual(TestBox.Min[C], Bounds.Min[C], KINDA_SMALL_NUMBER)) { Contacts++; }
			if (FMath::IsNearlyEqual(TestBox.Max[C], Bounds.Max[C], KINDA_SMALL_NUMBER)) { Contacts++; }
		}

		// Check contact with placed items (face-to-face adjacency with padded boxes)
		for (const FBP3DItem& Item : Items)
		{
			for (int C = 0; C < 3; C++)
			{
				const int32 A = (C + 1) % 3;
				const int32 B = (C + 2) % 3;

				// Check if ranges overlap on the other two axes (indicates face contact, not just edge)
				const bool bRangeA = TestBox.Max[A] > Item.PaddedBox.Min[A] + KINDA_SMALL_NUMBER &&
					TestBox.Min[A] < Item.PaddedBox.Max[A] - KINDA_SMALL_NUMBER;
				const bool bRangeB = TestBox.Max[B] > Item.PaddedBox.Min[B] + KINDA_SMALL_NUMBER &&
					TestBox.Min[B] < Item.PaddedBox.Max[B] - KINDA_SMALL_NUMBER;

				if (bRangeA && bRangeB)
				{
					if (FMath::IsNearlyEqual(TestBox.Min[C], Item.PaddedBox.Max[C], KINDA_SMALL_NUMBER)) { Contacts++; }
					if (FMath::IsNearlyEqual(TestBox.Max[C], Item.PaddedBox.Min[C], KINDA_SMALL_NUMBER)) { Contacts++; }
				}
			}
		}

		// Normalize to [0,1], lower is better (more contacts = better = lower score)
		return 1.0 - (static_cast<double>(FMath::Min(Contacts, 6)) / 6.0);
	}

	bool FBP3DBin::EvaluatePlacement(
		const FVector& ItemSize,
		const FVector& ItemPadding,
		int32 EPIndex,
		const FRotator& Rotation,
		FBP3DPlacementCandidate& OutCandidate) const
	{
		if (EPIndex < 0 || EPIndex >= ExtremePoints.Num()) { return false; }

		const FVector& EP = ExtremePoints[EPIndex];
		const FVector RotatedSize = FBP3DRotationHelper::RotateSize(ItemSize, Rotation);

		// Compute effective padding (rotate if not absolute)
		FVector EffectivePadding = ItemPadding;
		if (!bAbsolutePadding && !Rotation.IsNearlyZero())
		{
			EffectivePadding = FBP3DRotationHelper::RotateSize(ItemPadding, Rotation);
		}

		// Padded size is what the algorithm uses for placement/collision
		const FVector PaddedSize = RotatedSize + EffectivePadding * 2.0;

		// Compute placement position for the padded box based on packing direction
		FVector PaddedMin;
		for (int C = 0; C < 3; C++)
		{
			if (PackSign[C] > 0)
			{
				PaddedMin[C] = EP[C];
			}
			else
			{
				PaddedMin[C] = EP[C] - PaddedSize[C];
			}
		}

		const FBox PaddedBox(PaddedMin, PaddedMin + PaddedSize);

		// Bounds check with padded box
		if (PaddedBox.Min.X < Bounds.Min.X - KINDA_SMALL_NUMBER ||
			PaddedBox.Min.Y < Bounds.Min.Y - KINDA_SMALL_NUMBER ||
			PaddedBox.Min.Z < Bounds.Min.Z - KINDA_SMALL_NUMBER ||
			PaddedBox.Max.X > Bounds.Max.X + KINDA_SMALL_NUMBER ||
			PaddedBox.Max.Y > Bounds.Max.Y + KINDA_SMALL_NUMBER ||
			PaddedBox.Max.Z > Bounds.Max.Z + KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// Overlap check against all placed items (padded vs padded)
		if (HasOverlap(PaddedBox)) { return false; }

		// Actual item position (inset from padded box)
		const FVector ActualMin = PaddedMin + EffectivePadding;

		// Compute paper objective scores
		const double PaddedVolume = PaddedSize.X * PaddedSize.Y * PaddedSize.Z;
		const FVector BinSize = Bounds.GetSize();

		// o1: Bin usage -- prefer fuller bins (lower score = better), uses padded volume
		const double CurrentFillRatio = MaxVolume > 0 ? (UsedVolume + PaddedVolume) / MaxVolume : 0;
		const double BinUsageScore = 1.0 - CurrentFillRatio;

		// o2: Height -- prefer lower actual placement Z (Paper Eq. 2)
		const double NormalizedZ = BinSize.Z > KINDA_SMALL_NUMBER
			                           ? (ActualMin.Z + RotatedSize.Z - Bounds.Min.Z) / BinSize.Z
			                           : 0.0;

		// o3: Load balance -- Manhattan distance of actual center to bin center (Paper Eq. 3)
		const FVector ActualCenter = ActualMin + RotatedSize * 0.5;
		const FVector BinCenter = Bounds.GetCenter();
		const FVector Diff = (ActualCenter - BinCenter).GetAbs();
		const FVector BinExtent = Bounds.GetExtent();
		const double MaxManhattan = BinExtent.X + BinExtent.Y + BinExtent.Z;
		const double ManhattanDist = Diff.X + Diff.Y + Diff.Z;
		const double LoadBalanceScore = MaxManhattan > KINDA_SMALL_NUMBER ? ManhattanDist / MaxManhattan : 0.0;

		// Contact score -- uses padded box for algorithm adjacency
		const double ContactScoreVal = ComputeContactScore(PaddedBox);

		OutCandidate.BinIndex = BinIndex;
		OutCandidate.EPIndex = EPIndex;
		OutCandidate.Rotation = Rotation;
		OutCandidate.RotatedSize = RotatedSize;
		OutCandidate.PlacementMin = ActualMin;
		OutCandidate.EffectivePadding = EffectivePadding;
		OutCandidate.BinUsageScore = BinUsageScore;
		OutCandidate.HeightScore = NormalizedZ;
		OutCandidate.LoadBalanceScore = LoadBalanceScore;
		OutCandidate.ContactScore = ContactScoreVal;

		return true;
	}

	bool FBP3DBin::CheckLoadBearing(const FBP3DPlacementCandidate& Candidate, double ItemWeight, double Threshold) const
	{
		if (Items.IsEmpty()) { return true; }

		// Use padded geometry since the algorithm places items in padded-box space
		const FBox CandidateActual(Candidate.PlacementMin, Candidate.PlacementMin + Candidate.RotatedSize);
		const FBox CandidatePadded = CandidateActual.ExpandBy(Candidate.EffectivePadding);

		for (const FBP3DItem& Existing : Items)
		{
			// Check if candidate is above existing using padded geometry
			const bool bAbove = CandidatePadded.Min.Z >= Existing.PaddedBox.Max.Z - KINDA_SMALL_NUMBER;

			if (!bAbove) { continue; }

			// Check XY overlap using padded geometry
			const bool bXOverlap = CandidatePadded.Min.X < Existing.PaddedBox.Max.X && CandidatePadded.Max.X > Existing.PaddedBox.Min.X;
			const bool bYOverlap = CandidatePadded.Min.Y < Existing.PaddedBox.Max.Y && CandidatePadded.Max.Y > Existing.PaddedBox.Min.Y;

			if (bXOverlap && bYOverlap)
			{
				if (ItemWeight > Threshold * Existing.Weight)
				{
					return false;
				}
			}
		}

		return true;
	}

	double FBP3DBin::ComputeSupportRatio(const FBox& ItemBox) const
	{
		const double BaseArea = (ItemBox.Max.X - ItemBox.Min.X) * (ItemBox.Max.Y - ItemBox.Min.Y);
		if (BaseArea <= KINDA_SMALL_NUMBER) { return 1.0; }

		// On the bin floor = fully supported
		if (FMath::IsNearlyEqual(ItemBox.Min.Z, Bounds.Min.Z, KINDA_SMALL_NUMBER))
		{
			return 1.0;
		}

		// Sum XY overlap area with items whose padded top touches our bottom
		// Uses PaddedBox since the algorithm places items in padded-box space
		double SupportArea = 0.0;
		for (const FBP3DItem& Existing : Items)
		{
			if (!FMath::IsNearlyEqual(Existing.PaddedBox.Max.Z, ItemBox.Min.Z, KINDA_SMALL_NUMBER))
			{
				continue;
			}

			const double OverlapMinX = FMath::Max(ItemBox.Min.X, Existing.PaddedBox.Min.X);
			const double OverlapMaxX = FMath::Min(ItemBox.Max.X, Existing.PaddedBox.Max.X);
			const double OverlapMinY = FMath::Max(ItemBox.Min.Y, Existing.PaddedBox.Min.Y);
			const double OverlapMaxY = FMath::Min(ItemBox.Max.Y, Existing.PaddedBox.Max.Y);

			if (OverlapMaxX > OverlapMinX && OverlapMaxY > OverlapMinY)
			{
				SupportArea += (OverlapMaxX - OverlapMinX) * (OverlapMaxY - OverlapMinY);
			}
		}

		return FMath::Min(SupportArea / BaseArea, 1.0);
	}

	void FBP3DBin::CommitPlacement(const FBP3DPlacementCandidate& Candidate, FBP3DItem& InItem)
	{
		if (!Candidate.IsValid()) { return; }

		const FVector ItemSize = Candidate.RotatedSize;
		const FVector ItemMin = Candidate.PlacementMin;

		InItem.Box = FBox(ItemMin, ItemMin + ItemSize);
		InItem.Rotation = Candidate.Rotation;

		// Reconstruct padded box using the effective padding from evaluation
		InItem.PaddedBox = InItem.Box.ExpandBy(Candidate.EffectivePadding);

		// Update tracking
		CurrentWeight += InItem.Weight;
		if (InItem.Category >= 0)
		{
			PresentCategories.Add(InItem.Category);
		}

		// Track padded volume (the space consumed from the algorithm's perspective)
		const FVector PaddedSize = InItem.PaddedBox.GetSize();
		UsedVolume += PaddedSize.X * PaddedSize.Y * PaddedSize.Z;

		Items.Add(InItem);

		// Generate new extreme points from the placed item's padded box
		GenerateExtremePoints(InItem.PaddedBox);

		// Remove extreme points that are now inside the placed item
		RemoveInvalidExtremePoints(InItem.PaddedBox);
	}

	void FBP3DBin::UpdatePoint(PCGExData::FMutablePoint& InPoint, const FBP3DItem& InItem) const
	{
		const FQuat RotQuat = InItem.Rotation.Quaternion();
		const FVector ScaledBoundsCenter = InPoint.GetLocalBounds().GetCenter() * InPoint.GetScale3D();

		const FTransform ItemTransform = FTransform(
			RotQuat,
			InItem.Box.GetCenter() - RotQuat.RotateVector(ScaledBoundsCenter),
			InPoint.GetScale3D()
		);
		InPoint.SetTransform(ItemTransform * Transform);
	}

#pragma endregion

#pragma region FProcessor

	void FProcessor::BuildAffinityLookups()
	{
		NegativeAffinityPairs.Empty();
		PositiveAffinityGroup.Empty();

		if (!Settings->bEnableAffinities || Settings->AffinityRules.IsEmpty()) { return; }

		TMap<int32, int32> Parent;

		auto FindRoot = [&Parent](int32 X) -> int32
		{
			while (Parent.Contains(X) && Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		};

		auto Union = [&Parent, &FindRoot](int32 A, int32 B)
		{
			const int32 RootA = FindRoot(A);
			const int32 RootB = FindRoot(B);
			if (RootA != RootB)
			{
				Parent.Add(RootA, RootB);
			}
		};

		for (const FPCGExBP3DAffinityRule& Rule : Settings->AffinityRules)
		{
			if (Rule.Type == EPCGExBP3DAffinityType::Negative)
			{
				NegativeAffinityPairs.Add(MakeAffinityKey(Rule.CategoryA, Rule.CategoryB));
			}
			else
			{
				if (!Parent.Contains(Rule.CategoryA)) { Parent.Add(Rule.CategoryA, Rule.CategoryA); }
				if (!Parent.Contains(Rule.CategoryB)) { Parent.Add(Rule.CategoryB, Rule.CategoryB); }
				Union(Rule.CategoryA, Rule.CategoryB);
			}
		}

		for (auto& Pair : Parent)
		{
			PositiveAffinityGroup.Add(Pair.Key, FindRoot(Pair.Key));
		}
	}

	uint64 FProcessor::MakeAffinityKey(int32 A, int32 B)
	{
		if (A > B) { Swap(A, B); }
		return (static_cast<uint64>(static_cast<uint32>(A)) << 32) | static_cast<uint64>(static_cast<uint32>(B));
	}

	bool FProcessor::HasNegativeAffinity(int32 CatA, int32 CatB) const
	{
		return NegativeAffinityPairs.Contains(MakeAffinityKey(CatA, CatB));
	}

	int32 FProcessor::FindPositiveGroup(int32 Category) const
	{
		const int32* Group = PositiveAffinityGroup.Find(Category);
		return Group ? *Group : -1;
	}

	bool FProcessor::IsCategoryCompatibleWithBin(int32 ItemCategory, const FBP3DBin& Bin) const
	{
		if (ItemCategory < 0) { return true; }

		for (const int32 PresentCat : Bin.PresentCategories)
		{
			if (HasNegativeAffinity(ItemCategory, PresentCat))
			{
				return false;
			}
		}

		return true;
	}

	int32 FProcessor::FindRequiredBinForPositiveAffinity(int32 ItemCategory) const
	{
		if (ItemCategory < 0) { return -1; }

		const int32 ItemGroup = FindPositiveGroup(ItemCategory);
		if (ItemGroup < 0) { return -1; }

		for (int32 BinIdx = 0; BinIdx < Bins.Num(); BinIdx++)
		{
			const TSharedPtr<FBP3DBin>& Bin = Bins[BinIdx];
			for (const int32 PresentCat : Bin->PresentCategories)
			{
				if (FindPositiveGroup(PresentCat) == ItemGroup)
				{
					return BinIdx;
				}
			}
		}

		return -1;
	}

	FBP3DPlacementCandidate FProcessor::FindBestPlacement(const FBP3DItem& InItem)
	{
		FBP3DPlacementCandidate BestCandidate;
		double BestScore = MAX_dbl;

		const FVector OriginalSize = InItem.OriginalSize;

		// Generate rotations for this item
		TArray<FRotator> RotationsToTest;
		if (Settings->RotationMode == EPCGExBP3DRotationMode::Paper6)
		{
			FBP3DRotationHelper::GetPaper6Rotations(OriginalSize, RotationsToTest);
		}
		else
		{
			FBP3DRotationHelper::GetRotationsToTest(Settings->RotationMode, RotationsToTest);
		}

		// Positive affinity: if item belongs to a group that's already placed, restrict to that bin
		const int32 RequiredBin = Settings->bEnableAffinities ? FindRequiredBinForPositiveAffinity(InItem.Category) : -1;

		auto EvaluateBin = [&](int32 BinIdx)
		{
			const TSharedPtr<FBP3DBin>& Bin = Bins[BinIdx];

			// Weight constraint pre-check
			if (Settings->bEnableWeightConstraint)
			{
				if (Bin->CurrentWeight + InItem.Weight > Bin->MaxWeight)
				{
					return;
				}
			}

			// Negative affinity pre-check
			if (Settings->bEnableAffinities && InItem.Category >= 0)
			{
				if (!IsCategoryCompatibleWithBin(InItem.Category, *Bin))
				{
					return;
				}
			}

			for (int32 EPIdx = 0; EPIdx < Bin->GetEPCount(); EPIdx++)
			{
				for (int32 RotIdx = 0; RotIdx < RotationsToTest.Num(); RotIdx++)
				{
					FBP3DPlacementCandidate Candidate;
					Candidate.RotationIndex = RotIdx;

					if (Bin->EvaluatePlacement(OriginalSize, InItem.Padding, EPIdx, RotationsToTest[RotIdx], Candidate))
					{
						// Support check -- reject placements with no physical support beneath
						if (Settings->bRequireSupport)
						{
							const FBox CandidateActualBox(Candidate.PlacementMin, Candidate.PlacementMin + Candidate.RotatedSize);
							const FBox CandidatePaddedBox = CandidateActualBox.ExpandBy(Candidate.EffectivePadding);
							const double Support = Bin->ComputeSupportRatio(CandidatePaddedBox);
							if (Support < InItem.MinSupportRatio - KINDA_SMALL_NUMBER)
							{
								continue;
							}
							// With MinSupportRatio=0, still reject fully floating items (no support at all)
							if (Support < KINDA_SMALL_NUMBER)
							{
								continue;
							}
						}

						// Load bearing post-check
						if (Settings->bEnableLoadBearing)
						{
							if (!Bin->CheckLoadBearing(Candidate, InItem.Weight, InItem.LoadBearingThreshold))
							{
								continue;
							}
						}

						Candidate.Score = ComputeFinalScore(Candidate);

						if (Candidate.Score < BestScore)
						{
							BestScore = Candidate.Score;
							BestCandidate = Candidate;
						}
					}
				}
			}
		};

		if (Settings->bGlobalBestFit)
		{
			if (RequiredBin >= 0)
			{
				EvaluateBin(RequiredBin);
			}
			else
			{
				for (int32 BinIdx = 0; BinIdx < Bins.Num(); BinIdx++)
				{
					EvaluateBin(BinIdx);
				}
			}
		}
		else
		{
			if (RequiredBin >= 0)
			{
				EvaluateBin(RequiredBin);
			}
			else
			{
				for (int32 BinIdx = 0; BinIdx < Bins.Num(); BinIdx++)
				{
					BestScore = MAX_dbl;
					BestCandidate = FBP3DPlacementCandidate();

					EvaluateBin(BinIdx);

					if (BestCandidate.IsValid())
					{
						break;
					}
				}
			}
		}

		return BestCandidate;
	}

	double FProcessor::ComputeFinalScore(const FBP3DPlacementCandidate& Candidate) const
	{
		return Settings->ObjectiveWeightBinUsage * Candidate.BinUsageScore +
			Settings->ObjectiveWeightHeight * Candidate.HeightScore +
			Settings->ObjectiveWeightLoadBalance * Candidate.LoadBalanceScore +
			Settings->ObjectiveWeightContact * Candidate.ContactScore;
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
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBinPacking3D::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		PointDataFacade->Source->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		TSharedPtr<PCGExData::FPointIO> TargetBins = Context->Bins->Pairs[BatchIndex];
		PCGEX_INIT_IO(TargetBins, PCGExData::EIOInit::Duplicate)

		// Init shorthand buffers
		PaddingBuffer = Settings->OccupationPadding.GetValueSetting();
		if (!PaddingBuffer->Init(PointDataFacade)) { return false; }

		if (Settings->bEnableWeightConstraint)
		{
			ItemWeightBuffer = Settings->ItemWeight.GetValueSetting();
			if (!ItemWeightBuffer->Init(PointDataFacade)) { return false; }
		}

		if (Settings->bEnableAffinities)
		{
			CategoryBuffer = Settings->ItemCategory.GetValueSetting();
			if (!CategoryBuffer->Init(PointDataFacade)) { return false; }

			BuildAffinityLookups();
		}

		if (Settings->bEnableLoadBearing)
		{
			LoadBearingThresholdBuffer = Settings->LoadBearingThreshold.GetValueSetting();
			if (!LoadBearingThresholdBuffer->Init(PointDataFacade)) { return false; }
		}

		if (Settings->bRequireSupport)
		{
			MinSupportRatioBuffer = Settings->MinSupportRatio.GetValueSetting();
			if (!MinSupportRatioBuffer->Init(PointDataFacade)) { return false; }
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		Fitted.Init(false, NumPoints);
		Bins.Reserve(TargetBins->GetNum());

		// Setup seed getter
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

		// Setup bin max weight buffer (reads from bin points)
		TSharedPtr<PCGExDetails::TSettingValue<double>> BinMaxWeightBuffer;
		if (Settings->bEnableWeightConstraint)
		{
			PCGEX_MAKE_SHARED(BinFacade, PCGExData::FFacade, TargetBins.ToSharedRef())
			BinMaxWeightBuffer = Settings->BinMaxWeight.GetValueSetting();
			if (!BinMaxWeightBuffer->Init(BinFacade)) { return false; }
		}

		PCGExArrayHelpers::ArrayOfIndices(ProcessingOrder, NumPoints);

		// Sort by volume if enabled (Best-Fit Decreasing approach)
		if (Settings->bSortByVolume)
		{
			const UPCGBasePointData* InPoints = PointDataFacade->GetIn();

			TArray<double> Volumes;
			Volumes.SetNum(NumPoints);

			for (int32 i = 0; i < NumPoints; i++)
			{
				const FVector Size = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(PCGExData::FConstPoint(InPoints, i)).GetSize();
				Volumes[i] = Size.X * Size.Y * Size.Z;
			}

			if (Settings->SortDirection == EPCGExSortDirection::Descending)
			{
				ProcessingOrder.Sort([&Volumes](const int32 A, const int32 B) { return Volumes[A] > Volumes[B]; });
			}
			else
			{
				ProcessingOrder.Sort([&Volumes](const int32 A, const int32 B) { return Volumes[A] < Volumes[B]; });
			}
		}
		else if (Sorter && Sorter->Init(Context))
		{
			if (TSharedPtr<PCGExSorting::FSortCache> Cache = Sorter->BuildCache(NumPoints))
			{
				ProcessingOrder.Sort([&](const int32 A, const int32 B) { return Cache->Compare(A, B); });
			}
			else
			{
				ProcessingOrder.Sort([&](const int32 A, const int32 B) { return Sorter->Sort(A, B); });
			}
		}

		// Create bins
		BinMaxWeights.SetNum(TargetBins->GetNum());
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

			PCGEX_MAKE_SHARED(NewBin, FBP3DBin, i, BinPoint, Seed)

			NewBin->bAbsolutePadding = Settings->bAbsolutePadding;

			// Set bin max weight
			if (BinMaxWeightBuffer)
			{
				NewBin->MaxWeight = BinMaxWeightBuffer->Read(i);
			}
			else
			{
				NewBin->MaxWeight = MAX_dbl;
			}

			BinMaxWeights[i] = NewBin->MaxWeight;
			Bins.Add(NewBin);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BinPacking3D::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 PointIndex = ProcessingOrder[Index];
			PCGExData::FMutablePoint Point(OutPointData, PointIndex);

			const FVector PointSize = PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point).GetSize();

			FBP3DItem Item;
			Item.Index = PointIndex;
			Item.Box = FBox(FVector::ZeroVector, PointSize);
			Item.OriginalSize = PointSize;
			Item.Padding = PaddingBuffer->Read(PointIndex);
			Item.Weight = ItemWeightBuffer ? ItemWeightBuffer->Read(PointIndex) : 0.0;
			Item.Category = CategoryBuffer ? CategoryBuffer->Read(PointIndex) : -1;
			Item.LoadBearingThreshold = LoadBearingThresholdBuffer ? LoadBearingThresholdBuffer->Read(PointIndex) : 1.0;
			Item.MinSupportRatio = MinSupportRatioBuffer ? MinSupportRatioBuffer->Read(PointIndex) : 0.0;

			FBP3DPlacementCandidate BestPlacement = FindBestPlacement(Item);

			bool bPlaced = false;
			if (BestPlacement.IsValid())
			{
				TSharedPtr<FBP3DBin>& Bin = Bins[BestPlacement.BinIndex];
				Bin->CommitPlacement(BestPlacement, Item);
				Bin->UpdatePoint(Point, Item);
				bPlaced = true;
			}

			Fitted[PointIndex] = bPlaced;
			if (!bPlaced) { bHasUnfitted = true; }
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

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
