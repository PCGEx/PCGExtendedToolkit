// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchAttrToAttr.h"
#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExMatchTagToAttr.generated.h"

class UPCGSettings;
class UPCGNode;
struct FPCGExTaggedData;

namespace PCGExData
{
	template <typename T>
	class TAttributeBroadcaster;
}

USTRUCT(BlueprintType)
struct FPCGExMatchTagToAttrConfig : public FPCGExMatchRuleConfigBase
{
	GENERATED_BODY()

	FPCGExMatchTagToAttrConfig()
		: FPCGExMatchRuleConfigBase()
	{
	}

	/** Tag name value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Tag Name"))
	FPCGExInputShorthandNameString TagNameValue = FPCGExInputShorthandNameString(FName("ReadTagFrom"), FString(TEXT("TagOnInput")), false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType TagNameInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName TagNameAttribute_DEPRECATED = FName("ReadTagFrom");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FString TagName_DEPRECATED = TEXT("TagOnInput");

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Match"))
	EPCGExStringMatchMode NameMatch = EPCGExStringMatchMode::Equals;

	/** Whether to do a tag value match or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bDoValueMatch = false;

	/** Expected value type, this is a strict check. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bDoValueMatch"))
	EPCGExComparisonDataType ValueType = EPCGExComparisonDataType::Numeric;

	/** Attribute to read tag name value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoValueMatch"))
	FPCGAttributePropertyInputSelector ValueAttribute;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Comparison", EditCondition="bDoValueMatch && ValueType == EPCGExComparisonDataType::Numeric", EditConditionHides))
	EPCGExComparison NumericComparison = EPCGExComparison::NearlyEqual;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="bDoValueMatch && ValueType == EPCGExComparisonDataType::Numeric && (NumericComparison == EPCGExComparison::NearlyEqual || NumericComparison == EPCGExComparison::NearlyNotEqual)", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Comparison", EditCondition="bDoValueMatch && ValueType == EPCGExComparisonDataType::String", EditConditionHides))
	EPCGExStringComparison StringComparison = EPCGExStringComparison::Contains;

	virtual void Init() override;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
class FPCGExMatchTagToAttr : public FPCGExMatchRuleOperation
{
public:
	FPCGExMatchTagToAttrConfig Config;

	virtual bool PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources) override;

	virtual bool Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const override;

protected:
	TArray<TSharedPtr<PCGExData::TAttributeBroadcaster<FString>>> TagNameGetters;
	TArray<TSharedPtr<PCGExData::TAttributeBroadcaster<double>>> NumGetters;
	TArray<TSharedPtr<PCGExData::TAttributeBroadcaster<FString>>> StrGetters;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExMatchTagToAttrFactory : public UPCGExMatchRuleFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExMatchTagToAttrConfig Config;

	virtual bool WantsPoints() const override;

	virtual TSharedPtr<FPCGExMatchRuleOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="DataMatch", meta=(PCGExNodeLibraryDoc="utilities/data-matching/match-tags-attributes"))
class UPCGExCreateMatchTagToAttrSettings : public UPCGExMatchRuleFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(MatchTagToAttr, "Match : Tags × Attributes", "Compares attribute value on targets against tags on inputs", FName(GetDisplayName()))

#endif
	//~End UPCGSettings

	/** Rules properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExMatchTagToAttrConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
