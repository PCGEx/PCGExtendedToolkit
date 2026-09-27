// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/IntegralConstant.h"
#include "PCGExFittingCommon.generated.h"

UENUM(BlueprintType)
enum class EPCGExVariationSnapping : uint8
{
	None       = 0 UMETA(DisplayName = "No Snapping", ToolTip="No Snapping", ActionIcon="NoSnapping"),
	SnapOffset = 1 UMETA(DisplayName = "Snap Offset", ToolTip="Snap Offset (the variation value will be snapped, not the result)", ActionIcon="SnapOffset"),
	SnapResult = 2 UMETA(DisplayName = "Snap Result", ToolTip="Snap Result (the variation will not be snapped but the final result will)", ActionIcon="SnapResult"),
};

UENUM(BlueprintType)
enum class EPCGExFitMode : uint8
{
	None       = 0 UMETA(DisplayName = "None", ToolTip="No fitting", ActionIcon="STF_None"),
	Uniform    = 1 UMETA(DisplayName = "Uniform", ToolTip="Uniform fit", ActionIcon="STF_Uniform"),
	Individual = 2 UMETA(DisplayName = "Individual", ToolTip="Per-component fit", ActionIcon="STF_Individual"),
};

UENUM(BlueprintType)
enum class EPCGExScaleToFit : uint8
{
	None = 0 UMETA(DisplayName = "None", ToolTip="No fitting", ActionIcon="Fit_None"),
	Fill = 1 UMETA(DisplayName = "Fill", ToolTip="Fill", ActionIcon="Fit_Fill"),
	Min  = 2 UMETA(DisplayName = "Min", ToolTip="Min", ActionIcon="Fit_Min"),
	Max  = 3 UMETA(DisplayName = "Max", ToolTip="Max", ActionIcon="Fit_Max"),
	Avg  = 4 UMETA(DisplayName = "Average", ToolTip="Average", ActionIcon="Fit_Average"),
};

UENUM(BlueprintType)
enum class EPCGExJustifyFrom : uint8
{
	Min    = 0 UMETA(DisplayName = "Min", ToolTip="Min", ActionIcon="From_Min"),
	Center = 1 UMETA(DisplayName = "Center", ToolTip="Center", ActionIcon="From_Center"),
	Max    = 2 UMETA(DisplayName = "Max", ToolTip="Max", ActionIcon="From_Max"),
	Pivot  = 3 UMETA(DisplayName = "Pivot", ToolTip="Pivot", ActionIcon="From_Pivot"),
	Custom = 4 UMETA(DisplayName = "Custom", ToolTip="Custom", ActionIcon="From_Custom"),
};

UENUM(BlueprintType)
enum class EPCGExJustifyTo : uint8
{
	Min    = 1 UMETA(DisplayName = "Min", ToolTip="Min", ActionIcon="To_Min"),
	Center = 2 UMETA(DisplayName = "Center", ToolTip="Center", ActionIcon="To_Center"),
	Max    = 3 UMETA(DisplayName = "Max", ToolTip="Max", ActionIcon="To_Max"),
	Pivot  = 4 UMETA(DisplayName = "Pivot", ToolTip="Pivot", ActionIcon="To_Pivot"),
	Custom = 5 UMETA(DisplayName = "Custom", ToolTip="Custom", ActionIcon="To_Custom"),
	Same   = 0 UMETA(DisplayName = "Same", ToolTip="Same as 'From'", ActionIcon="To_Same"),
};

UENUM(BlueprintType)
enum class EPCGExVariationMode : uint8
{
	Disabled = 0 UMETA(DisplayName = "Disabled", ToolTip="Disabled", ActionIcon="STF_None"),
	Before   = 1 UMETA(DisplayName = "Before fitting", ToolTip="Pre-processing.\nVariation are applied to the asset before it will be fitted to the host point.", ActionIcon="BeforeStaging"),
	After    = 2 UMETA(DisplayName = "After fitting", ToolTip="Post-processing.\nVariation are applied to the host point after the asset has been fitted inside.", ActionIcon="AfterStaging"),
};

namespace PCGExFitting
{
	/** Strategy index for ApplyInheritedTransform from the inherit flags. */
	FORCEINLINE int32 GetInheritStrategy(const bool bInheritRotation, const bool bInheritScale)
	{
		return (bInheritRotation ? 2 : 0) + (bInheritScale ? 1 : 0);
	}

	/** Applies a target transform to one point transform; the one implementation of the inherit strategies. */
	template <int32 Strategy>
	FORCEINLINE void ApplyInheritedTransform(FTransform& InOutTransform, const FTransform& InTarget)
	{
		if constexpr (Strategy == 3)
		{
			InOutTransform *= InTarget;
		}
		else if constexpr (Strategy == 2)
		{
			const FVector OriginalScale = InOutTransform.GetScale3D();
			InOutTransform *= InTarget;
			InOutTransform.SetScale3D(OriginalScale);
		}
		else if constexpr (Strategy == 1)
		{
			const FQuat OriginalRotation = InOutTransform.GetRotation();
			InOutTransform *= InTarget;
			InOutTransform.SetRotation(OriginalRotation);
		}
		else
		{
			InOutTransform.SetLocation(InTarget.TransformPosition(InOutTransform.GetLocation()));
		}
	}

	/** Calls InFunc with the strategy as a TIntegralConstant, so per-point loops branch once instead of per point. */
	template <typename FuncType>
	FORCEINLINE void DispatchInheritStrategy(const int32 InStrategy, FuncType&& InFunc)
	{
		switch (InStrategy)
		{
		case 3:
			InFunc(TIntegralConstant<int32, 3>{});
			break;
		case 2:
			InFunc(TIntegralConstant<int32, 2>{});
			break;
		case 1:
			InFunc(TIntegralConstant<int32, 1>{});
			break;
		default:
			InFunc(TIntegralConstant<int32, 0>{});
			break;
		}
	}
}
