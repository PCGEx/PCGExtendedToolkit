// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExOffsetPath.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/PCGExPathShift.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExOffsetPathElement"
#define PCGEX_NAMESPACE OffsetPath

#if WITH_EDITOR
void UPCGExOffsetPathSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 7)
	{
		// Rewire Offset
		PCGEX_SHORTHAND_RENAME_PIN(OffsetAttribute, OffsetConstant, Offset)

		// Rewire Direction
		PCGEX_SHORTHAND_RENAME_PIN(DirectionAttribute, UNDEFINED, Direction)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExOffsetPathSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 7)
	{
		Offset.Update(OffsetInput_DEPRECATED, OffsetAttribute_DEPRECATED, OffsetConstant_DEPRECATED);
		Direction.Update(DirectionType_DEPRECATED, DirectionAttribute_DEPRECATED, FVector::UpVector);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}

#endif

PCGEX_INITIALIZE_ELEMENT(OffsetPath)

PCGExData::EIOInit UPCGExOffsetPathSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(OffsetPath)

bool FPCGExOffsetPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(OffsetPath)

	return true;
}

bool FPCGExOffsetPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExOffsetPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(OffsetPath)

	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be affected."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				PCGEX_SKIP_INVALID_PATH_ENTRY
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				//NewBatch->SetPointsFilterData(&Context->FilterFactories);
			}))
		{
			Context->CancelExecution(TEXT("Could not find any paths to offset."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExOffsetPath
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExOffsetPath::Process);

		if (Settings->OffsetMethod == EPCGExOffsetMethod::Slide)
		{
			PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;
		}
		else
		{
			PointDataFacade->bSupportsScopedGet = false;
		}

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())
		PointDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

		if (Settings->bInvertDirection)
		{
			DirectionFactor *= -1;
		}

		InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		Up = Settings->UpVectorConstant.GetSafeNormal();
		OffsetConstant = Settings->OffsetConstant_DEPRECATED;

		Path = MakeShared<PCGExPaths::FPath>(InTransforms, PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn()), 0);

		if (Settings->OffsetMethod == EPCGExOffsetMethod::Slide)
		{
			if (Settings->Adjustment != EPCGExOffsetAdjustment::None)
			{
				PathAngles = Path->AddExtra<PCGExPaths::FPathEdgeHalfAngle>(false, Up);
			}
		}

		OffsetGetter = Settings->Offset.GetValueSetting();
		if (!OffsetGetter->Init(PointDataFacade))
		{
			return false;
		}

		OffsetScaleGetter = Settings->OffsetScale.GetValueSetting();
		if (!OffsetScaleGetter->Init(PointDataFacade))
		{
			return false;
		}

		if (Settings->DirectionConstant == EPCGExPathNormalDirection::Custom)
		{
			DirectionGetter = Settings->Direction.GetValueSetting();
			if (!DirectionGetter->Init(PointDataFacade))
			{
				return false;
			}
		}
		else
		{
			if (Settings->OffsetMethod == EPCGExOffsetMethod::LinePlane)
			{
				OffsetDirection = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeNormal>(true, Up));
			}
			else
			{
				switch (Settings->DirectionConstant)
				{
				default:
				case EPCGExPathNormalDirection::Normal:
					OffsetDirection = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeNormal>(false, Up));
					break;
				case EPCGExPathNormalDirection::Binormal:
					OffsetDirection = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeBinormal>(false, Up));
					break;
				case EPCGExPathNormalDirection::AverageNormal:
					OffsetDirection = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeAvgNormal>(false, Up));
					break;
				case EPCGExPathNormalDirection::ParallelTransport:
					OffsetDirection = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeParallelTransportNormal>(false, Up));
					break;
				}
			}
		}

		StartParallelLoopForPoints();
		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::OffsetPath::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 EdgeIndex = (!Path->IsClosedLoop() && Index == Path->LastIndex) ? Path->LastEdge : Index;
			Path->ComputeEdgeExtra(EdgeIndex);

			FVector Dir = (OffsetDirection ? OffsetDirection->Get(EdgeIndex) : DirectionGetter->Read(Index)) * DirectionFactor;
			double Offset = OffsetGetter->Read(Index) * OffsetScaleGetter->Read(Index);

			if (Settings->bApplyPointScaleToOffset)
			{
				Dir *= InTransforms[Index].GetScale3D();
			}

			if (Settings->OffsetMethod == EPCGExOffsetMethod::Slide)
			{
				if (PathAngles)
				{
					if (Settings->Adjustment == EPCGExOffsetAdjustment::SmoothCustom)
					{
						Offset *= (1 + Settings->AdjustmentScale * FMath::Cos(PathAngles->Get(EdgeIndex)));
					}
					else if (Settings->Adjustment == EPCGExOffsetAdjustment::SmoothAuto)
					{
						const double Dot = FMath::Clamp(FVector::DotProduct(Path->DirToPrevPoint(Index) * -1, Path->DirToNextPoint(Index)), -1, 0);
						Offset *= 1 + (FMath::Abs(Dot) * FMath::Acos(Dot)) * FMath::Abs(Dot);
					}
					else if (Settings->Adjustment == EPCGExOffsetAdjustment::Mitre)
					{
						double MitreLength = Offset / FMath::Sin(PathAngles->Get(EdgeIndex) / 2);
						if (MitreLength > Settings->MitreLimit * Offset)
						{
							Offset *= Settings->MitreLimit;
						} // Should bevel :(
					}
				}

				OutTransforms[Index].SetLocation(Path->GetPos_Unsafe(Index) + (Dir * Offset));
			}
			else
			{
				const int32 PrevIndex = Path->SafePointIndex(Index - 1);
				const FVector PlaneDir = ((OffsetDirection ? OffsetDirection->Get(PrevIndex) : DirectionGetter->Read(PrevIndex)) * DirectionFactor).GetSafeNormal();
				const FVector PlaneOrigin = Path->GetPos_Unsafe(PrevIndex) + (PlaneDir * Offset);

				const FVector A = Path->GetPos_Unsafe(Index) + (Dir * Offset);
				const FVector NextDir = Path->DirToNextPoint(Index);
				const double Denom = FMath::Abs(FVector::DotProduct(NextDir, PlaneDir));

				if (Denom < 0.01)
				{
					OutTransforms[Index].SetLocation(A);
				}
				else
				{
					const FVector Candidate = FMath::LinePlaneIntersection(A, A + NextDir * 10, PlaneOrigin, PlaneDir * -1);
					if (Candidate.ContainsNaN())
					{
						OutTransforms[Index].SetLocation(A);
					}
					else
					{
						OutTransforms[Index].SetLocation(Candidate);
					}
				}
			}

			if (!PointFilterCache[Index])
			{
				OutTransforms[Index].SetLocation(InTransforms[Index].GetLocation());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
