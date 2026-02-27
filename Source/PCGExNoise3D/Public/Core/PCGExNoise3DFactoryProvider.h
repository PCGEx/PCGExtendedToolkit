// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExNoise3DCommon.h"
#include "Factories/PCGExFactoryProvider.h"
#include "UObject/Object.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Factories/PCGExFactoryData.h"
#include "Math/PCGExMathContrast.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExNoise3DFactoryProvider.generated.h"

#define PCGEX_NOISE3D_FACTORY_BOILERPLATE \
virtual void RegisterAssetDependencies(FPCGExContext* InContext) const override;

#define PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(_TYPE, _REGISTER_ASSET_BODY)\
void UPCGExNoise3DFactory##_TYPE::RegisterAssetDependencies(FPCGExContext* InContext) const{\
	Super::RegisterAssetDependencies(InContext); }

#define PCGEX_FORWARD_NOISE3D_FACTORY \
	NewFactory->Priority = Priority; \
	NewFactory->Config = Config; \
	NewFactory->Config.Init(); \
	NewFactory->ConfigBase = NewFactory->Config;

#define PCGEX_FORWARD_NOISE3D_CONFIG \
	NewOperation->Frequency = Config.Frequency; \
	NewOperation->Seed = Config.Seed; \
	NewOperation->bInvert = Config.bInvert; \
	NewOperation->BlendMode = Config.BlendMode; \
	NewOperation->WeightFactor = Config.WeightFactor; \
	NewOperation->RemapLUT = Config.RemapLUT; \
	NewOperation->bApplyTransform = Config.bApplyTransform; \
	NewOperation->Transform = Config.Transform; \
	NewOperation->Contrast = Config.Contrast; \
	NewOperation->ContrastCurve = Config.ContrastCurve; \
	NewOperation->Scale = Config.Scale;

class FPCGExNoise3DOperation;

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Noise3D"))
struct FPCGExDataTypeInfoNoise3D : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXNOISE3D_API)
};

USTRUCT(BlueprintType)
struct PCGEXNOISE3D_API FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoise3DConfigBase()
	{
		LocalRemapCurve.EditorCurveData.AddKey(0, 0);
		LocalRemapCurve.EditorCurveData.AddKey(1, 1);
	}

	~FPCGExNoise3DConfigBase()
	{
	}

	/** The weight factor for this Noise3D (used when combining multiple noise sources). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	double WeightFactor = 1;

	/** Blend mode when stacked against other noises */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	EPCGExNoiseBlendMode BlendMode = EPCGExNoiseBlendMode::Blend;

	/** Invert the noise output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	bool bInvert = false;

	/** Curve the value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta = (PCG_NotOverridable, DisplayName="Remap Curve"))
	FRuntimeFloatCurve LocalRemapCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails RemapCurveLookup;

	PCGExFloatLUT RemapLUT = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	int32 Seed = 1337;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bApplyTransform = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bApplyTransform"))
	FTransform Transform = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.000001", DIsplayPriority=-1))
	double Frequency = 0.01;

	/** Contrast adjustment (1.0 = no change, >1 = more contrast, <1 = less) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Constrast", meta=(PCG_Overridable, DisplayPriority=-1))
	double Contrast = 1.0;

	/** Contrast curve type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Constrast", meta=(PCG_Overridable, DisplayPriority=-1, EditCondition="Contrast != 1.0"))
	EPCGExContrastCurve ContrastCurve = EPCGExContrastCurve::Power;

	/** Post-remap scale multiplier (applied after invert, remap curve, and contrast) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	double Scale = 1.0;

	void Init();
};


UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXNOISE3D_API UPCGExNoise3DFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoNoise3D)

	FPCGExNoise3DConfigBase ConfigBase;

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::Noise3D; }

	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperation(FPCGExContext* InContext) const;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Noise")
class PCGEXNOISE3D_API UPCGExNoise3DFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoNoise3D)

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AbstractNoise3D, "Noise3D Definition", "Creates a single Noise3D computational node, to be used with nodes that support it.")
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Noise3D); }
#endif

	virtual FName GetMainOutputPin() const override { return PCGExNoise3D::Labels::OutputNoise3DLabel; }
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Noise priority, matters for blending and weighting.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;
};
