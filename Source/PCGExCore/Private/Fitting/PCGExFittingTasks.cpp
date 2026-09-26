// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Fitting/PCGExFittingTasks.h"

#include "CoreMinimal.h"
#include "Data/PCGExPointIO.h"
#include "Fitting/PCGExFitting.h"

namespace PCGExFitting::Tasks
{
	FBox ComputeFitBounds(const UPCGBasePointData* InPointData, const bool bIgnoreBounds)
	{
		const TConstPCGValueRange<FTransform> Transforms = InPointData->GetConstTransformValueRange();
		const int32 NumPoints = Transforms.Num();

		FBox Bounds = FBox(ForceInit);

		if (!bIgnoreBounds)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				Bounds += InPointData->GetLocalBounds(i).TransformBy(Transforms[i]);
			}
		}
		else
		{
			for (int i = 0; i < NumPoints; i++)
			{
				Bounds += Transforms[i].GetLocation();
			}
		}

		return Bounds;
	}

	FTransformPointIO::FTransformPointIO(const int32 InTaskIndex, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<PCGExData::FPointIO>& InToBeTransformedIO, FPCGExTransformDetails* InTransformDetails, bool bAllocate)
		: FPCGExIndexedTask(InTaskIndex)
		  , PointIO(InPointIO)
		  , ToBeTransformedIO(InToBeTransformedIO)
		  , TransformDetails(InTransformDetails)
	{
	}

	FTransformPointIO::FTransformPointIO(const int32 InTaskIndex, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<PCGExData::FPointIO>& InToBeTransformedIO, FPCGExTransformDetails* InTransformDetails, const FBox& InFitBounds)
		: FPCGExIndexedTask(InTaskIndex)
		  , PointIO(InPointIO)
		  , ToBeTransformedIO(InToBeTransformedIO)
		  , TransformDetails(InTransformDetails)
		  , FitBounds(InFitBounds)
		  , bHasFitBounds(true)
	{
	}

	void FTransformPointIO::ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
	{
		UPCGBasePointData* OutPointData = ToBeTransformedIO->GetOut();
		TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange();
		FTransform TargetTransform = FTransform::Identity;

		FBox PointBounds = bHasFitBounds ? FitBounds : ComputeFitBounds(OutPointData, TransformDetails->bIgnoreBounds);
		FVector Translation = FVector::ZeroVector;

		PointBounds = PointBounds.ExpandBy(0.1); // Avoid NaN
		TransformDetails->ComputeTransform(TaskIndex, TargetTransform, PointBounds, Translation);

		DispatchInheritStrategy(
			GetInheritStrategy(TransformDetails->bInheritRotation, TransformDetails->bInheritScale), [&](auto StrategyTag)
			{
				using FStrategy = decltype(StrategyTag);
				PCGEX_PARALLEL_FOR(
					OutTransforms.Num(),
					ApplyInheritedTransform<FStrategy::Value>(OutTransforms[i], TargetTransform);
					)
			});
	}
}
