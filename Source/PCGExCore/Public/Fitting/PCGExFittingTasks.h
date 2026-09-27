// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMT.h"

namespace PCGExData
{
	class FPointIO;
}

struct FPCGExTransformDetails;

class UPCGBasePointData;

namespace PCGExFitting::Tasks
{
	/** Untransformed bounds FTransformPointIO fits against: local point bounds, or bare locations when bIgnoreBounds. */
	PCGEXCORE_API FBox ComputeFitBounds(const UPCGBasePointData* InPointData, const bool bIgnoreBounds);

	class PCGEXCORE_API FTransformPointIO final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FTransformPointIO(const int32 InTaskIndex, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<PCGExData::FPointIO>& InToBeTransformedIO, FPCGExTransformDetails* InTransformDetails, bool bAllocate = false);

		// Fits against InFitBounds, not the output's own bounds: a cluster's Vtx and Edges must share one fit.
		FTransformPointIO(const int32 InTaskIndex, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<PCGExData::FPointIO>& InToBeTransformedIO, FPCGExTransformDetails* InTransformDetails, const FBox& InFitBounds);

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<PCGExData::FPointIO> ToBeTransformedIO;
		FPCGExTransformDetails* TransformDetails = nullptr;

		FBox FitBounds = FBox(ForceInit);
		bool bHasFitBounds = false;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
	};
}
