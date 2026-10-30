// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExAttributesDetails.h"
#include "Details/PCGExClampDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExRemapDetails.h"
#include "UObject/Object.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExAttributeRemap.generated.h"

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

namespace PCGExData
{
	class IBufferProxy;
}

USTRUCT(BlueprintType)
struct PCGEXELEMENTSMETA_API FPCGExComponentRemapRule
{
	GENERATED_BODY()

	FPCGExComponentRemapRule()
	{
	}

	FPCGExComponentRemapRule(const FPCGExComponentRemapRule& Other)
		: InputClampDetails(Other.InputClampDetails)
		  , RemapDetails(Other.RemapDetails)
		  , OutputClampDetails(Other.OutputClampDetails)
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Clamp Input"))
	FPCGExClampDetails InputClampDetails;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExRemapDetails RemapDetails;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Clamp Output"))
	FPCGExClampDetails OutputClampDetails;

	TSharedPtr<PCGExMT::TScopedNumericValue<double>> MinCache;
	TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxCache;
	TSharedPtr<PCGExDetails::TSettingValue<double>> SnapCache;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/modify/attribute-remap"))
class UPCGExAttributeRemapSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(AttributeRemap, "Attribute Remap", "Remap a single property or attribute.", FName(GetDisplayName()));

	virtual TArray<FText> GetNodeTitleAliases() const override;
	
	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

#pragma region DEPRECATED

	// Deprecated, old source/target
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName SourceAttributeName_DEPRECATED = NAME_None;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName TargetAttributeName_DEPRECATED = NAME_None;

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAttributeSourceToTargetDetails Attributes;

	/* If enabled, will auto-cast integer to double. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bAutoCastIntegerToDouble = false;

	/** The default remap rule, used for single component values, or first component (X), or all components if no individual override is specified. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Remap (Default)"))
	FPCGExComponentRemapRule BaseRemap;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOverrideComponent2;

	/** Remap rule used for second (Y) value component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, EditCondition="bOverrideComponent2", DisplayName="Remap (2nd Component)"))
	FPCGExComponentRemapRule Component2RemapOverride;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOverrideComponent3;

	/** Remap rule used for third (Z) value component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, EditCondition="bOverrideComponent3", DisplayName="Remap (3rd Component)"))
	FPCGExComponentRemapRule Component3RemapOverride;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOverrideComponent4;

	/** Remap rule used for fourth (W) value component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Individual Components", meta = (PCG_NotOverridable, EditCondition="bOverrideComponent4", DisplayName="Remap (4th Component)"))
	FPCGExComponentRemapRule Component4RemapOverride;

#if WITH_EDITOR
	FString GetDisplayName() const;
#endif

private:
	friend class FPCGExAttributeRemapElement;
};

struct FPCGExAttributeRemapContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExAttributeRemapElement;

	FPCGExComponentRemapRule RemapSettings[4];
	int32 RemapIndices[4];

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExAttributeRemapElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributeRemap)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExAttributeRemap
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAttributeRemapContext, UPCGExAttributeRemapSettings>
	{
		EPCGMetadataTypes UnderlyingType = EPCGMetadataTypes::Unknown;
		int32 Dimensions = 0;

		TArray<TSharedPtr<PCGExData::IBufferProxy>> InputProxies;
		TArray<TSharedPtr<PCGExData::IBufferProxy>> OutputProxies;

		TArray<FPCGExComponentRemapRule> Rules;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
