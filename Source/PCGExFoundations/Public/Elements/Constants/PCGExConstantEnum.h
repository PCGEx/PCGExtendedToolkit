// Copyright 2026 Edward Boucher and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGSettings.h"

#include "PCGExEnumSelector.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExEnumCommon.h"
#include "PCGExConstantEnum.generated.h"

struct FPCGExBitmask;


UENUM(BlueprintType)
enum class EPCGExEnumConstantOutputType : uint8
{
	EECOT_Attribute = 0 UMETA(DisplayName="Attribute Set"),
	EECOT_String    = 1 UMETA(Hidden),
	// Unsure if this is needed since there's the option to output name and description
	EECOT_Tag = 2 UMETA(Hidden) // Hidden for now since this might actually be better as a separate node (Tag With Enum or similar)
};

// TODO (perhaps) - 'Selection' and 'Selection to Multiple Pins'
UENUM(BlueprintType)
enum class EPCGExEnumOutputMode : uint8
{
	EEOM_Single                  = 0 UMETA(DisplayName="Single", Tooltip="Output a single enum value"),
	EEOM_All                     = 1 UMETA(DisplayName="All", ToolTip="Output a dataset containing all the enum names and values"),
	EEOM_AllToMultiplePins       = 2 UMETA(DisplayName="All to Separate Outputs", Tooltip="Output all values in the enum to different pins"),
	EEOM_Selection               = 3 UMETA(DisplayName="Selection", Tooltip="Select values to output as one dataset"),
	EEOM_SelectionToMultiplePins = 4 UMETA(DisplayName="Selection to Separate Outputs", Tooltip="Select values to output to multiple pins")
};

namespace PCGExConstantEnumConstants
{
	using FMapping = TTuple<FName, FName, int64, int32>;

	static const FName SingleOutputPinName = "Out";
	static const FName BitflagOutputPinName = "Flags";

	static const FName KeyOutputAttribute = "Key";
	static const FName ValueOutputAttribute = "Value";
	static const FName DescriptionAttribute = "Description";
}

UCLASS(BlueprintType, ClassGroup=(Procedural), meta=(AutoExpandCategories="Settings|Output Attributes", PCGExNodeLibraryDoc="metadata/keys/enum-constant"))
class UPCGExConstantEnumSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(EnumConstant, "Enum Constant", "Break an enum into handy constant values.", FName(GetDisplayName())); // Tim says nope! :D
	FString GetDisplayName() const;

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	};

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	};
	// End unrolling of Tim's lovely macro
#endif

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void FillEnabledExportValues();
	virtual void OnOverrideSettingsDuplicatedInternal(bool bSkippedPostLoad) override;

	virtual bool HasDynamicPins() const override
	{
		return true;
	};

	TObjectPtr<UEnum> GetEnumClass() const;

	UPROPERTY()
	EPCGExEnumConstantSourceType Source_DEPRECATED = EPCGExEnumConstantSourceType::Selector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category= Settings)
	EPCGExEnumOutputMode OutputMode = EPCGExEnumOutputMode::EEOM_All;

	UPROPERTY()
	TObjectPtr<UEnum> PickerEnum_DEPRECATED;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(PCG_NotOverridable), Category= Settings)
	FPCGExEnumSelector SelectedEnum;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category= Settings, EditFixedSize, meta=( ReadOnlyKeys, EditCondition="OutputMode == EPCGExEnumOutputMode::EEOM_Selection || OutputMode == EPCGExEnumOutputMode::EEOM_SelectionToMultiplePins", EditConditionHides ))
	TMap<FName, bool> EnabledExportValues;

	// Hidden for now
	UPROPERTY(/*BlueprintReadWrite, EditAnywhere, Category=Settings*/)
	EPCGExEnumConstantOutputType OutputType = EPCGExEnumConstantOutputType::EECOT_Attribute;

	/** Output the enum value keys (the short names used in C++). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Keys")
	bool OutputEnumKeys = false;

	/**
	 * Strip the namespace prefix from enum keys.
	 * 'SomeEnum::SomeKey' becomes just 'SomeKey'.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Keys", meta=(EditCondition="OutputEnumKeys"))
	bool StripEnumNamespaceFromKey = true;

	/** Attribute name for the enum key output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Keys", meta=(EditCondition="OutputEnumKeys"))
	FName KeyAttribute = "Key";

	/** Output the enum value descriptions (human-readable display names). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Descriptions")
	bool OutputEnumDescriptions = false;

	/** Attribute name for the description output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Descriptions", meta=(EditCondition="OutputEnumDescriptions"))
	FName DescriptionAttribute = "Description";

	/** Whether to output the numeric enum values.
	 * Note: will be output as int64 to match behaviour in native PCG */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Values")
	bool OutputEnumValues = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Attributes|Values", meta=(EditCondition="OutputEnumValues"))
	FName ValueOutputAttribute = "Value";

	TArray<PCGExConstantEnumConstants::FMapping> GetEnumValueMap() const;
	UFUNCTION(BlueprintCallable, Category="Config")
	FName GetEnumName() const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Bitflags")
	bool bOutputFlags = false;

	/** Whether to output the enum as a bitmask, and which name should the attribute have in the output attribute set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Bitflags", meta=(EditCondition="bOutputFlags"))
	FName FlagsName = FName("Flags");

	/** Bit to start writing the enum bits to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Output Bitflags", meta=(EditCondition="bOutputFlags", ClampMin=0, ClampMax=63))
	uint8 FlagBitOffset = 0;

	// Imitating behaviour in the native PCGSwitch.h
	UPROPERTY()
	TArray<FName> CachedPinLabels;
	void CachePinLabels();

protected:
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& PropName) const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override
	{
		return {};
	};
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGExConstantEnumElement : public IPCGExElement
{
protected:
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	static void StageEnumValuesSeparatePins(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, const TArray<PCGExConstantEnumConstants::FMapping>& ValueData, FPCGExBitmask& OutBitflags);

	static void StageEnumValuesSinglePin(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, const TArray<PCGExConstantEnumConstants::FMapping>& ValueData, FPCGExBitmask& OutBitflags);

	static void StageBitFlags(FPCGExContext* InContext, const UPCGExConstantEnumSettings* Settings, FPCGExBitmask& OutBitflags);

	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT
};
