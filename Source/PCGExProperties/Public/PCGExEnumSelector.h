// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/StructOpsTypeTraits.h"

#include "PCGExEnumSelector.generated.h"

struct FPropertyTag;

/**
 * Type-safe enum selection: pairs a UEnum* (native or Blueprint) with one of its values.
 *
 * A field tagged meta=(PCGExEnumFlags) instead stores an OR of flag masks when Class is a Bitflags enum
 * (PCGExEnumSelector::GetFlagMask). Also loads data saved as the engine's FEnumSelector, which has the same fields.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExEnumSelector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enum")
	TObjectPtr<UEnum> Class = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enum")
	int64 Value = 0;

#if WITH_EDITORONLY_DATA
	/** Set by a load that read the engine's FEnumSelector layout; a host that converts such data clears it. Not serialized. */
	bool bLoadedFromEngineLayout = false;
#endif

	bool IsValid() const
	{
		return Class != nullptr;
	}

	FText GetDisplayName() const;
	FString GetCultureInvariantDisplayName() const;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

#if WITH_EDITOR
	/**
	 * For a single-value field loading data older than 1.78.6, whose picker stored a flag mask for any Bitflags enum:
	 * turns a one-flag mask into that flag's value. No-op after a load from the engine layout, which stored values.
	 * @return true when Value changed.
	 */
	bool EDITOR_RestoreValueFromFlagMask();
#endif
};

template <>
struct TStructOpsTypeTraits<FPCGExEnumSelector> : public TStructOpsTypeTraitsBase2<FPCGExEnumSelector>
{
	enum
	{
		WithStructuredSerializeFromMismatchedTag = true,
	};
};

#if WITH_EDITOR
namespace PCGExEnumSelector
{
	/** UPROPERTY meta opting an FPCGExEnumSelector field into flag masks for Bitflags enums. */
	inline const FName FlagsMeta = FName(TEXT("PCGExEnumFlags"));

	/** A UENUM with the Bitflags meta, or a Blueprint enum with Bitmask Flags ticked. */
	PCGEXPROPERTIES_API bool IsFlagsEnum(const UEnum* Enum);

	/** Entries the picker never lists: Hidden, Spacer, BlueprintInternalUseOnly, HiddenByDefault, and the generated _MAX. */
	PCGEXPROPERTIES_API bool IsHiddenEntry(const UEnum* Enum, int32 Index);

	/** Mask of a Bitflags enum entry: its value under UseEnumValuesAsMaskValuesInEditor, else 1 << value; 0 when out of range. */
	PCGEXPROPERTIES_API int64 GetFlagMask(const UEnum* Enum, int32 Index);

	/** GetFlagMask for an entry value. Reads only the enum's metadata, never its entries, so it is safe mid-load. */
	PCGEXPROPERTIES_API int64 GetFlagMaskOfValue(const UEnum* Enum, int64 Value);

	/** Display names of the listed flags set in Mask, joined with " | "; "(None)" for 0. */
	PCGEXPROPERTIES_API FText GetFlagsDisplayText(const UEnum* Enum, int64 Mask);
}
#endif
