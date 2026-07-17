// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Math/PCGExMathAxis.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExSubdivisionDetails.generated.h"

class UPCGSettings;
class UPCGNode;

struct FPCGExContext;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

UENUM()
enum class EPCGExSubdivideMode : uint8
{
	Distance  = 0 UMETA(DisplayName = "Distance", ToolTip="Number of subdivisions depends on length"),
	Count     = 1 UMETA(DisplayName = "Count", ToolTip="Number of subdivisions is fixed"),
	Manhattan = 2 UMETA(DisplayName = "Manhattan", ToolTip="Manhattan subdivision, number of subdivisions depends on spatial relationship between the points; will be in the [0..2] range."),
};

UENUM()
enum class EPCGExManhattanMethod : uint8
{
	Simple       = 0 UMETA(DisplayName = "Simple", ToolTip="Simple Manhattan subdivision, will generate 0..2 points"),
	GridDistance = 1 UMETA(DisplayName = "Grid (Distance)", ToolTip="Grid Manhattan subdivision, will subdivide space according to a grid size."),
	GridCount    = 2 UMETA(DisplayName = "Grid (Count)", ToolTip="Grid Manhattan subdivision, will subdivide space according to a grid size."),
};

UENUM()
enum class EPCGExManhattanAlign : uint8
{
	World    = 0 UMETA(DisplayName = "World", ToolTip=""),
	Custom   = 1 UMETA(DisplayName = "Custom", ToolTip=""),
	SegmentX = 5 UMETA(DisplayName = "Segment X", ToolTip=""),
	SegmentY = 6 UMETA(DisplayName = "Segment Y", ToolTip=""),
	SegmentZ = 7 UMETA(DisplayName = "Segment Z", ToolTip=""),
};

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExManhattanDetails
{
	GENERATED_BODY()

	explicit FPCGExManhattanDetails(const bool InSupportAttribute = false)
		: bSupportAttribute(InSupportAttribute)
	{
	}

	UPROPERTY()
	bool bSupportAttribute = false;

	/** How Manhattan subdivision is calculated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExManhattanMethod Method = EPCGExManhattanMethod::Simple;

	/** Order in which axes are processed for subdivision. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAxisOrder Order = EPCGExAxisOrder::XYZ;

	/** Grid Size -- If using count, values will be rounded down to the nearest int. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Grid Size", EditCondition="Method != EPCGExManhattanMethod::Simple", EditConditionHides))
	FPCGExInputShorthandNameVector GridSizeValue = FPCGExInputShorthandNameVector(FName("GridSize"), FVector(10), false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType GridSizeInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName GridSizeAttribute_DEPRECATED = FName("GridSize");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector GridSize_DEPRECATED = FVector(10);

#pragma endregion

	/** How subdivision space is aligned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExManhattanAlign SpaceAlign = EPCGExManhattanAlign::World;

	/** Orientation of the subdivision space. Represented as a rotator; attribute source is read as a rotator. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Orient", EditCondition="SpaceAlign == EPCGExManhattanAlign::Custom", EditConditionHides))
	FPCGExInputShorthandSelectorRotator OrientValue = FPCGExInputShorthandSelectorRotator(FName("@Last"), FRotator::ZeroRotator, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType OrientInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector OrientAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FQuat OrientConstant_DEPRECATED = FQuat::Identity;

#pragma endregion

	bool IsValid() const;
	bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade);
	int32 ComputeSubdivisions(const FVector& A, const FVector& B, const int32 Index, TArray<FVector>& OutSubdivisions, double& OutDist) const;

#if WITH_EDITOR
	void ApplyDeprecation();
	/** Rewires the pre-shorthand override pins; call from the embedder's PCGExApplyDeprecationBeforeUpdatePins under the same version gate as ApplyDeprecation. */
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif

protected:
	bool bInitialized = false;

	int32 Comps[3] = {0, 0, 0};
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> GridSizeBuffer;
	TSharedPtr<PCGExDetails::TSettingValue<FRotator>> OrientBuffer;
};
