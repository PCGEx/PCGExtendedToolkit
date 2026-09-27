// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExBoundsAxisToPoints.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExMathAxis.h"
#include "Math/PCGExMathBounds.h"


#define LOCTEXT_NAMESPACE "PCGExBoundsAxisToPointsElement"
#define PCGEX_NAMESPACE BoundsAxisToPoints

PCGEX_INITIALIZE_ELEMENT(BoundsAxisToPoints)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BoundsAxisToPoints)

bool FPCGExBoundsAxisToPointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BoundsAxisToPoints)

	return true;
}

bool FPCGExBoundsAxisToPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBoundsAxisToPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BoundsAxisToPoints)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Missing data."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBoundsAxisToPoints
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBoundsAxisToPoints::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->bGeneratePerPointData ? PCGExData::EIOInit::NoInit : PCGExData::EIOInit::Duplicate)

		bSetExtents = Settings->bSetExtents;
		Extents = Settings->Extents;

		bSetScale = Settings->bSetScale;
		Scale = Settings->Scale;

		PointAttributesToOutputTags = Settings->PointAttributesToOutputTags;
		if (!PointAttributesToOutputTags.Init(ExecutionContext, PointDataFacade))
		{
			return false;
		}

		NumPoints = PointDataFacade->GetNum();
		bGeneratePerPointData = Settings->bGeneratePerPointData;

		if (bGeneratePerPointData)
		{
			NewOutputs.SetNum(PointDataFacade->GetNum());
			for (int i = 0; i < NewOutputs.Num(); i++)
			{
				NewOutputs[i] = Context->MainPoints->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::New);

				// Inputs emplace concurrently: stage by input, then by point.
				if (NewOutputs[i]) { NewOutputs[i]->SetSortKey(PointDataFacade->Source->IOIndex, i); }
			}
		}
		else
		{
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointDataFacade->GetOut(), NumPoints * 2);
			PointDataFacade->Source->InheritProperties(0, NumPoints, NumPoints);
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BoundsAxisToPoints::ProcessPoints);

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		TConstPCGValueRange<FTransform> InTransforms = PointIO->GetIn()->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExData::FConstPoint Point = PointIO->GetInPoint(Index);

			const FVector E = PCGExMath::GetLocalBounds(PointIO->GetInPoint(Index), Settings->BoundsReference).GetExtent();
			PCGExMath::FPCGExConstantUVW UVW;
			UVW.BoundsReference = Settings->BoundsReference;

			EPCGExMinimalAxis Axis = EPCGExMinimalAxis::None;

			FQuat R = InTransforms[Index].GetRotation();
			int32 Idx = -1;
			int32 Indices[3] = {0, 1, 2};
			int32 DotsIndices[3] = {0, 1, 2};

			const FVector Direction[3] = {R.GetAxisX(), R.GetAxisY(), R.GetAxisZ()};
			const double Size[3] = {E.X, E.Y, E.Z};
			constexpr EPCGExMinimalAxis AxisEnum[3] = {EPCGExMinimalAxis::X, EPCGExMinimalAxis::Y, EPCGExMinimalAxis::Z};
			double Dots[3] = {0, 0, 0};

			if (Settings->DirectionConstraint != EPCGExAxisDirectionConstraint::None)
			{
				for (int i = 0; i < 3; i++)
				{
					Dots[i] = FVector::DotProduct(Direction[i], Settings->Direction);
				}
				// Sort with index tie-breaker for determinism when dot products are equal
				Algo::Sort(DotsIndices, [&](const int32 A, const int32 B)
				{
					if (Dots[A] != Dots[B])
					{
						return Dots[A] < Dots[B];
					}
					return A < B;
				});
			}

			// Sort with index tie-breaker for determinism when sizes are equal
			Algo::Sort(Indices, [&](const int32 A, const int32 B)
			{
				if (Size[A] != Size[B])
				{
					return Size[A] < Size[B];
				}
				return A < B;
			});

			switch (Settings->Priority)
			{
			case EPCGExBoundAxisPriority::Shortest:
				Idx = 0;
				break;
			case EPCGExBoundAxisPriority::Median:
				Idx = 1;
				break;
			case EPCGExBoundAxisPriority::Longest:
				Idx = 2;
				break;
			}

			auto ApplySizeConstraint = [&]()
			{
				if (Settings->SizeConstraint == EPCGExAxisSizeConstraint::Greater)
				{
					for (int i = FMath::Clamp(Idx, 0, 2); i < 3; i++)
					{
						if (Size[Indices[i]] < Settings->SizeThreshold)
						{
							Idx++;
						}
					}
				}
				else
				{
					for (int i = FMath::Clamp(Idx, 0, 2); i >= 0; i--)
					{
						if (Size[Indices[i]] > Settings->SizeThreshold)
						{
							Idx--;
						}
					}
				}
			};

			auto ApplyDirectionConstraint = [&]()
			{
				if (Settings->DirectionConstraint == EPCGExAxisDirectionConstraint::Avoid)
				{
					// If selected direction is the worst
					// Back off
					if (Indices[FMath::Clamp(Idx, 0, 2)] == DotsIndices[2])
					{
						Idx = 1;
					}
				}
				else
				{
					// If selected direction is not the best
					// Nudge toward it
					if (Indices[FMath::Clamp(Idx, 0, 2)] != DotsIndices[2])
					{
						Idx++;
					}
				}
			};

			if (Settings->ConstraintsOrder == EPCGExAxisConstraintSorting::SizeMatters)
			{
				if (Settings->DirectionConstraint != EPCGExAxisDirectionConstraint::None)
				{
					ApplyDirectionConstraint();
				}
				if (Settings->SizeConstraint != EPCGExAxisSizeConstraint::None)
				{
					ApplySizeConstraint();
				}
			}
			else
			{
				if (Settings->SizeConstraint != EPCGExAxisSizeConstraint::None)
				{
					ApplySizeConstraint();
				}
				if (Settings->DirectionConstraint != EPCGExAxisDirectionConstraint::None)
				{
					ApplyDirectionConstraint();
				}
			}

			Axis = AxisEnum[Indices[FMath::Clamp(Idx, 0, 2)]];

			switch (Axis)
			{
			case EPCGExMinimalAxis::None:
			case EPCGExMinimalAxis::X:
				UVW.U = Settings->U;
				break;
			case EPCGExMinimalAxis::Y:
				UVW.V = Settings->U;
				break;
			case EPCGExMinimalAxis::Z:
				UVW.W = Settings->U;
				break;
			}

			if (bGeneratePerPointData)
			{
				// TODO : Revisit this, it's a terrible way to work with point arrays				
				const TSharedPtr<PCGExData::FPointIO>& NewOutput = NewOutputs[Index];

				int32 A;
				int32 B;

				NewOutput->CopyToNewPoint(Index, A);
				NewOutput->CopyToNewPoint(Index, B);

				// A and B get distinct transforms even when the input's is uniform (unallocated).
				NewOutput->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

				TPCGValueRange<FTransform> Transforms = NewOutput->GetOut()->GetTransformValueRange(false);
				TPCGValueRange<FVector> BoundsMin = NewOutput->GetOut()->GetBoundsMinValueRange(false);
				TPCGValueRange<FVector> BoundsMax = NewOutput->GetOut()->GetBoundsMaxValueRange(false);

				if (bSetExtents)
				{
					BoundsMin[A] = -Extents;
					BoundsMin[B] = -Extents;

					BoundsMax[A] = Extents;
					BoundsMax[B] = Extents;
				}

				Transforms[A].SetLocation(UVW.GetPosition(Point));
				Transforms[B].SetLocation(UVW.GetPosition(Point, Axis, true));

				if (bSetScale)
				{
					Transforms[A].SetScale3D(Scale);
					Transforms[B].SetScale3D(Scale);
				}

				PointAttributesToOutputTags.Tag(Point, NewOutput);
			}
			else
			{
				TPCGValueRange<FTransform> Transforms = PointIO->GetOut()->GetTransformValueRange(false);
				TPCGValueRange<FVector> BoundsMin = PointIO->GetOut()->GetBoundsMinValueRange(false);
				TPCGValueRange<FVector> BoundsMax = PointIO->GetOut()->GetBoundsMaxValueRange(false);

				int32 A = Index;
				int32 B = NumPoints + A;

				if (bSetExtents)
				{
					BoundsMin[A] = -Extents;
					BoundsMin[B] = -Extents;

					BoundsMax[A] = Extents;
					BoundsMax[B] = Extents;
				}

				Transforms[A].SetLocation(UVW.GetPosition(Point));
				Transforms[B].SetLocation(UVW.GetPosition(Point, Axis, true));

				if (bSetScale)
				{
					Transforms[A].SetScale3D(Scale);
					Transforms[B].SetScale3D(Scale);
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (!bGeneratePerPointData)
		{
			PointDataFacade->Source->InitializeMetadataEntries_Unsafe(false);
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
