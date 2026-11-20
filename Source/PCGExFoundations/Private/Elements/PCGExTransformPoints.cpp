// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTransformPoints.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataMacros.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Fitting/PCGExFittingVariations.h"

#define LOCTEXT_NAMESPACE "PCGExTransformPointsElement"
#define PCGEX_NAMESPACE TransformPoints

PCGEX_INITIALIZE_ELEMENT(TransformPoints)

PCGExData::EIOInit UPCGExTransformPointsSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(TransformPoints)

bool FPCGExTransformPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTransformPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TransformPoints)
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

namespace PCGExTransformPoints
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTransformPoints::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		// Cherry pick native properties allocations

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		AllocateFor |= EPCGPointNativeProperties::Transform;

		bApplyScaleToBounds = Settings->bApplyScaleToBounds;
		bResetPointCenter = Settings->bResetPointCenter;
		if (bApplyScaleToBounds || bResetPointCenter)
		{
			bAllocatedBounds = true;
			AllocateFor |= EPCGPointNativeProperties::BoundsMin;
			AllocateFor |= EPCGPointNativeProperties::BoundsMax;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

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

		AbsoluteOffset = Settings->AbsoluteOffset.GetValueSetting();
		if (!AbsoluteOffset->Init(PointDataFacade))
		{
			return false;
		}


		RotMin = Settings->RotationMin.GetValueSetting();
		if (!RotMin->Init(PointDataFacade))
		{
			return false;
		}

		RotMax = Settings->RotationMax.GetValueSetting();
		if (!RotMax->Init(PointDataFacade))
		{
			return false;
		}

		RotScale = Settings->RotationScaling.GetValueSetting();
		if (!RotScale->Init(PointDataFacade))
		{
			return false;
		}

		RotSnap = Settings->RotationSnap.GetValueSetting();
		if (!RotSnap->Init(PointDataFacade))
		{
			return false;
		}


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


		if (bResetPointCenter)
		{
			PointCenter = Settings->PointCenterLocation.GetValueSetting();
			if (!PointCenter->Init(PointDataFacade))
			{
				return false;
			}
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::TransformPoints::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SV_VIEW(OffsetMin)
		PCGEX_SV_VIEW(OffsetMax)
		PCGEX_SV_VIEW(OffsetScale)
		PCGEX_SV_VIEW(OffsetSnap)
		PCGEX_SV_VIEW(AbsoluteOffset)
		PCGEX_SV_VIEW(RotMin)
		PCGEX_SV_VIEW(RotMax)
		PCGEX_SV_VIEW(RotScale)
		PCGEX_SV_VIEW(RotSnap)
		PCGEX_SV_VIEW(ScaleMin)
		PCGEX_SV_VIEW(ScaleMax)
		PCGEX_SV_VIEW(ScaleScale)
		PCGEX_SV_VIEW(ScaleSnap)
		PCGEX_SV_VIEW(UniformScale)

		PCGEX_SV_VIEW_COND(PointCenter, bResetPointCenter)

		TConstPCGValueRange<int32> Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();
		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);

		TPCGValueRange<FVector> OutBoundsMin = bAllocatedBounds ? PointDataFacade->GetOut()->GetBoundsMinValueRange(false) : TPCGValueRange<FVector>();
		TPCGValueRange<FVector> OutBoundsMax = bAllocatedBounds ? PointDataFacade->GetOut()->GetBoundsMaxValueRange(false) : TPCGValueRange<FVector>();

		FPCGExFittingVariations Variations;
		Variations.SnapPosition = Settings->SnapPosition;
		Variations.SnapRotation = Settings->SnapRotation;
		Variations.SnapScale = Settings->SnapScale;
		Variations.AbsoluteRotation = Settings->AbsoluteRotation;

		FRandomStream RandomSource;

		const bool bResetScale = Settings->bResetScale;
		const bool bResetRotation = Settings->bResetRotation;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const int32 i = Index - Scope.Start;

			RandomSource.Initialize(Seeds[Index]);

			FTransform& OutTransform = OutTransforms[Index];
			if (bResetScale)
			{
				OutTransform.SetScale3D(FVector::OneVector);
			}
			if (bResetRotation)
			{
				OutTransform.SetRotation(FQuat::Identity);
			}

			const FVector& OffsetScaleV = PCGEX_SV_READ(OffsetScale, i);
			Variations.OffsetMin = PCGEX_SV_READ(OffsetMin, i) * OffsetScaleV;
			Variations.OffsetMax = PCGEX_SV_READ(OffsetMax, i) * OffsetScaleV;
			Variations.OffsetSnap = PCGEX_SV_READ(OffsetSnap, i);
			Variations.bAbsoluteOffset = PCGEX_SV_READ(AbsoluteOffset, i);

			const FVector& RotScaleV = PCGEX_SV_READ(RotScale, i);
			const FRotator& RMin = PCGEX_SV_READ(RotMin, i);
			const FRotator& RMax = PCGEX_SV_READ(RotMax, i);
			Variations.RotationMin = FRotator(RMin.Pitch * RotScaleV.Y, RMin.Yaw * RotScaleV.Z, RMin.Roll * RotScaleV.X);
			Variations.RotationMax = FRotator(RMax.Pitch * RotScaleV.Y, RMax.Yaw * RotScaleV.Z, RMax.Roll * RotScaleV.X);
			Variations.RotationSnap = PCGEX_SV_READ(RotSnap, i);

			const FVector& ScaleScaleV = PCGEX_SV_READ(ScaleScale, i);
			Variations.ScaleMin = PCGEX_SV_READ(ScaleMin, i) * ScaleScaleV;
			Variations.ScaleMax = PCGEX_SV_READ(ScaleMax, i) * ScaleScaleV;
			Variations.ScaleSnap = PCGEX_SV_READ(ScaleSnap, i);
			Variations.bUniformScale = PCGEX_SV_READ(UniformScale, i);

			Variations.ApplyOffset(RandomSource, OutTransform);
			Variations.ApplyRotation(RandomSource, OutTransform);
			Variations.ApplyScale(RandomSource, OutTransform);

			if (bApplyScaleToBounds)
			{
				PCGPointHelpers::ApplyScaleToBounds(OutTransform, OutBoundsMin[Index], OutBoundsMax[Index]);
			}

			if (bResetPointCenter)
			{
				PCGPointHelpers::ResetPointCenter(PCGEX_SV_READ(PointCenter, i), OutTransform, OutBoundsMin[Index], OutBoundsMax[Index]);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
