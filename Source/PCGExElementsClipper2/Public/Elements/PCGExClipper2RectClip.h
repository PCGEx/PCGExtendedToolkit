// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClipper2Processor.h"
#include "Paths/PCGExPath.h"

#include "PCGExClipper2RectClip.generated.h"

/**
 * Source for the clipping rectangle bounds
 */
UENUM(BlueprintType)
enum class EPCGExRectClipBoundsSource : uint8
{
	Operand = 0 UMETA(DisplayName = "Operand Bounds", ToolTip="Use the combined bounds of all operand spatial data in the group"),
	Manual  = 1 UMETA(DisplayName = "Manual", ToolTip="Use manually specified rectangle bounds"),
};

/**
 * Clipper2 Rectangle Clipping
 * Uses optimized RectClip64 algorithm which is significantly faster than boolean intersection for rectangles.
 * NOTE : Work only with AABB!
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/intersect/clipper2-rect-clip"))
class UPCGExClipper2RectClipSettings : public UPCGExClipper2ProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Clipper2RectClip, "Clipper2 : Rect Clip",
	                 "Fast rectangle clipping using optimized Clipper2 algorithm.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Projection settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Source for the clipping rectangle bounds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Clip Bounds", meta = (PCG_Overridable))
	EPCGExRectClipBoundsSource BoundsSource = EPCGExRectClipBoundsSource::Operand;

	/** Manual rectangle bounds in world space (used when BoundsSource is Manual).
	 *  Note: Only X and Y are used after projection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Clip Bounds", meta = (PCG_Overridable, EditCondition="BoundsSource == EPCGExRectClipBoundsSource::Manual", EditConditionHides))
	FBox ManualBounds = FBox(FVector(-100, -100, -100), FVector(100, 100, 100));

	/** Uniform padding to apply to the bounds (can be negative to shrink) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Clip Bounds", meta = (PCG_Overridable))
	double BoundsPadding = 0.0;

	/** Axis-specific padding multipliers */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Clip Bounds", meta = (PCG_Overridable))
	FVector2D BoundsPaddingScale = FVector2D(1.0, 1.0);

	/** If true, clips open paths using RectClipLines (preserves them as open paths).
	 *  If false, treats open paths as closed polygons for clipping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable, EditCondition="!bSkipOpenPaths", EditConditionHides))
	bool bClipOpenPathsAsLines = true;

	/** If true, clips closed paths as lines (outputs will be open paths).
	*  Useful when you want to cut through paths rather than get polygon intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable, EditCondition="!bInvertClip", EditConditionHides))
	bool bClipAsLines = false;

	/** If enabled, inverts the clip region (uses boolean difference with the rectangle instead) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable))
	bool bInvertClip = false;

	virtual bool WantsOperands() const override;
	virtual FPCGExGeo2DProjectionDetails GetProjectionDetails() const override;
	virtual bool SupportOpenMainPaths() const override;
	virtual bool SupportOpenOperandPaths() const override;
	virtual bool OperandsAsBounds() const override;
};

struct FPCGExClipper2RectClipContext final : FPCGExClipper2ProcessorContext
{
	friend class FPCGExClipper2RectClipElement;

	virtual void Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group) override;

protected:
	// Compute rectangle from bounds source using PCG data bounds
	PCGExClipper2Lib::Rect64 ComputeClipRect(
		const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
		const UPCGExClipper2RectClipSettings* Settings);

	// Compute combined world-space bounds from facade indices
	FBox ComputeCombinedBounds(const TArray<int32>& Indices);

	static void ApplyPadding(PCGExClipper2Lib::Rect64& Rect, double Padding, const FVector2D& Scale, int32 Precision);
};

class FPCGExClipper2RectClipElement final : public FPCGExClipper2ProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Clipper2RectClip)
};
