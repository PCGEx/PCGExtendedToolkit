// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"

#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"

#include "PCGExSmooth.generated.h"

class UPCGExBlendOpFactory;
class UPCGExSmoothingInstancedFactory;
class FPCGExSmoothingOperation;

namespace PCGExBlending
{
	class IBlender;
	class FBlendOpsManager;
	class FMetadataBlender;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/transform/path-smooth"))
class UPCGExSmoothSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PathSmooth, "Path : Smooth", "Smooth paths points.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
#if WITH_EDITORONLY_DATA
	// UObject interface
	virtual void PostInitProperties() override;
	// End of UObject interface
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get smoothed.", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bPreserveStart = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bPreserveEnd = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExSmoothingInstancedFactory> SmoothingMethod;

	/** Blend between unsmoothed (0) and fully smoothed (1) result. Negative values invert the smoothing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble11 Influence = FPCGExInputShorthandSelectorDouble11(FName("@Last"), 1.0, false);

	/** The amount of smoothing applied. Range of this value is highly dependant on the chosen smoothing method. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Smoothing"))
	FPCGExInputShorthandSelectorDoubleAbs SmoothingAmount = FPCGExInputShorthandSelectorDoubleAbs(FName("@Last"), 5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType InfluenceInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector InfluenceAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double InfluenceConstant_DEPRECATED = 1.0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType SmoothingAmountType_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector SmoothingAmountAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double SmoothingAmountConstant_DEPRECATED = 5;

#pragma endregion

	/** Static multiplier for the local smoothing amount. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.001))
	double ScaleSmoothingAmountAttribute = 1;

	/** How to blend data from sampled points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable))
	EPCGExBlendingInterface BlendingInterface = EPCGExBlendingInterface::Individual;

	/** Blending settings used to smooth attributes.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="BlendingInterface == EPCGExBlendingInterface::Monolithic", EditConditionHides))
	FPCGExBlendingDetails BlendingSettings = FPCGExBlendingDetails(EPCGExBlendingType::Average);
};

struct FPCGExSmoothContext final : FPCGExPathProcessorContext
{
	friend class FPCGExSmoothElement;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	UPCGExSmoothingInstancedFactory* SmoothingMethod = nullptr;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSmoothElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Smooth)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSmooth
{
	const FName SourceOverridesSmoothing = TEXT("Overrides : Smoothing");

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSmoothContext, UPCGExSmoothSettings>
	{
		int32 NumPoints = 0;

		TSharedPtr<PCGExDetails::TSettingValue<double>> Influence;
		TSharedPtr<PCGExDetails::TSettingValue<double>> Smoothing;

		TSharedPtr<PCGExBlending::FMetadataBlender> MetadataBlender;
		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;
		TSharedPtr<PCGExBlending::IBlender> DataBlender;

		TSharedPtr<FPCGExSmoothingOperation> SmoothingOperation;

		bool bClosedLoop = false;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};
}
