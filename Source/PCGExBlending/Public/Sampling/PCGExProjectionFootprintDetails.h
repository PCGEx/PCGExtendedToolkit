// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExProjectionFootprintDetails.generated.h"

class UPCGSpatialData;
struct FPCGProjectionParams;

namespace PCGExMT
{
	struct FScope;
}

namespace PCGExData
{
	class FFacade;
	struct FConstPoint;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

UENUM()
enum class EPCGExFootprintReference : uint8
{
	Input     = 0 UMETA(DisplayName = "Input", ToolTip="The point's input rotation orients the footprint box."),
	Projected = 1 UMETA(DisplayName = "Projected", ToolTip="The rotation the target projects for the point center orients the footprint box (surface-aligned)."),
};

UENUM()
enum class EPCGExFootprintAxis : uint8
{
	Projection  = 0 UMETA(DisplayName = "Projection", ToolTip="Each corner's offset is measured along its own projection displacement; a push follows the extreme corner's displacement and lands it exactly on the surface."),
	ReferenceUp = 1 UMETA(DisplayName = "Reference Up", ToolTip="Offsets and push are measured along the reference rotation's up axis. Approximate on tilted references, keeps the point seated along its own axis."),
};

UENUM()
enum class EPCGExFootprintAdjust : uint8
{
	Add      = 0 UMETA(DisplayName = "Add", ToolTip="Extents + Adjust"),
	Multiply = 1 UMETA(DisplayName = "Multiply", ToolTip="Extents * Adjust"),
};

UENUM()
enum class EPCGExFootprintFailMetric : uint8
{
	None        = 0 UMETA(DisplayName = "None", ToolTip="The footprint never fails the projection."),
	Overhang    = 1 UMETA(DisplayName = "Overhang", ToolTip="Fail when the maximum overhang exceeds the threshold."),
	Penetration = 2 UMETA(DisplayName = "Penetration", ToolTip="Fail when the maximum penetration exceeds the threshold."),
	Either      = 3 UMETA(DisplayName = "Either", ToolTip="Fail when either maximum exceeds the threshold."),
};

UENUM()
enum class EPCGExFootprintThreshold : uint8
{
	Manual       = 0 UMETA(DisplayName = "Manual", ToolTip="Threshold value, constant or per-point attribute."),
	BoundsHeight = 1 UMETA(DisplayName = "Bounds Height", ToolTip="The footprint box' full height (Z size after adjust)."),
};

UENUM()
enum class EPCGExFootprintPush : uint8
{
	None    = 0 UMETA(DisplayName = "None", ToolTip="Measure only."),
	Sink    = 1 UMETA(DisplayName = "Sink", ToolTip="Push the point toward the surface until the most overhanging corner touches it."),
	Raise   = 2 UMETA(DisplayName = "Raise", ToolTip="Push the point away from the surface until the deepest corner surfaces."),
	Balance = 3 UMETA(DisplayName = "Balance", ToolTip="Push so the most overhanging and the deepest corner sit at equal distance from the surface."),
};

/** Probes the four bottom corners of the point bounds at the projected location to measure how unevenly the surface sits under them. */
USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExProjectionFootprintDetails
{
	GENERATED_BODY()

	FPCGExProjectionFootprintDetails() = default;

	/** Rotation that orients the footprint box at the projected center. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFootprintReference Reference = EPCGExFootprintReference::Input;

	/** Axis the corner offsets and the push are measured along. Push and measurement always share it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFootprintAxis Axis = EPCGExFootprintAxis::Projection;

	/** Point bounds the footprint box is built from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** How Adjust combines with the box extents (half sizes). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFootprintAdjust AdjustMode = EPCGExFootprintAdjust::Add;

	/** Per-axis change applied to the box extents before probing; a negative result clamps to zero. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorVector Adjust = FPCGExInputShorthandSelectorVector(FName("FootprintAdjust"), FVector::ZeroVector);

	/** Which measurement can fail the projection. A corner that finds no surface fails the point whenever this is not None. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFootprintFailMetric FailMetric = EPCGExFootprintFailMetric::None;

	/** Where the fail threshold comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="FailMetric != EPCGExFootprintFailMetric::None", EditConditionHides))
	EPCGExFootprintThreshold ThresholdSource = EPCGExFootprintThreshold::Manual;

	/** Distance above which the selected measurement fails the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="FailMetric != EPCGExFootprintFailMetric::None && ThresholdSource == EPCGExFootprintThreshold::Manual", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs Threshold = FPCGExInputShorthandSelectorDoubleAbs(FName("FootprintThreshold"), 100.0);

	/** Move the point along the measurement axis to seat it. The whole push vector is added, whatever position components are applied. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFootprintPush Push = EPCGExFootprintPush::Sink;
};

namespace PCGExSampling
{
	/**
	 * Runtime side of FPCGExProjectionFootprintDetails, bound to one facade.
	 * Init once, PrepareScope once per scope after the facade's Fetch, then Probe per point.
	 */
	class PCGEXBLENDING_API FProjectionFootprint
	{
	public:
		/** Per-scope operands, indexed by Index - Scope.Start. An array stays empty when its operand is constant. */
		struct FScopeView
		{
			TArray<FVector> Adjusts;
			TArray<double> Thresholds;
		};

		struct FResult
		{
			/** Largest gap under a corner, 0 when none hangs. */
			double Overhang = 0;
			/** Largest depth of a corner under the surface, 0 when none sinks. */
			double Penetration = 0;
			/** Push that lands the most overhanging corner on the surface; zero when nothing hangs. */
			FVector Lo = FVector::ZeroVector;
			/** Push that lands the deepest corner on the surface; zero when nothing sinks. */
			FVector Hi = FVector::ZeroVector;
			/** Unclamped extreme displacements, whatever their sign. */
			FVector RawLo = FVector::ZeroVector;
			FVector RawHi = FVector::ZeroVector;
			/** Box full Z size after adjust. */
			double Height = 0;
			int32 Hits = 0;
			bool bMissed = false;
		};

	private:
		EPCGExFootprintReference Reference = EPCGExFootprintReference::Input;
		EPCGExFootprintAxis Axis = EPCGExFootprintAxis::Projection;
		EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;
		EPCGExFootprintAdjust AdjustMode = EPCGExFootprintAdjust::Add;
		EPCGExFootprintFailMetric FailMetric = EPCGExFootprintFailMetric::None;
		EPCGExFootprintThreshold ThresholdSource = EPCGExFootprintThreshold::Manual;
		EPCGExFootprintPush Push = EPCGExFootprintPush::None;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> Adjust;
		/** Null unless a manual threshold is in use. */
		TSharedPtr<PCGExDetails::TSettingValue<double>> Threshold;

		// Constant and @Data operands resolve once in Init; scopes then skip the per-point reads.
		bool bConstantAdjust = true;
		bool bConstantThreshold = true;
		FVector ConstantAdjust = FVector::ZeroVector;
		double ConstantThreshold = 0;

	public:
		bool Init(const FPCGExProjectionFootprintDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		void PrepareScope(const PCGExMT::FScope& Scope, FScopeView& OutView) const;

		FORCEINLINE bool WantsProjectedRotation() const { return Reference == EPCGExFootprintReference::Projected; }
		FORCEINLINE bool WantsPush() const { return Push != EPCGExFootprintPush::None; }
		FORCEINLINE bool WantsFail() const { return FailMetric != EPCGExFootprintFailMetric::None; }

		/** Center is the projected location the box sits at, Rotation the reference orientation. Params must project positions only. */
		void Probe(const UPCGSpatialData* Target, const FPCGProjectionParams& Params, const PCGExData::FConstPoint& Point, const FVector& Center, const FQuat& Rotation, const FScopeView& View, const int32 ScopeIndex, FResult& OutResult) const;

		bool ShouldFail(const FResult& Result, const FScopeView& View, const int32 ScopeIndex) const;

		FVector GetPush(const FResult& Result) const;
	};
}
