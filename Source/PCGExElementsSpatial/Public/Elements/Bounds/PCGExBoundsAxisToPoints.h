// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"


#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"

#include "Math/PCGExUVW.h"


#include "PCGExBoundsAxisToPoints.generated.h"

class FPCGExComputeIOBounds;

UENUM()
enum class EPCGExBoundAxisPriority : uint8
{
	Shortest = 0 UMETA(DisplayName = "Shortest", ToolTip="Shortest axis"),
	Longest  = 1 UMETA(DisplayName = "Longest", ToolTip="Longest axis"),
	Median   = 2 UMETA(DisplayName = "Median", ToolTip="The leftover axis, that is neither the shortest nor the longest.")
};

UENUM()
enum class EPCGExAxisDirectionConstraint : uint8
{
	None  = 0 UMETA(DisplayName = "None", ToolTip="Apply no direction shift to the picked axis."),
	Avoid = 1 UMETA(DisplayName = "Avoid", ToolTip="When the picked axis is the one most aligned with Direction, fall back to the median-length axis."),
	Favor = 2 UMETA(DisplayName = "Favor", ToolTip="When the picked axis is not the one most aligned with Direction, step one rank toward the longer axes."),
};

UENUM()
enum class EPCGExAxisSizeConstraint : uint8
{
	None    = 0 UMETA(DisplayName = "None", ToolTip="Apply no size shift to the picked axis."),
	Greater = 1 UMETA(DisplayName = "Greater", ToolTip="Step one rank toward the longer axes for every remaining axis whose extent is under Threshold."),
	Smaller = 1 UMETA(DisplayName = "Smaller", ToolTip="Shares its value with Greater, so picking it selects Greater and reads back as Greater. The step-toward-shorter branch is unreachable."),
};

UENUM()
enum class EPCGExAxisConstraintSorting : uint8
{
	SizeMatters      = 0 UMETA(DisplayName = "Size matters more", ToolTip="Apply the direction constraint first and the size constraint second, so size has the final say."),
	DirectionMatters = 1 UMETA(DisplayName = "Direction matters more", ToolTip="Apply the size constraint first and the direction constraint second, so direction has the final say."),
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/analyze/bounds-axis-to-points"))
class UPCGExBoundsAxisToPointsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BoundsAxisToPoints, "Bounds Axis To Points", "Generate a two-point from a bound axis.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Transform);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Generates a point collections per generated point */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bGeneratePerPointData = false;

	/** Which point property to use for bounds calculation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsReference = EPCGExPointBoundsSource::ScaledBounds;

	/** Which initial direction should initially picked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBoundAxisPriority Priority = EPCGExBoundAxisPriority::Shortest;

	/** Shifts the axis selection based on whether the selected axis points toward or away from a static direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExAxisDirectionConstraint DirectionConstraint = EPCGExAxisDirectionConstraint::Avoid;

	/** Reference direction for the direction constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Direction", EditCondition="DirectionConstraint != EPCGExAxisDirectionConstraint::None", EditConditionHides))
	FVector Direction = FVector::UpVector;

	/** Shifts the axis selection based on whether its size is greater or smaller than a given threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExAxisSizeConstraint SizeConstraint = EPCGExAxisSizeConstraint::Greater;

	/** Size threshold for the size constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Threshold", EditCondition="SizeConstraint != EPCGExAxisSizeConstraint::None", EditConditionHides))
	double SizeThreshold = 0.1;

	/** In which order shifting should be processed, as one is likely to override the other. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="DirectionConstraint != EPCGExAxisDirectionConstraint::None && SizeConstraint != EPCGExAxisSizeConstraint::None", EditConditionHides))
	EPCGExAxisConstraintSorting ConstraintsOrder = EPCGExAxisConstraintSorting::DirectionMatters;

	/** Extent factor at which the points will be created on the selected world-align axis */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double U = 1;

	/** Set the extents of output points to a custom value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bSetExtents = true;

	/** Set the output point' extent to this value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSetExtents"))
	FVector Extents = FVector(0.5);

	/** Set the scale of output points to a custom value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bSetScale = true;

	/**  Set the output point' scale to this value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSetScale"))
	FVector Scale = FVector::OneVector;

	/** Copy source point attributes as tags on output collections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bGeneratePerPointData"))
	FPCGExAttributeToTagDetails PointAttributesToOutputTags;

private:
	friend class FPCGExBoundsAxisToPointsElement;
};

struct FPCGExBoundsAxisToPointsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBoundsAxisToPointsElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBoundsAxisToPointsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BoundsAxisToPoints)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBoundsAxisToPoints
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBoundsAxisToPointsContext, UPCGExBoundsAxisToPointsSettings>
	{
		FPCGExUVW BaseUVW;

		int32 NumPoints = 0;
		bool bGeneratePerPointData = false;

		bool bSetExtents = false;
		FVector Extents = FVector::OneVector;

		bool bSetScale = false;
		FVector Scale = FVector::OneVector;

		FPCGExAttributeToTagDetails PointAttributesToOutputTags;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		TArray<TSharedPtr<PCGExData::FPointIO>> NewOutputs;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
