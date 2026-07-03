// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExSettingsMacros.h"
#include "Details/PCGExSubdivisionDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Paths/PCGExPathProfile.h"
#include "Utils/PCGValueRange.h"

#include "PCGExExtrudePath.generated.h"

namespace PCGExExtrudePath
{
	const FName SourceExtrudeFilters = TEXT("Extrude Conditions");
	const FName SourceCustomProfile = TEXT("Profile");
}

UENUM()
enum class EPCGExExtrudeEndpoint : uint8
{
	Both  = 0 UMETA(DisplayName = "Start and End", ToolTip="Extrude both endpoints."),
	Start = 1 UMETA(DisplayName = "Start", ToolTip="Extrude the start endpoint only."),
	End   = 2 UMETA(DisplayName = "End", ToolTip="Extrude the end endpoint only."),
};

UENUM()
enum class EPCGExExtrudeDirection : uint8
{
	PathDirection = 0 UMETA(DisplayName = "Path Direction", ToolTip="Extrude outward along the endpoint's terminal segment (inverted direction to prev/next), optionally tilted by the Direction offset."),
	Custom        = 1 UMETA(DisplayName = "Custom", ToolTip="Extrude along a direction read from a constant or attribute."),
};

UENUM()
enum class EPCGExExtrudeProfileType : uint8
{
	Line   = 0 UMETA(DisplayName = "Line", ToolTip="Straight extrusion, subdivided evenly."),
	Arc    = 1 UMETA(DisplayName = "Arc", ToolTip="Seamless arc, tangent to the path at the endpoint (a perpendicular extrusion direction gives a half-circle). Needs an extrusion direction not parallel to the path (Custom direction or a non-zero offset), else it stays a straight line."),
	Custom = 2 UMETA(DisplayName = "Custom", ToolTip="Custom profile applied to the new segment. Needs an extrusion direction angled from the path (Custom direction or a non-zero offset), else it falls back to no profile."),
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(Keywords = "extend grow prolong lengthen cap", PCGExNodeLibraryDoc="paths/modify/path-extrude"))
class UPCGExExtrudePathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	explicit UPCGExExtrudePathSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ExtrudePath, "Path : Extrude", "Extrude new points out of path endpoints, keeping the originals.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExExtrudePath::SourceExtrudeFilters, "Filters which endpoints get extruded (endpoint is extruded only if it passes).", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** Which endpoints to extrude (start, end, or both). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExExtrudeEndpoint Endpoint = EPCGExExtrudeEndpoint::Both;

	/** Distance the new endpoint is pushed out by. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Length = FPCGExInputShorthandSelectorDouble(FName("Length"), 100.0, false);

	/** How the extrusion direction is computed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExExtrudeDirection DirectionMode = EPCGExExtrudeDirection::PathDirection;

	/** Extrusion direction in Custom mode; offset added to the path direction in Path Direction mode (zero = pure path direction). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDirection Direction = FPCGExInputShorthandSelectorDirection(FName("$Transform.Up"), FVector::ZeroVector, false);

	/** If enabled, the offset is added as-read so its length drives how strongly it tilts the path direction; otherwise it is normalized first and weighs equally with it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ├─ Offset Magnitude Matters", EditCondition="DirectionMode == EPCGExExtrudeDirection::PathDirection", EditConditionHides))
	bool bOffsetMagnitudeMatters = false;

	/** If enabled, the direction read above is rotated by the endpoint's transform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Transform Direction"))
	bool bTransformDirection = false;


	/** Type of profile applied to the new segment. Arc & Custom curve the extrusion and require a Custom direction (Path Direction is always straight). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Profile", meta = (PCG_NotOverridable))
	EPCGExExtrudeProfileType Type = EPCGExExtrudeProfileType::Line;

	/** Define how the custom profile is scaled on the main axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Profile", meta = (PCG_Overridable, EditCondition="Type == EPCGExExtrudeProfileType::Custom", EditConditionHides))
	EPCGExPathProfileScaling MainAxisScaling = EPCGExPathProfileScaling::Uniform;

	/** Scale or Distance value for the main axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Profile", meta = (PCG_Overridable, EditCondition="Type == EPCGExExtrudeProfileType::Custom && (MainAxisScaling == EPCGExPathProfileScaling::Scale || MainAxisScaling == EPCGExPathProfileScaling::Distance)", EditConditionHides))
	double MainAxisScale = 1;

	/** Define how the custom profile is scaled on the cross axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Profile", meta = (PCG_Overridable, EditCondition="Type == EPCGExExtrudeProfileType::Custom", EditConditionHides))
	EPCGExPathProfileScaling CrossAxisScaling = EPCGExPathProfileScaling::Uniform;

	/** Scale or Distance value for the cross axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Profile", meta = (PCG_Overridable, EditCondition="Type == EPCGExExtrudeProfileType::Custom && (CrossAxisScaling == EPCGExPathProfileScaling::Scale || CrossAxisScaling == EPCGExPathProfileScaling::Distance)", EditConditionHides))
	double CrossAxisScale = 1;


	/** Whether to subdivide the new segment (Line & Arc). Without this an extruded endpoint is just a single new tip point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivision", meta = (PCG_Overridable, EditCondition="Type != EPCGExExtrudeProfileType::Custom", EditConditionHides))
	bool bSubdivide = false;

	/** Subdivision method. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivision", meta = (PCG_Overridable, DisplayName=" ├─ Method", EditCondition="bSubdivide && Type != EPCGExExtrudeProfileType::Custom", EditConditionHides))
	EPCGExSubdivideMode SubdivideMethod = EPCGExSubdivideMode::Count;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivision", meta=(PCG_Overridable, DisplayName=" └─ Subdivisions Amount", EditCondition="bSubdivide && Type != EPCGExExtrudeProfileType::Custom && SubdivideMethod != EPCGExSubdivideMode::Manhattan", EditConditionHides, ClampMin=0.1))
	FPCGExInputShorthandSelectorDouble SubdivisionAmount = FPCGExInputShorthandSelectorDouble(FName("Count"), 10.0, false);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Subdivision", meta=(PCG_Overridable, DisplayName=" └─ Manhattan", EditCondition="bSubdivide && Type != EPCGExExtrudeProfileType::Custom && SubdivideMethod == EPCGExSubdivideMode::Manhattan", EditConditionHides))
	FPCGExManhattanDetails ManhattanDetails;


	/** Write a flag marking subdivision points (the intermediate points added between the endpoint and its new tip). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Flags", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagSubdivision = false;

	/** Name of the boolean flag to write whether the point is a subdivision point or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Flags", meta = (PCG_Overridable, EditCondition="bFlagSubdivision"))
	FName SubdivisionFlagName = "IsSubdivision";


	/** Suppress the warning raised when an Arc/Custom profile is degenerate (extrusion parallel to the path). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietDegenerateProfileWarning = false;

	/** Suppress warning when attempting to extrude a closed loop path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietClosedLoopWarning = false;

};

struct FPCGExExtrudePathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExExtrudePathElement;

	TSharedPtr<PCGExData::FFacade> CustomProfileFacade;
	TArray<FVector> CustomProfilePositions;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExExtrudePathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ExtrudePath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExExtrudePath
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExExtrudePathContext, UPCGExExtrudePathSettings>
	{
		int32 NumPoints = 0;
		int32 LastPointIndex = 0;

		bool bExtrudeStart = false;
		bool bExtrudeEnd = false;

		// Output-ordered positions for each extrusion (tip + subdivisions), empty when the endpoint isn't extruded
		TArray<FVector> StartPositions;
		TArray<FVector> EndPositions;

		int32 StartExtra = 0; // == StartPositions.Num()
		int32 EndExtra = 0;   // == EndPositions.Num()
		int32 NumOutPoints = 0;

		bool bProfileActive = false;
		bool bSubdivideCount = false;
		bool bIsManhattan = false;

		FPCGExManhattanDetails ManhattanDetails;

		TSharedPtr<PCGExDetails::TSettingValue<double>> LengthGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> DirectionGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> SubdivAmountGetter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
			DefaultPointFilterValue = true;
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		virtual void CompleteWork() override;
		virtual void Write() override;

	protected:
		// Builds StartPositions/EndPositions for one endpoint. Returns the number of points added (0 if not extruded).
		int32 ComputeExtrusion(const bool bIsStart, const TConstPCGValueRange<FTransform>& InTransforms, TArray<FVector>& OutPositions);

		// Source input index each output point inherits from (start-new -> 0, originals -> self, end-new -> Last)
		FORCEINLINE int32 SourceIndex(const int32 OutIndex) const
		{
			if (OutIndex < StartExtra)
			{
				return 0;
			}
			if (OutIndex < StartExtra + NumPoints)
			{
				return OutIndex - StartExtra;
			}
			return LastPointIndex;
		}
	};
}
