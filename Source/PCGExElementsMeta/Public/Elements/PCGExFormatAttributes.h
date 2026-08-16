// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExFormatAttributes.generated.h"

UENUM(BlueprintType)
enum class EPCGExFormatWriteMode : uint8
{
	InPlace      = 0 UMETA(DisplayName = "In Place", ToolTip="Overwrite the source attribute."),
	NewAttribute = 1 UMETA(DisplayName = "New Attribute", ToolTip="Write the formatted value to a new attribute (original attribute is preserved)."),
};

UENUM(BlueprintType)
enum class EPCGExFormatReplacementMode : uint8
{
	Sequential = 0 UMETA(DisplayName = "Sequential", ToolTip="Apply each rule in order; each rule operates on the result of the previous one."),
	SinglePass = 1 UMETA(DisplayName = "Single Pass", ToolTip="Scan the original string once. Replacement values are never re-scanned for further token matches. Useful when replacement values may contain other rule tokens."),
};

UENUM(BlueprintType)
enum class EPCGExTokenSourceMode : uint8
{
	Self     = 0 UMETA(DisplayName = "Self", ToolTip="Read the replacement value from the data being processed."),
	External = 1 UMETA(DisplayName = "External", ToolTip="Read the replacement value from an external source pin."),
};

USTRUCT(BlueprintType)
struct FPCGExFormatTokenRule
{
	GENERATED_BODY()

	/** Text to find inside target string attributes. When attribute-driven, read from this rule's source (per Source Mode), with the same broadcast/per-row pairing as the replacement value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Token"))
	FPCGExInputShorthandSelectorString TokenValue;

	/** Where to read this rule's replacement value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExTokenSourceMode SourceMode = EPCGExTokenSourceMode::Self;

	/** Attribute on the source whose value replaces the token. Any type -- values are converted to string. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGAttributePropertyInputSelector Source;

	/** Name of the external input pin to read the source from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="SourceMode == EPCGExTokenSourceMode::External", EditConditionHides))
	FName ExternalPin = FName("Format Source");

	/** If non-empty, this rule applies only to the listed target attributes. Empty = applies to every target. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> TargetAttributes;

	/** Use FallbackValue when the source attribute is missing or unreadable. Also suppresses the missing-source warning for this rule. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bHasFallback = false;

	/** Literal value substituted in place of the source attribute when the source can't be read. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bHasFallback"))
	FString FallbackValue;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FString Token_DEPRECATED;

#pragma endregion
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/modify/format-attributes"))
class UPCGExFormatAttributesSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(FormatAttributes, "Format Attributes", "Replace literal tokens in FName/FString attributes using values from other attributes (self or external).");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}

	virtual bool HasDynamicPins() const override
	{
		return true;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Whether to overwrite the source attribute or write the result to a new one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExFormatWriteMode WriteMode = EPCGExFormatWriteMode::InPlace;

	/** Suffix appended to each target attribute name when WriteMode is NewAttribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="WriteMode == EPCGExFormatWriteMode::NewAttribute", EditConditionHides))
	FString NewAttributeSuffix = TEXT("_Formatted");

	/** How rule replacements compose with each other. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExFormatReplacementMode ReplacementMode = EPCGExFormatReplacementMode::Sequential;

	/** Attributes the rules will operate on. Each must resolve to a FName or FString attribute; others are skipped with a warning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGAttributePropertyInputSelector> TargetAttributes;

	/** A list of selectors separated by a comma, for easy overrides. Will be appended to the existing array. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString CommaSeparatedAttributeSelectors;

	/** Rules applied in order, top to bottom. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGExFormatTokenRule> Rules;

	/** Suppress warnings when a rule's source attribute can't be read and the rule has no fallback. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietMissingSourceWarning = false;

	/** Collect distinct external pin names referenced by rules with SourceMode == External. */
	void GatherExternalPinNames(TSet<FName>& OutPins) const;
};

struct FPCGExFormatAttributesContext final : FPCGExContext
{
	friend class FPCGExFormatAttributesElement;

	/** Settings->TargetAttributes merged with any selectors parsed from CommaSeparatedAttributeSelectors. Populated in Boot. */
	TArray<FPCGAttributePropertyInputSelector> TargetSelectors;

	/** Resolved external pin -> tagged data list. Populated in Boot from the node's external input pins. */
	TMap<FName, TArray<FPCGTaggedData>> ExternalSources;
};

class FPCGExFormatAttributesElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FormatAttributes)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
