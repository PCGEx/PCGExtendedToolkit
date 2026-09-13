// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFittingCommon.h"

#include "PCGExFittingOverrides.generated.h"

namespace PCGExData
{
	struct FPoint;
}

namespace PCGExFitting
{
	/**
	 * Per-axis scale-to-fit factor resolution. Shared by the node-level details
	 * (FPCGExScaleToFitDetails) and the lean per-entry representation so the math has a
	 * single source of truth.
	 * MinMaxFit packs the three uniform options: X = Min, Y = Max, Z = Avg.
	 */
	FORCEINLINE void ScaleToFitAxis(const EPCGExScaleToFit Fit, const int32 Axis, const FVector& TargetScale, const FVector& TargetSize, const FVector& CandidateSize, const FVector& MinMaxFit, FVector& OutScale)
	{
		const double Scale = TargetScale[Axis];
		double FinalScale = Scale;

		switch (Fit)
		{
		default: case EPCGExScaleToFit::None:
			break;
		case EPCGExScaleToFit::Fill:
			FinalScale = ((TargetSize[Axis] * Scale) / CandidateSize[Axis]);
			break;
		case EPCGExScaleToFit::Min:
			FinalScale = MinMaxFit[0];
			break;
		case EPCGExScaleToFit::Max:
			FinalScale = MinMaxFit[1];
			break;
		case EPCGExScaleToFit::Avg:
			FinalScale = MinMaxFit[2];
			break;
		}

		OutScale[Axis] = FinalScale;
	}

	/**
	 * Full three-axis scale-to-fit pass over resolved mode/fit values.
	 * No-op when Mode == None: OutScale and OutBounds are left untouched.
	 */
	FORCEINLINE void ScaleToFitAxes(
		const EPCGExFitMode Mode,
		const EPCGExScaleToFit UniformFit, const EPCGExScaleToFit FitX, const EPCGExScaleToFit FitY, const EPCGExScaleToFit FitZ,
		const FVector& TargetSize, const FVector& TargetScale,
		const FBox& InBounds, FVector& OutScale, FBox& OutBounds)
	{
		if (Mode == EPCGExFitMode::None)
		{
			return;
		}

		const FVector TargetSizeScaled = TargetSize * TargetScale;
		const FVector CandidateSize = InBounds.GetSize();

		const double XFactor = TargetSizeScaled.X / CandidateSize.X;
		const double YFactor = TargetSizeScaled.Y / CandidateSize.Y;
		const double ZFactor = TargetSizeScaled.Z / CandidateSize.Z;

		// Pack all three uniform scale options into a single FVector:
		// X = smallest axis ratio (Min), Y = largest (Max), Z = average (Avg).
		// ScaleToFitAxis indexes into this vector to pick the desired uniform mode.
		const FVector FitMinMax = FVector(FMath::Min3(XFactor, YFactor, ZFactor), FMath::Max3(XFactor, YFactor, ZFactor), (XFactor + YFactor + ZFactor) / 3);

		OutBounds.Min = InBounds.Min;
		OutBounds.Max = InBounds.Max;

		if (Mode == EPCGExFitMode::Uniform)
		{
			ScaleToFitAxis(UniformFit, 0, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(UniformFit, 1, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(UniformFit, 2, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
		}
		else
		{
			ScaleToFitAxis(FitX, 0, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(FitY, 1, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(FitZ, 2, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
		}
	}

	/**
	 * Local-frame scale for a target-frame per-axis fit on an asset pre-rotated by R and pre-scaled by S:
	 * diagonal of R^T * diag(FitScale) * R, times S. Exact for a uniform FitScale (Min/Max/Avg) or a 90-degree rotation; otherwise
	 * the ideal fit is a skew FTransform cannot hold and this is its least-squares diagonal.
	 */
	FORCEINLINE FVector ResolveLocalFitScale(const FQuat& LocalRotation, const FVector& LocalScale, const FVector& FitScale)
	{
		const FVector AxisX = LocalRotation.GetAxisX();
		const FVector AxisY = LocalRotation.GetAxisY();
		const FVector AxisZ = LocalRotation.GetAxisZ();

		return FVector(
			LocalScale.X * (FitScale.X * AxisX.X * AxisX.X + FitScale.Y * AxisX.Y * AxisX.Y + FitScale.Z * AxisX.Z * AxisX.Z),
			LocalScale.Y * (FitScale.X * AxisY.X * AxisY.X + FitScale.Y * AxisY.Y * AxisY.Y + FitScale.Z * AxisY.Z * AxisY.Z),
			LocalScale.Z * (FitScale.X * AxisZ.X * AxisZ.X + FitScale.Y * AxisZ.Y * AxisZ.Y + FitScale.Z * AxisZ.Z * AxisZ.Z));
	}

	/** Axis bitmask (1/2/4) of the fits that promise to stay inside the target: Fill (per-axis even in Uniform mode) and Individual Min. */
	FORCEINLINE uint8 ContainmentAxes(const EPCGExFitMode Mode, const EPCGExScaleToFit UniformFit, const EPCGExScaleToFit FitX, const EPCGExScaleToFit FitY, const EPCGExScaleToFit FitZ)
	{
		if (Mode == EPCGExFitMode::Uniform)
		{
			return UniformFit == EPCGExScaleToFit::Fill ? 7 : 0;
		}
		if (Mode != EPCGExFitMode::Individual)
		{
			return 0;
		}

		uint8 Axes = 0;
		if (FitX == EPCGExScaleToFit::Fill || FitX == EPCGExScaleToFit::Min) { Axes |= 1; }
		if (FitY == EPCGExScaleToFit::Fill || FitY == EPCGExScaleToFit::Min) { Axes |= 2; }
		if (FitZ == EPCGExScaleToFit::Fill || FitZ == EPCGExScaleToFit::Min) { Axes |= 4; }
		return Axes;
	}

	/** Uniform factor (<= 1) bringing FootprintSize inside TargetSize on the masked axes; 1 when nothing overflows. Sign-safe for mirrored targets. */
	FORCEINLINE double ContainmentShrink(const uint8 Axes, const FVector& TargetSize, const FVector& FootprintSize)
	{
		double Shrink = 1.0;
		for (int32 Axis = 0; Axis < 3; Axis++)
		{
			if ((Axes & (1 << Axis)) && FootprintSize[Axis] > 0.0)
			{
				Shrink = FMath::Min(Shrink, FMath::Abs(TargetSize[Axis]) / FootprintSize[Axis]);
			}
		}
		return Shrink;
	}

	/**
	 * Per-axis justification translation from resolved values. Shared by the node-level,
	 * attribute-driven details (FPCGExSingleJustifyDetails reads its getters then calls this)
	 * and the lean per-entry representation (plain constants).
	 * FromValue/ToValue are normalized positions (0 = bounds min, 0.5 = center, 1 = bounds max),
	 * consumed only by the Custom modes (and by To == Same when From == Custom).
	 */
	FORCEINLINE void JustifyAxis(
		const EPCGExJustifyFrom From, const EPCGExJustifyTo To,
		const double FromValue, const double ToValue,
		const int32 Axis,
		const FVector& InCenter, const FVector& InSize,
		const FVector& OutCenter, const FVector& OutSize,
		FVector& OutTranslation)
	{
		double Start = 0;
		double End = 0;

		const double HalfOutSize = OutSize[Axis] * 0.5;
		const double HalfInSize = InSize[Axis] * 0.5;

		switch (From)
		{
		default: case EPCGExJustifyFrom::Min:
			Start = OutCenter[Axis] - HalfOutSize;
			break;
		case EPCGExJustifyFrom::Center:
			Start = OutCenter[Axis];
			break;
		case EPCGExJustifyFrom::Max:
			Start = OutCenter[Axis] + HalfOutSize;
			break;
		case EPCGExJustifyFrom::Custom:
			Start = OutCenter[Axis] - HalfOutSize + (OutSize[Axis] * FromValue);
			break;
		case EPCGExJustifyFrom::Pivot:
			Start = 0;
			break;
		}

		switch (To)
		{
		default: case EPCGExJustifyTo::Min:
			End = InCenter[Axis] - HalfInSize;
			break;
		case EPCGExJustifyTo::Center:
			End = InCenter[Axis];
			break;
		case EPCGExJustifyTo::Max:
			End = InCenter[Axis] + HalfInSize;
			break;
		case EPCGExJustifyTo::Custom:
			End = InCenter[Axis] - HalfInSize + (InSize[Axis] * ToValue);
			break;
		case EPCGExJustifyTo::Same:
			// Mirror the 'From' mode onto the target bounds. The node-level details usually
			// pre-resolve Same during Init; the lean per-entry path relies on this switch.
			switch (From)
			{
			default: case EPCGExJustifyFrom::Min:
				End = InCenter[Axis] - HalfInSize;
				break;
			case EPCGExJustifyFrom::Center:
				End = InCenter[Axis];
				break;
			case EPCGExJustifyFrom::Max:
				End = InCenter[Axis] + HalfInSize;
				break;
			case EPCGExJustifyFrom::Custom:
				// Same-as-Custom: mirror the From fraction into the target bounds.
				End = InCenter[Axis] - HalfInSize + (InSize[Axis] * FromValue);
				break;
			case EPCGExJustifyFrom::Pivot:
				End = 0;
				break;
			}
			break;
		case EPCGExJustifyTo::Pivot:
			End = 0;
			break;
		}

		OutTranslation[Axis] = End - Start;
	}
}

/**
 * Lean, per-entry-safe counterpart to FPCGExScaleToFitDetails: same modes, none of the
 * node-side machinery, safe to serialize on asset collection entries. Kept as a separate
 * type so future node-side additions (e.g. attribute-driven options) can never leak into
 * collection asset layouts.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExLeanScaleToFitDetails
{
	GENERATED_BODY()

	FPCGExLeanScaleToFitDetails() = default;

	/**
	 * How scaling is applied to fit within target bounds.
	 * None = no scaling, Uniform = same scale all axes, Individual = per-axis control.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExFitMode ScaleToFitMode = EPCGExFitMode::Uniform;

	/**
	 * Uniform scaling strategy.
	 * Fill = stretch to fill, Min = fit smallest axis, Max = fit largest axis, Avg = average.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="ScaleToFitMode == EPCGExFitMode::Uniform", EditConditionHides))
	EPCGExScaleToFit ScaleToFit = EPCGExScaleToFit::Min;

	/** Scaling strategy for X axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitX = EPCGExScaleToFit::None;

	/** Scaling strategy for Y axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitY = EPCGExScaleToFit::None;

	/** Scaling strategy for Z axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitZ = EPCGExScaleToFit::None;

	void Process(const PCGExData::FPoint& InPoint, const FBox& InBounds, FVector& OutScale, FBox& OutBounds) const;
};

/**
 * Lean, per-entry-safe counterpart to FPCGExSingleJustifyDetails. Custom From/To are
 * backed by plain constants -- attribute-driven values are a node-level concept and
 * cannot exist on shared collection entries.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExLeanSingleJustifyDetails
{
	GENERATED_BODY()

	FPCGExLeanSingleJustifyDetails() = default;

	/**
	 * Reference point on the object being positioned.
	 * Min/Center/Max/Pivot, or Custom for the constant below.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExJustifyFrom From = EPCGExJustifyFrom::Center;

	/**
	 * Constant 'From' position.
	 * 0 = bounds min, 0.5 = center, 1 = bounds max.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Constant", EditCondition="From == EPCGExJustifyFrom::Custom", EditConditionHides))
	float FromConstant = 0.5f;

	/**
	 * Target point in the container bounds to align to.
	 * Same = match 'From', or Min/Center/Max/Pivot/Custom.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExJustifyTo To = EPCGExJustifyTo::Same;

	/**
	 * Constant 'To' position.
	 * 0 = bounds min, 0.5 = center, 1 = bounds max.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Constant", EditCondition="To == EPCGExJustifyTo::Custom", EditConditionHides))
	float ToConstant = 0.5f;
};

/**
 * Lean, per-entry-safe counterpart to FPCGExJustificationDetails. Stateless and const at
 * runtime (no Init, no getters) -- entries are read concurrently from multiple processors.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExLeanJustificationDetails
{
	GENERATED_BODY()

	FPCGExLeanJustificationDetails() = default;

	/** Enable justification on X axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bDoJustifyX = true;

	/** X axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bDoJustifyX"))
	FPCGExLeanSingleJustifyDetails JustifyX;

	/** Enable justification on Y axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bDoJustifyY = true;

	/** Y axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bDoJustifyY"))
	FPCGExLeanSingleJustifyDetails JustifyY;

	/** Enable justification on Z axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bDoJustifyZ = true;

	/** Z axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bDoJustifyZ"))
	FPCGExLeanSingleJustifyDetails JustifyZ;

	void Process(const FBox& InBounds, const FBox& OutBounds, FVector& OutTranslation) const;
};

namespace PCGExFitting
{
	/**
	 * Resolved per-entry override selection for one fitting computation, passed alongside
	 * FPCGExFittingDetailsHandler::ComputeTransform / ComputeLocalTransform.
	 * Null members fall back to the handler's node-level details.
	 */
	struct FOverridesView
	{
		const FPCGExLeanScaleToFitDetails* ScaleToFit = nullptr;
		const FPCGExLeanJustificationDetails* Justification = nullptr;

		bool Any() const { return ScaleToFit || Justification; }
	};
}
