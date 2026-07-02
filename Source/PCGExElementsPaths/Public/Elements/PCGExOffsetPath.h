// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExOffsetPath.generated.h"

namespace PCGExPaths
{
	class FPathEdgeHalfAngle;
	class FPath;
}

UENUM()
enum class EPCGExOffsetAdjustment : uint8
{
	None         = 0 UMETA(DisplayName = "Raw", ToolTip="..."),
	SmoothCustom = 1 UMETA(DisplayName = "Custom Smooth", ToolTip="..."),
	SmoothAuto   = 2 UMETA(DisplayName = "Auto Smooth", ToolTip="..."),
	Mitre        = 3 UMETA(DisplayName = "Mitre", ToolTip="..."),
};

UENUM()
enum class EPCGExOffsetMethod : uint8
{
	Slide     = 0 UMETA(DisplayName = "Slide", ToolTip="..."),
	LinePlane = 1 UMETA(DisplayName = "Line/Plane", ToolTip="..."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/transform/path-offset"))
class UPCGExOffsetPathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(PathOffset, "Path : Offset", "Offset paths points.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters which points will be offset", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** Algorithm used to compute the offset direction at each point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExOffsetMethod OffsetMethod = EPCGExOffsetMethod::Slide;

	/** Size of the offset in the selected direction **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Offset = FPCGExInputShorthandSelectorDouble(FName("@Last"), 1.0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType OffsetInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector OffsetAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double OffsetConstant_DEPRECATED = 1.0;

#pragma endregion

	/** Offset scale.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Scale"))
	FPCGExInputShorthandSelectorDouble OffsetScale = FPCGExInputShorthandSelectorDouble(FName("$Scale"), 1.0, false);

	/** Scale offset direction & distance using point scale.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bApplyPointScaleToOffset = false;

	/** Type of arithmetic path point offset direction.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Direction", EditCondition="OffsetMethod == EPCGExOffsetMethod::Slide", EditConditionHides))
	EPCGExPathNormalDirection DirectionConstant = EPCGExPathNormalDirection::AverageNormal;

	/** Custom offset direction **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OffsetMethod == EPCGExOffsetMethod::Slide && DirectionConstant == EPCGExPathNormalDirection::Custom", EditConditionHides))
	FPCGExInputShorthandSelectorVector Direction = FPCGExInputShorthandSelectorVector(FName("$Rotation.Left"), FVector::UpVector, true);

	/** Inverts offset direction. Can also be achieved by using negative offset values, but this enable consistent inversion no matter the input.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvertDirection = false;

	/** Up vector used to calculate Offset direction.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector UpVectorConstant = PCGEX_CORE_SETTINGS.WorldUp;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType DirectionType_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector DirectionAttribute_DEPRECATED;

#pragma endregion

	/** Adjust aspect in tight angles */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OffsetMethod == EPCGExOffsetMethod::Slide"))
	EPCGExOffsetAdjustment Adjustment = EPCGExOffsetAdjustment::SmoothAuto;

	/** Adjust aspect in tight angles */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OffsetMethod == EPCGExOffsetMethod::Slide && Adjustment == EPCGExOffsetAdjustment::SmoothCustom", EditConditionHides))
	double AdjustmentScale = -0.5;

	/** Offset size.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OffsetMethod == EPCGExOffsetMethod::Slide && Adjustment == EPCGExOffsetAdjustment::Mitre", EditConditionHides))
	double MitreLimit = 4.0;
};

struct FPCGExOffsetPathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExOffsetPathElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExOffsetPathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(OffsetPath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExOffsetPath
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExOffsetPathContext, UPCGExOffsetPathSettings>
	{
		TConstPCGValueRange<FTransform> InTransforms;

		TSharedPtr<PCGExPaths::FPath> Path;
		TSharedPtr<PCGExPaths::FPathEdgeHalfAngle> PathAngles;
		TSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>> OffsetDirection;

		double DirectionFactor = -1; // Default to -1 because the normal maths changed at some point, inverting all existing value. Sorry for the lack of elegance.
		double OffsetConstant = 0;
		FVector Up = FVector::UpVector;

		TSharedPtr<PCGExDetails::TSettingValue<double>> OffsetGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> OffsetScaleGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> DirectionGetter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
	};
}
