// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Core/PCGExTensorSampler.h"
#include "Details/PCGExSettingsDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExTensorHandler.generated.h"

class UPCGSettings;
class UPCGNode;
class UPCGExTensorFactoryData;

namespace PCGExTensor
{
	struct FTensorSample;
}

USTRUCT(BlueprintType)
struct PCGEXELEMENTSTENSORS_API FPCGExTensorSamplerDetails
{
	GENERATED_BODY()

	FPCGExTensorSamplerDetails()
	{
	}

	virtual ~FPCGExTensorSamplerDetails()
	{
	}

	/** Sampler type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	TSubclassOf<UPCGExTensorSampler> Sampler = UPCGExTensorSampler::StaticClass();

	/** Sampling radius. 
	 * NOTE : Whether it has any effect depends on the selected sampler. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Radius = 1;

	/** Minimum step size as fraction of base radius 
	 * NOTE : Whether it has any effect depends on the selected sampler. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.01, ClampMax=1.0))
	double MinStepFraction = 0.1;

	/** Maximum step size as fraction of base radius 
	 NOTE : Whether it has any effect depends on the selected sampler. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.1, ClampMax=2.0))
	double MaxStepFraction = 1.0;

	/** Error tolerance for step size adaptation 
	 * NOTE : Whether it has any effect depends on the selected sampler. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.001, ClampMax=0.5))
	double ErrorTolerance = 0.01;

	/** Maximum sub-steps per sample 
	 * NOTE : Whether it has any effect depends on the selected sampler. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1, ClampMax=16))
	int32 MaxSubSteps = 4;
};


USTRUCT(BlueprintType)
struct PCGEXELEMENTSTENSORS_API FPCGExTensorHandlerDetails
{
	GENERATED_BODY()

	FPCGExTensorHandlerDetails()
	{
		SizeAttribute_DEPRECATED.Update("ExtrusionSize");
	}

	virtual ~FPCGExTensorHandlerDetails()
	{
	}

	/** If enabled, sampling direction will be inverted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvert = false;

	/** If enabled, normalize sampling. This effectively negates the influence of effectors potency. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bNormalize = true;

	/** Size applied after normalization. This will be scaled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Size", EditCondition="bNormalize", EditConditionHides))
	FPCGExInputShorthandSelectorDouble Size = FPCGExInputShorthandSelectorDouble(FName("ExtrusionSize"), 100, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType SizeInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector SizeAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double SizeConstant_DEPRECATED = 100;

#pragma endregion

	/** Uniform scale factor applied to sampling after all other mutations are accounted for. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double UniformScale = 1;

	/** Uniform scale factor applied to sampling after all other mutations are accounted for. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTensorSamplerDetails SamplerSettings;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

namespace PCGExTensor
{
	class PCGEXELEMENTSTENSORS_API FTensorsHandler : public TSharedFromThis<FTensorsHandler>
	{
		TArray<TSharedPtr<PCGExTensorOperation>> Tensors;
		FPCGExTensorHandlerDetails Config;
		TSharedPtr<PCGExDetails::TSettingValue<double>> Size;

		UPCGExTensorSampler* SamplerInstance = nullptr;

	public:
		explicit FTensorsHandler(const FPCGExTensorHandlerDetails& InConfig);

		bool Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExTensorFactoryData>>& InFactories, const TSharedPtr<PCGExData::FFacade>& InDataFacade);
		bool Init(FPCGExContext* InContext, const FName InPin, const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const;
	};
}
