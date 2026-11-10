// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExEnumSelector.h"

#include "Elements/ControlFlow/PCGControlFlow.h"
#include "UObject/PropertyTag.h"

#define LOCTEXT_NAMESPACE "PCGExEnumSelector"

#pragma region FPCGExEnumSelector

FText FPCGExEnumSelector::GetDisplayName() const
{
	if (!Class)
	{
		return FText::GetEmpty();
	}
	return Class->GetDisplayNameTextByValue(Value);
}

FString FPCGExEnumSelector::GetCultureInvariantDisplayName() const
{
	if (!Class)
	{
		return FString();
	}
	return Class->GetDisplayNameTextByValue(Value).BuildSourceString();
}

bool FPCGExEnumSelector::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	if (!Tag.GetType().IsStruct(FEnumSelector::StaticStruct()->GetFName()))
	{
		return false;
	}

	// Starts from the current values: delta serialization omits the fields that match the archetype.
	FEnumSelector Legacy;
	Legacy.Class = Class;
	Legacy.Value = Value;
	FEnumSelector::StaticStruct()->SerializeItem(Slot, &Legacy, nullptr);
	Class = Legacy.Class;
	Value = Legacy.Value;
#if WITH_EDITORONLY_DATA
	bLoadedFromEngineLayout = true;
#endif
	return true;
}

#if WITH_EDITOR
bool FPCGExEnumSelector::EDITOR_RestoreValueFromFlagMask()
{
	const uint64 Bits = static_cast<uint64>(Value);
	if (bLoadedFromEngineLayout || !PCGExEnumSelector::IsFlagsEnum(Class) || Bits == 0 || (Bits & (Bits - 1)) != 0)
	{
		return false;
	}

	for (int32 i = 0, Num = Class->NumEnums(); i < Num; ++i)
	{
		if (PCGExEnumSelector::IsHiddenEntry(Class, i) || PCGExEnumSelector::GetFlagMask(Class, i) != Value)
		{
			continue;
		}
		const int64 FlagValue = Class->GetValueByIndex(i);
		const bool bChanged = FlagValue != Value;
		Value = FlagValue;
		return bChanged;
	}
	return false;
}
#endif

#pragma endregion

#if WITH_EDITOR
#pragma region PCGExEnumSelector

namespace PCGExEnumSelector
{
	bool IsFlagsEnum(const UEnum* Enum)
	{
		return Enum && Enum->HasMetaData(TEXT("Bitflags"));
	}

	bool IsHiddenEntry(const UEnum* Enum, const int32 Index)
	{
		return Enum->HasMetaData(TEXT("Hidden"), Index)
			|| Enum->HasMetaData(TEXT("Spacer"), Index)
			|| Enum->HasMetaData(TEXT("BlueprintInternalUseOnly"), Index)
			|| Enum->HasMetaData(TEXT("HiddenByDefault"), Index)
			|| Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"), ESearchCase::CaseSensitive);
	}

	int64 GetFlagMask(const UEnum* Enum, const int32 Index)
	{
		return GetFlagMaskOfValue(Enum, Enum->GetValueByIndex(Index));
	}

	int64 GetFlagMaskOfValue(const UEnum* Enum, const int64 Value)
	{
		if (Enum->HasMetaData(TEXT("UseEnumValuesAsMaskValuesInEditor")))
		{
			return Value;
		}
		return (Value < 0 || Value >= 64) ? 0 : static_cast<int64>(1ULL << static_cast<uint64>(Value));
	}

	FText GetFlagsDisplayText(const UEnum* Enum, const int64 Mask)
	{
		if (Mask == 0)
		{
			return LOCTEXT("NoFlags", "(None)");
		}

		TArray<FString> Parts;
		for (int32 i = 0, Num = Enum->NumEnums(); i < Num; ++i)
		{
			if (IsHiddenEntry(Enum, i))
			{
				continue;
			}
			const int64 Bit = GetFlagMask(Enum, i);
			if (Bit != 0 && (Mask & Bit) == Bit)
			{
				Parts.Add(Enum->GetDisplayNameTextByIndex(i).ToString());
			}
		}
		return FText::FromString(FString::Join(Parts, TEXT(" | ")));
	}
}

#pragma endregion
#endif

#undef LOCTEXT_NAMESPACE
