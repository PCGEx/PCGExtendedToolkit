// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExAttributesDetails.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataCommon.h"

#include "PCGExAttributesToPins.generated.h"

class UPCGData;
class UPCGParamData;
class UPCGExPickerFactoryData;

UENUM()
enum class EPCGExAttributeToPinType : uint8
{
	Auto           = 0 UMETA(DisplayName = "Auto", ToolTip="Keep the source's own type, including structs, arrays and other extended types. The pin is untyped."),
	Float          = 1 UMETA(DisplayName = "Float"),
	Double         = 2 UMETA(DisplayName = "Double"),
	Integer32      = 3 UMETA(DisplayName = "Integer32"),
	Integer64      = 4 UMETA(DisplayName = "Integer64"),
	Vector2        = 5 UMETA(DisplayName = "Vector2"),
	Vector         = 6 UMETA(DisplayName = "Vector"),
	Vector4        = 7 UMETA(DisplayName = "Vector4"),
	Quaternion     = 8 UMETA(DisplayName = "Quaternion"),
	Transform      = 9 UMETA(DisplayName = "Transform"),
	String         = 10 UMETA(DisplayName = "String"),
	Boolean        = 11 UMETA(DisplayName = "Boolean"),
	Rotator        = 12 UMETA(DisplayName = "Rotator"),
	Name           = 13 UMETA(DisplayName = "Name"),
	SoftObjectPath = 14 UMETA(DisplayName = "Soft Object Path"),
	SoftClassPath  = 15 UMETA(DisplayName = "Soft Class Path"),
};

/** One output pin: a source value, the name it is output under, and the type its pin carries. */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSMETA_API FPCGExAttributeToPinDetails : public FPCGExAttributeSourceToTargetDetails
{
	GENERATED_BODY()

	FPCGExAttributeToPinDetails() = default;

	/** Type of the output value and of its pin. Anything but Auto converts the source value to that type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExAttributeToPinType Type = EPCGExAttributeToPinType::Auto;

	/** Target while remapping, None otherwise -- an empty Target never remaps. */
	FName GetRemappedName() const;

	/** The remapped name, or the source's own name. None for an entry that has no source yet. */
	FName GetPinLabel() const;
};

namespace PCGExAttributesToPins
{
	/** Unknown for Auto. */
	PCGEXELEMENTSMETA_API EPCGMetadataTypes GetMetadataType(EPCGExAttributeToPinType InType);

	/** A settings entry resolved into what its pin outputs. */
	struct FPin
	{
		FPCGAttributePropertyInputSelector Selector;
		FName Label = NAME_None;

		// None keeps the source's own name, resolved against each input.
		FName OutputName = NAME_None;

		// Unknown keeps the source's own type.
		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
	};

	/** Rows read from a source of NumRows elements: the first one, or every valid pick in ascending order. */
	PCGEXELEMENTSMETA_API void GetRows(const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& PickerFactories, int32 NumRows, TArray<int32>& OutRows);

	/** One pin's output for one input: a new attribute set holding only that value. Null, with a warning, when there is nothing to output. */
	PCGEXELEMENTSMETA_API UPCGParamData* Extract(FPCGExContext* Context, const FPin& Pin, const UPCGData* Data, const TArray<int32>& Rows, bool bDataDomainToElements);
}

/**
 * Isolates values of the input data, each on its own attribute set pin. A typed pin carries its type as a pin subtype,
 * so it only connects where that type is accepted.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "isolate extract split attribute set value override pin typed", PCGExNodeLibraryDoc="metadata/keys/attributes-to-pins"))
class UPCGExAttributesToPinsSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AttributesToPins, "Attributes to Pins", "Isolate values of the input data, each on its own typed attribute set pin.");

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
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** One output pin per entry. Each pin outputs, for every input, an attribute set holding only that entry's value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(TitleProperty="{Source}"))
	TArray<FPCGExAttributeToPinDetails> Attributes;

	/** Output @Data values as one regular row, which is what override pins read. Otherwise they stay @Data values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Data domain to Elements"))
	bool bDataDomainToElements = true;

	/** Entries that get a pin, in order. False when some were left out for sharing an earlier entry's pin name. */
	PCGEXELEMENTSMETA_API bool GetPins(TArray<PCGExAttributesToPins::FPin>& OutPins) const;
};

struct FPCGExAttributesToPinsContext final : FPCGExContext
{
	friend class FPCGExAttributesToPinsElement;

	TArray<PCGExAttributesToPins::FPin> Pins;
	TArray<TObjectPtr<const UPCGExPickerFactoryData>> PickerFactories;
};

class FPCGExAttributesToPinsElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributesToPins)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
