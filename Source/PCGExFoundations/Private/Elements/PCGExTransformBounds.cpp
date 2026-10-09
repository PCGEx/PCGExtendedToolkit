// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTransformBounds.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataMacros.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGPointHelpers.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExTransformBoundsElement"
#define PCGEX_NAMESPACE TransformBounds

PCGEX_INITIALIZE_ELEMENT(TransformBounds)

PCGExData::EIOInit UPCGExTransformBoundsSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(TransformBounds)

bool FPCGExTransformBoundsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTransformBoundsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TransformBounds)
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
			return Context->CancelExecution(TEXT("No data."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExTransformBounds
{
	// Zero extents are not valid point bounds; every mutated point is clamped to this floor.
	constexpr double MinExtent = UE_DOUBLE_KINDA_SMALL_NUMBER;

	void ApplyScaleToBounds(const bool (&Axes)[3], FTransform& InOutTransform, FVector& InOutBoundsMin, FVector& InOutBoundsMax)
	{
		FVector Scale = InOutTransform.GetScale3D();
		for (int32 A = 0; A < 3; A++)
		{
			if (!Axes[A])
			{
				continue;
			}

			const double Magnitude = FMath::Abs(Scale[A]);
			InOutBoundsMin[A] *= Magnitude;
			InOutBoundsMax[A] *= Magnitude;
			Scale[A] = FMath::FloatSelect(Scale[A], 1.0, -1.0);
		}
		InOutTransform.SetScale3D(Scale);
	}

	// Keeps the world-space box unchanged: Min/Max scale by Ref/Extents and the point scale by Extents/Ref.
	void ApplyBoundsToScale(const bool (&Axes)[3], const FVector& InReference, const FVector& InExtents, FTransform& InOutTransform, FVector& InOutBoundsMin, FVector& InOutBoundsMax)
	{
		FVector Scale = InOutTransform.GetScale3D();
		for (int32 A = 0; A < 3; A++)
		{
			const double Reference = FMath::Abs(InReference[A]);
			if (!Axes[A] || Reference < MinExtent)
			{
				continue;
			}

			const double Extent = InExtents[A];
			const double Ratio = Reference / Extent;
			InOutBoundsMin[A] *= Ratio;
			InOutBoundsMax[A] *= Ratio;
			Scale[A] *= Extent / Reference;
		}
		InOutTransform.SetScale3D(Scale);
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTransformBounds::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		switch (Settings->PostMutation)
		{
		case EPCGExBoundsPostMutation::None:
			break;
		case EPCGExBoundsPostMutation::ScaleToBounds:
			bScaleToBounds = true;
			break;
		case EPCGExBoundsPostMutation::BoundsToScale:
			bBoundsToScale = true;
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Unknown post mutation."));
			return false;
		}

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax;
		if (bScaleToBounds || bBoundsToScale)
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
			bAxes[0] = (Settings->Components & static_cast<uint8>(EPCGExApplySampledComponentFlags::X)) != 0;
			bAxes[1] = (Settings->Components & static_cast<uint8>(EPCGExApplySampledComponentFlags::Y)) != 0;
			bAxes[2] = (Settings->Components & static_cast<uint8>(EPCGExApplySampledComponentFlags::Z)) != 0;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		bScaleMode = Settings->Mode == EPCGExBoundsVariationMode::Scale;

		if (bScaleMode)
		{
			ScaleMin = Settings->ScaleMin.GetValueSetting();
			if (!ScaleMin->Init(PointDataFacade))
			{
				return false;
			}

			ScaleMax = Settings->ScaleMax.GetValueSetting();
			if (!ScaleMax->Init(PointDataFacade))
			{
				return false;
			}

			ScaleScale = Settings->ScaleScaling.GetValueSetting();
			if (!ScaleScale->Init(PointDataFacade))
			{
				return false;
			}

			ScaleSnap = Settings->ScaleSnap.GetValueSetting();
			if (!ScaleSnap->Init(PointDataFacade))
			{
				return false;
			}

			UniformScale = Settings->UniformScale.GetValueSetting();
			if (!UniformScale->Init(PointDataFacade))
			{
				return false;
			}
		}
		else
		{
			OffsetMin = Settings->OffsetMin.GetValueSetting();
			if (!OffsetMin->Init(PointDataFacade))
			{
				return false;
			}

			OffsetMax = Settings->OffsetMax.GetValueSetting();
			if (!OffsetMax->Init(PointDataFacade))
			{
				return false;
			}

			OffsetScale = Settings->OffsetScaling.GetValueSetting();
			if (!OffsetScale->Init(PointDataFacade))
			{
				return false;
			}

			OffsetSnap = Settings->OffsetSnap.GetValueSetting();
			if (!OffsetSnap->Init(PointDataFacade))
			{
				return false;
			}
		}

		if (bBoundsToScale)
		{
			ReferenceExtents = Settings->ReferenceExtents.GetValueSetting();
			if (!ReferenceExtents->Init(PointDataFacade))
			{
				return false;
			}
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::TransformBounds::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SV_VIEW_COND(OffsetMin, !bScaleMode)
		PCGEX_SV_VIEW_COND(OffsetMax, !bScaleMode)
		PCGEX_SV_VIEW_COND(OffsetScale, !bScaleMode)
		PCGEX_SV_VIEW_COND(OffsetSnap, !bScaleMode)

		PCGEX_SV_VIEW_COND(ScaleMin, bScaleMode)
		PCGEX_SV_VIEW_COND(ScaleMax, bScaleMode)
		PCGEX_SV_VIEW_COND(ScaleScale, bScaleMode)
		PCGEX_SV_VIEW_COND(ScaleSnap, bScaleMode)
		PCGEX_SV_VIEW_COND(UniformScale, bScaleMode)

		PCGEX_SV_VIEW_COND(ReferenceExtents, bBoundsToScale)

		TConstPCGValueRange<int32> Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();
		TPCGValueRange<FVector> OutBoundsMin = PointDataFacade->GetOut()->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> OutBoundsMax = PointDataFacade->GetOut()->GetBoundsMaxValueRange(false);

		const bool bPostMutate = bScaleToBounds || bBoundsToScale;
		TPCGValueRange<FTransform> OutTransforms = bPostMutate ? PointDataFacade->GetOut()->GetTransformValueRange(false) : TPCGValueRange<FTransform>();

		const EPCGExVariationSnapping Snapping = bScaleMode ? Settings->SnapScale : Settings->SnapOffset;
		const FVector MinExtents = FVector(MinExtent);

		FRandomStream RandomSource;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const int32 i = Index - Scope.Start;

			RandomSource.Initialize(Seeds[Index]);

			FVector& BoundsMin = OutBoundsMin[Index];
			FVector& BoundsMax = OutBoundsMax[Index];
			FVector Extents = PCGPointHelpers::GetExtents(BoundsMin, BoundsMax);

			if (bScaleMode)
			{
				const FVector& Scaling = PCGEX_SV_READ(ScaleScale, i);
				const FVector Min = PCGEX_SV_READ(ScaleMin, i) * Scaling;
				const FVector Max = PCGEX_SV_READ(ScaleMax, i) * Scaling;
				const FVector& Step = PCGEX_SV_READ(ScaleSnap, i);

				FVector Factor;
				if (PCGEX_SV_READ(UniformScale, i))
				{
					Factor = FVector(RandomSource.FRandRange(Min.X, Max.X));
				}
				else
				{
					Factor = FVector(RandomSource.FRandRange(Min.X, Max.X), RandomSource.FRandRange(Min.Y, Max.Y), RandomSource.FRandRange(Min.Z, Max.Z));
				}

				if (Snapping == EPCGExVariationSnapping::SnapOffset)
				{
					PCGExMath::Snap(Factor, Step);
				}

				Extents *= Factor;

				if (Snapping == EPCGExVariationSnapping::SnapResult)
				{
					PCGExMath::Snap(Extents, Step);
				}
			}
			else
			{
				const FVector& Scaling = PCGEX_SV_READ(OffsetScale, i);
				const FVector Min = PCGEX_SV_READ(OffsetMin, i) * Scaling;
				const FVector Max = PCGEX_SV_READ(OffsetMax, i) * Scaling;
				const FVector& Step = PCGEX_SV_READ(OffsetSnap, i);

				FVector Offset = FVector(RandomSource.FRandRange(Min.X, Max.X), RandomSource.FRandRange(Min.Y, Max.Y), RandomSource.FRandRange(Min.Z, Max.Z));

				if (Snapping == EPCGExVariationSnapping::SnapOffset)
				{
					PCGExMath::Snap(Offset, Step);
				}

				Extents += Offset;

				if (Snapping == EPCGExVariationSnapping::SnapResult)
				{
					PCGExMath::Snap(Extents, Step);
				}
			}

			Extents = Extents.ComponentMax(MinExtents);
			PCGPointHelpers::SetExtents(Extents, BoundsMin, BoundsMax);

			if (bScaleToBounds)
			{
				ApplyScaleToBounds(bAxes, OutTransforms[Index], BoundsMin, BoundsMax);
			}
			else if (bBoundsToScale)
			{
				ApplyBoundsToScale(bAxes, PCGEX_SV_READ(ReferenceExtents, i), Extents, OutTransforms[Index], BoundsMin, BoundsMax);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
