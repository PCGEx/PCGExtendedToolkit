// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "UObject/Object.h"

#include "PCGExProxyDataBlending.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Factories/PCGExOperation.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExBlendOpFactory.generated.h"

class UPCGSettings;
class UPCGNode;

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExBlending
{
	class FProxyDataBlender;

	const FName PreviousAttributeName = TEXT("#Previous");
	const FName PreviousNameAttributeName = TEXT("#PreviousName");
}

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExAttributeBlendWeight
{
	GENERATED_BODY()

	FPCGExAttributeBlendWeight()
	{
		LocalWeightCurve.EditorCurveData.AddKey(0, 0);
		LocalWeightCurve.EditorCurveData.AddKey(1, 1);
	}

	~FPCGExAttributeBlendWeight() = default;

	/** Weight. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight"))
	FPCGExInputShorthandSelectorDouble WeightValue = FPCGExInputShorthandSelectorDouble(FName("Weight"), 0.5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType WeightInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector WeightAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Weight_DEPRECATED = 0.5;

#pragma endregion

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve the weight value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Weight Curve", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalWeightCurve;

	/** Curve the weight value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight Curve", EditCondition="!bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> WeightCurve = TSoftObjectPtr<UCurveFloat>(PCGExCurves::WeightDistributionLinear);

	PCGExFloatLUT ScoreLUT = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails WeightCurveLookup;

	void Init();

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExAttributeBlendConfig
{
	GENERATED_BODY()

	FPCGExAttributeBlendConfig()
	{
		OperandA.Update("@Last");
		OperandB.Update("@Last");
		OutputTo.Update("Result");
	}

	~FPCGExAttributeBlendConfig() = default;

	UPROPERTY(meta=(PCG_NotOverridable, HideInDetailPanel))
	bool bRequiresWeight = false;

	/** BlendMode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExABBlendingType BlendMode = EPCGExABBlendingType::Average;

	/** Operand A Source. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExOperandSource OperandASource = EPCGExOperandSource::Attribute;

	/** Operand A. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Use a separate source for Operand B. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bUseOperandB = false;

	/** Operand B. If not enabled, will re-use Operand A' input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseOperandB"))
	FPCGAttributePropertyInputSelector OperandB;

	/** Operand B Source. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Inline", EditCondition="bUseOperandB", HideEditConditionToggle))
	EPCGExOperandSource OperandBSource = EPCGExOperandSource::Attribute;

	/** Choose where to output the result of the A/B blend */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExBlendOpOutputMode OutputMode = EPCGExBlendOpOutputMode::SameAsA;

	/** Output to (AB blend). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Output To", EditCondition="OutputMode == EPCGExBlendOpOutputMode::New || OutputMode == EPCGExBlendOpOutputMode::Transient", EditConditionHides))
	FPCGAttributePropertyInputSelector OutputTo;

	/** If enabled, when a node uses multiple sources for blending, the value will be reset to 0 for some specific BlendModes so as to not account for inherited values. Default is true, as it is usually the most desirable behavior. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bResetValueBeforeMultiSourceBlend = true;

	/** Which type should be used for the output value. Only used if the output is not a point property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable), AdvancedDisplay)
	EPCGExOperandAuthority OutputType = EPCGExOperandAuthority::Auto;

	/** Which type should be used for the output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Type", EditCondition="OutputType == EPCGExOperandAuthority::Custom", EditConditionHides), AdvancedDisplay)
	EPCGMetadataTypes CustomType = EPCGMetadataTypes::Double;

	/** Weight settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bRequiresWeight", EditConditionHides, HideEditConditionToggle))
	FPCGExAttributeBlendWeight Weighting;

	void Init();
};

/**
 * 
 */
class PCGEXBLENDING_API FPCGExBlendOperation : public FPCGExOperation
{
public:
	FPCGExAttributeBlendConfig Config;

	TSharedPtr<PCGExData::FFacade> WeightFacade;

	TSharedPtr<PCGExData::FFacade> Source_A_Facade;
	PCGExData::EIOSide SideA = PCGExData::EIOSide::In;

	TSharedPtr<PCGExData::FFacade> Source_B_Facade;
	PCGExData::EIOSide SideB = PCGExData::EIOSide::In;

	TSharedPtr<PCGExData::FFacade> TargetFacade;

	TSharedPtr<PCGExData::FFacade> ConstantA;
	TSharedPtr<PCGExData::FFacade> ConstantB;

	bool bUsedForMultiBlendOnly = false;

	// Mirrored from the owning factory at CreateOperation time; gates the Weight setting-value's
	// consumable auto-registration (ops carry no factory backref).
	bool bCleanupConsumableAttributes = false;

	int32 OpIdx = -1;
	TSharedPtr<TArray<TSharedPtr<FPCGExBlendOperation>>> SiblingOperations;

	virtual bool PrepareForData(FPCGExContext* InContext);

	virtual void BlendAutoWeight(const int32 SourceIndex, const int32 TargetIndex);
	virtual void Blend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight);
	virtual void Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double InWeight);

	virtual void BlendScope(const PCGExMT::FScope& Scope);
	virtual void BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask);
	virtual void BlendScope(const PCGExMT::FScope& Scope, const double InWeight);

	virtual PCGEx::FOpStats BeginMultiBlend(const int32 TargetIndex);
	virtual void MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight, PCGEx::FOpStats& Tracker);
	virtual void EndMultiBlend(const int32 TargetIndex, PCGEx::FOpStats& Tracker);

	virtual void CompleteWork(TSet<TSharedPtr<PCGExData::IBuffer>>& OutDisabledBuffers);

protected:
	bool CopyAndFixSiblingSelector(FPCGExContext* InContext, FPCGAttributePropertyInputSelector& Selector) const;

	TSharedPtr<PCGExDetails::TSettingValue<double>> Weight;
	TSharedPtr<PCGExBlending::FProxyDataBlender> Blender;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Blend Op"))
struct FPCGExDataTypeInfoBlendOp : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXBLENDING_API)
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Blending")
class PCGEXBLENDING_API UPCGExBlendOpFactory : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoBlendOp)

	FPCGExAttributeBlendConfig Config;
	TSharedPtr<PCGExData::FFacade> ConstantA;
	TSharedPtr<PCGExData::FFacade> ConstantB;

	virtual bool IsMonolithic() const
	{
		return false;
	}

	virtual TSharedPtr<FPCGExBlendOperation> CreateOperation(FPCGExContext* InContext) const;

	/**
	 * Resolves the effective blend configs this factory contributes for a given source facade.
	 * Default returns the single static Config; monolithic factories enumerate the source attributes.
	 * Resolution is data-dependent but target-independent, so results can be computed once
	 * (single-threaded) and shared across processors -- see PCGExBlending::FBlendOpsSchema.
	 */
	virtual bool ResolveConfigs(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FFacade>& InSourceAFacade,
		TArray<FPCGExAttributeBlendConfig>& OutConfigs,
		const TSet<FName>* InSupersedeNames = nullptr) const;

	/** Creates one or more operations from this factory: ResolveConfigs + one CreateOperation() per resolved config. */
	virtual bool CreateOperations(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FFacade>& InSourceAFacade,
		const TSharedPtr<PCGExData::FFacade>& InTargetFacade,
		TArray<TSharedPtr<FPCGExBlendOperation>>& OutOperations,
		const TSet<FName>* InSupersedeNames = nullptr) const;

	/** Extracts the output target name from a blend config, using $PropertyName convention for point properties. */
	static FName GetOutputTargetName(const FPCGExAttributeBlendConfig& InConfig);

	virtual bool WantsPreparation(FPCGExContext* InContext) override;

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

	virtual void RegisterBuffersDependenciesForSourceA(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;
	virtual void RegisterBuffersDependenciesForSourceB(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;
};
