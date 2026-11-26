// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathMean.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Utils/PCGExCurveLookup.h"
#include "Utils/PCGExWeightCurve.h"

#include "PCGExPathTaper.generated.h"

namespace PCGExData
{
	class IBufferProxy;
}

UENUM()
enum class EPCGExPathTaperOverlap : uint8
{
	Fit      = 0 UMETA(DisplayName = "Fit", ToolTip="Start and End lengths that add up to more than the path shrink proportionally, so the ramps still meet at the body."),
	Min      = 1 UMETA(DisplayName = "Min", ToolTip="Lengths are kept. Where the ramps overlap, the end nearer its tip wins, so the body may never be reached."),
	Multiply = 2 UMETA(DisplayName = "Multiply", ToolTip="Lengths are kept. Overlapping ramps multiply, keeping the overlap further from the body than Min."),
};

UENUM()
enum class EPCGExPathTaperBlend : uint8
{
	None     = 0 UMETA(DisplayName = "None", ToolTip="Replace the target's value with the taper value."),
	Add      = 1 UMETA(DisplayName = "Add", ToolTip="Add the taper value to the target's current value."),
	Multiply = 2 UMETA(DisplayName = "Multiply", ToolTip="Multiply the target's current value by the taper value."),
};

/**
 * Writes a 0..1 taper weight that ramps up from the path ends, then applies Scale and Offset.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(Keywords = "falloff fade gradient ramp ends", PCGExNodeLibraryDoc="paths/analyze/path-taper"))
class UPCGExPathTaperSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathTaper, "Path : Taper", "Write a taper weight that ramps up from the path's start and end, then scaled and offset.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/** Attribute or property to write the taper value to, e.g. Taper, $Density or $Position.Z. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector Output = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(FName("Taper"));

	/** How the taper value combines with the target's value. A missing target attribute is written as-is. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPathTaperBlend BlendMode = EPCGExPathTaperBlend::None;

	/** Multiplier applied to the 0..1 taper weight, before Offset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Scale = FPCGExInputShorthandSelectorDouble(FName("@Data.TaperScale"), 1, false);

	/** Added after Scale. Scale -1 with Offset 1 inverts the taper. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Offset = FPCGExInputShorthandSelectorDouble(FName("@Data.TaperOffset"), 0, false);

	/** How Start and End lengths are read: Discrete as distances, Relative as fractions of the path length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExMeanMeasure LengthMeasure = EPCGExMeanMeasure::Discrete;

	/** Taper from the first point. See Start Length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bTaperStart = true;

	/** When enabled, length of the ramp rising from the first point. Read on the first point, so @Data works too. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bTaperStart"))
	FPCGExInputShorthandSelectorDouble StartLength = FPCGExInputShorthandSelectorDouble(FName("@Data.TaperStart"), 100, false);

	/** Taper from the last point. See End Length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bTaperEnd = true;

	/** When enabled, length of the ramp rising from the last point. Read on the last point, so @Data works too. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bTaperEnd"))
	FPCGExInputShorthandSelectorDouble EndLength = FPCGExInputShorthandSelectorDouble(FName("@Data.TaperEnd"), 100, false);

	/** How Start and End lengths that add up to more than the path are resolved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPathTaperOverlap Overlap = EPCGExPathTaperOverlap::Fit;

	/** Ramp shape, from the tip (0) to the untapered body (1). Mirrored for the end unless End Curve is enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, PCGExCurveTimeMin="0", PCGExCurveTimeMax="1"))
	FPCGExWeightCurve Curve = FPCGExWeightCurve(0, 1);

	/** Use a separate end ramp shape. See End Curve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bUseEndCurve = false;

	/** When enabled, end ramp shape, from the last point (0) to the untapered body (1). Otherwise Curve is mirrored. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="bUseEndCurve", PCGExCurveTimeMin="0", PCGExCurveTimeMax="1"))
	FPCGExWeightCurve EndCurve = FPCGExWeightCurve(0, 1);

	/** Weight for closed loops and zero-length paths, which have no ends to taper from. Scale and Offset still apply. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble01 ClosedLoopWeight = FPCGExInputShorthandSelectorDouble01(FName("@Data.TaperWeight"), 1, false);
};

struct FPCGExPathTaperContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathTaperElement;

	PCGExFloatLUT StartCurve;
	PCGExFloatLUT EndCurve;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathTaperElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathTaper)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathTaper
{
	/** Ramp lengths in distance units; 0 leaves that end untapered. */
	struct FSpans
	{
		double Start = 0;
		double End = 0;
	};

	/** Negative lengths become 0, Relative ones scale by the path length, and Fit shrinks a pair that overflows the path. */
	PCGEXELEMENTSPATHS_API FSpans ResolveSpans(const double InStart, const double InEnd, const double InPathLength, const EPCGExMeanMeasure InMeasure, const EPCGExPathTaperOverlap InOverlap);

	/** Weight at a distance along the path. Ramp positions (0 at a tip, 1 past its span, 1 when untapered) combine first,
	 *  then the curve of the end nearer its tip shapes them, so any curve shape works and the body reads the curve at 1. */
	FORCEINLINE double ComputeWeight(const double InDistToStart, const double InPathLength, const FSpans& InSpans, const FPCGExCurveFloatLookup& InStartCurve, const FPCGExCurveFloatLookup& InEndCurve, const EPCGExPathTaperOverlap InOverlap)
	{
		const double StartPos = InSpans.Start > 0 ? FMath::Clamp(InDistToStart / InSpans.Start, 0.0, 1.0) : 1.0;
		const double EndPos = InSpans.End > 0 ? FMath::Clamp((InPathLength - InDistToStart) / InSpans.End, 0.0, 1.0) : 1.0;
		const double RampPos = InOverlap == EPCGExPathTaperOverlap::Multiply ? StartPos * EndPos : FMath::Min(StartPos, EndPos);
		return (EndPos < StartPos ? InEndCurve : InStartCurve).Eval(RampPos);
	}

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathTaperContext, UPCGExPathTaperSettings>
	{
		TSharedPtr<PCGExDetails::TSettingValue<double>> ScaleGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> OffsetGetter;

		// Set only for closed loops and zero-length paths; the per-point weight then comes from it.
		TSharedPtr<PCGExDetails::TSettingValue<double>> ClosedLoopWeightGetter;

		TSharedPtr<PCGExData::IBufferProxy> OutputProxy;

		// Reads the target's current value; set only while blending.
		TSharedPtr<PCGExData::IBufferProxy> TargetReader;

		// The settings' blend, or None when the target attribute doesn't exist yet.
		EPCGExPathTaperBlend BlendMode = EPCGExPathTaperBlend::None;

		TArray<double> DistToStart;
		double PathLength = 0;
		FSpans Spans;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;

	protected:
		void WriteValue(const int32 Index, const double Value) const;
	};
}
