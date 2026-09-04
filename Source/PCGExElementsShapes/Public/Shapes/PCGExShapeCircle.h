// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Core/PCGExShape.h"
#include "Core/PCGExShapeBuilderFactoryProvider.h"
#include "Core/PCGExShapeBuilderOperation.h"

#include "PCGExShapeCircle.generated.h"

USTRUCT(BlueprintType)
struct FPCGExShapeCircleConfig : public FPCGExShapeConfigBase
{
	GENERATED_BODY()

	FPCGExShapeCircleConfig()
		: FPCGExShapeConfigBase()
	{
	}

	/** Start Angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble StartAngle = FPCGExInputShorthandSelectorDouble(FName("StartAngle"), 0, false);

	/** End Angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble EndAngle = FPCGExInputShorthandSelectorDouble(FName("EndAngle"), 360, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType StartAngleInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector StartAngleAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double StartAngleConstant_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType EndAngleInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector EndAngleAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double EndAngleConstant_DEPRECATED = 360;

#pragma endregion

	/** If enabled, will flag circle as being closed if possible. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIsClosedLoop = true;

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
#endif
};

namespace PCGExShapes
{
	class FCircle : public FShape
	{
	public:
		double Radius = 1;
		double StartAngle = 0;
		double EndAngle = TWO_PI;
		double AngleRange = TWO_PI;
		bool bClosedLoop = false;

		explicit FCircle(const PCGExData::FConstPoint& InPointRef)
			: FShape(InPointRef)
		{
		}
	};
}

/**
 * 
 */
class FPCGExShapeCircleBuilder : public FPCGExShapeBuilderOperation
{
public:
	FPCGExShapeCircleConfig Config;

	virtual bool PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade) override;
	virtual void PrepareShape(const PCGExData::FConstPoint& Seed) override;
	virtual void BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, bool bOwnsData = false) override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<double>> StartAngle;
	TSharedPtr<PCGExDetails::TSettingValue<double>> EndAngle;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExShapeCircleFactory : public UPCGExShapeBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExShapeCircleConfig Config;

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Builder|Params", meta=(PCGExNodeLibraryDoc="shapes/shape-circle"))
class UPCGExCreateShapeCircleSettings : public UPCGExShapeBuilderFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ShapeBuilderCircle, "Shape : Circle", "Create points in a circular shape.", FName("Circle"))

#endif
	//~End UPCGSettings

	/** Shape properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExShapeCircleConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}
};
