// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "K2Node_PCGExPropertyBase.h"

#include "EdGraphSchema_K2.h"
#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_PCGExPropertyBase"

namespace PCGExK2PropertyBase
{
	// Pin names match the library functions' parameter names.
	const FName CollectionPinName(TEXT("Collection"));
	const FName EntryIndexPinName(TEXT("EntryIndex"));
	const FName CategoryPinName(TEXT("Category"));

	// "Entry" / "Category" / "Collection" -- the noun in node titles.
	FText GetScopeNoun(const UK2Node_PCGExPropertyBase::EScope Scope)
	{
		switch (Scope)
		{
		case UK2Node_PCGExPropertyBase::EScope::Entry: return LOCTEXT("ScopeEntry", "Entry");
		case UK2Node_PCGExPropertyBase::EScope::Category: return LOCTEXT("ScopeCategory", "Category");
		default: return LOCTEXT("ScopeCollection", "Collection");
		}
	}
}

FText UK2Node_PCGExPropertyBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::Format(
		IsSetNode() ? LOCTEXT("SetTitle", "Set {0} Property") : LOCTEXT("GetTitle", "Get {0} Property"),
		PCGExK2PropertyBase::GetScopeNoun(GetScope()));
}

FText UK2Node_PCGExPropertyBase::GetTooltipText() const
{
	const bool bSet = IsSetNode();
	switch (GetScope())
	{
	case EScope::Entry:
		return bSet
			? LOCTEXT("SetEntryTooltip",
			          "Writes a value to an entry's property override slot on a PCGEx Asset Collection "
			          "(by raw entry index), enables the override, and reads back the resolved value.\n"
			          "The property must be part of the collection's schema.\n"
			          "New Value and Readback share the same type at compile time; connecting either to "
			          "a typed pin retypes both.\n"
			          "Returns true when the write (and conversion) succeeded.")
			: LOCTEXT("GetEntryTooltip",
			          "Reads an entry's resolved property from a PCGEx Asset Collection by raw entry index\n"
			          "(enabled per-entry override first, then the entry's category override, then the collection default).\n"
			          "The output pin's type drives how the stored value is converted.\n"
			          "Returns true when the conversion succeeded.");
	case EScope::Category:
		return bSet
			? LOCTEXT("SetCategoryTooltip",
			          "Writes a value to a category's property override slot on a PCGEx Asset Collection, "
			          "minting the category's override row when it doesn't exist yet, enables the override, "
			          "and reads back the resolved value.\n"
			          "The property must be part of the collection's schema; category None has no row.\n"
			          "New Value and Readback share the same type at compile time; connecting either to "
			          "a typed pin retypes both.\n"
			          "Returns true when the write (and conversion) succeeded.")
			: LOCTEXT("GetCategoryTooltip",
			          "Reads a category's resolved property from a PCGEx Asset Collection\n"
			          "(enabled category override first, collection default otherwise).\n"
			          "The output pin's type drives how the stored value is converted.\n"
			          "Returns true when the conversion succeeded.");
	case EScope::Collection:
		return bSet
			? LOCTEXT("SetCollectionTooltip",
			          "Writes a property's collection-level default on a PCGEx Asset Collection and reads back "
			          "the resolved value.\n"
			          "Locally declared properties are written in place; properties coming from an imported "
			          "schema asset are written to the collection's import override (the asset is never touched).\n"
			          "New Value and Readback share the same type at compile time; connecting either to "
			          "a typed pin retypes both.\n"
			          "Returns true when the write (and conversion) succeeded.")
			: LOCTEXT("GetCollectionTooltip",
			          "Reads a property's collection-level default from a PCGEx Asset Collection\n"
			          "(local schema entry, enabled import override, or imported schema asset default).\n"
			          "The output pin's type drives how the stored value is converted.\n"
			          "Returns true when the conversion succeeded.");
	default:
		checkNoEntry();
		return FText::GetEmpty();
	}
}

FText UK2Node_PCGExPropertyBase::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "PCGEx|Collection");
}

FName UK2Node_PCGExPropertyBase::GetTargetPinName() const
{
	return PCGExK2PropertyBase::CollectionPinName;
}

UClass* UK2Node_PCGExPropertyBase::GetTargetPinClass() const
{
	return UPCGExAssetCollection::StaticClass();
}

FName UK2Node_PCGExPropertyBase::GetKeyPinName() const
{
	switch (GetScope())
	{
	case EScope::Entry: return PCGExK2PropertyBase::EntryIndexPinName;
	case EScope::Category: return PCGExK2PropertyBase::CategoryPinName;
	default: return NAME_None;
	}
}

void UK2Node_PCGExPropertyBase::CreateKeyPin()
{
	switch (GetScope())
	{
	case EScope::Entry:
		{
			UEdGraphPin* EntryIndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PCGExK2PropertyBase::EntryIndexPinName);
			EntryIndexPin->PinFriendlyName = LOCTEXT("EntryIndexPinFriendlyName", "Entry Index");
			EntryIndexPin->DefaultValue = TEXT("0");
			break;
		}
	case EScope::Category:
		{
			UEdGraphPin* CategoryPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PCGExK2PropertyBase::CategoryPinName);
			CategoryPin->PinFriendlyName = LOCTEXT("CategoryPinFriendlyName", "Category");
			break;
		}
	case EScope::Collection:
		break;
	default:
		checkNoEntry();
		break;
	}
}

UClass* UK2Node_PCGExPropertyBase::GetLibraryClass() const
{
	return UPCGExCollectionEntryBlueprintLibrary::StaticClass();
}

FName UK2Node_PCGExPropertyBase::GetLibraryFunctionName(const bool bSet, const EFlavor Flavor) const
{
#define PCGEX_LIB_FN(_NAME) GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, _NAME)
	switch (GetScope())
	{
	case EScope::Entry:
		switch (Flavor)
		{
		case EFlavor::Object: return bSet ? PCGEX_LIB_FN(TrySetEntryPropertyObject) : PCGEX_LIB_FN(TryGetEntryPropertyObject);
		case EFlavor::Class: return bSet ? PCGEX_LIB_FN(TrySetEntryPropertyClass) : PCGEX_LIB_FN(TryGetEntryPropertyClass);
		default: return bSet ? PCGEX_LIB_FN(TrySetEntryPropertyOverride) : PCGEX_LIB_FN(TryGetEntryPropertyValue);
		}
	case EScope::Category:
		switch (Flavor)
		{
		case EFlavor::Object: return bSet ? PCGEX_LIB_FN(TrySetCategoryPropertyObject) : PCGEX_LIB_FN(TryGetCategoryPropertyObject);
		case EFlavor::Class: return bSet ? PCGEX_LIB_FN(TrySetCategoryPropertyClass) : PCGEX_LIB_FN(TryGetCategoryPropertyClass);
		default: return bSet ? PCGEX_LIB_FN(TrySetCategoryPropertyOverride) : PCGEX_LIB_FN(TryGetCategoryPropertyValue);
		}
	case EScope::Collection:
		switch (Flavor)
		{
		case EFlavor::Object: return bSet ? PCGEX_LIB_FN(TrySetCollectionPropertyObject) : PCGEX_LIB_FN(TryGetCollectionPropertyObject);
		case EFlavor::Class: return bSet ? PCGEX_LIB_FN(TrySetCollectionPropertyClass) : PCGEX_LIB_FN(TryGetCollectionPropertyClass);
		default: return bSet ? PCGEX_LIB_FN(TrySetCollectionPropertyDefault) : PCGEX_LIB_FN(TryGetCollectionPropertyValue);
		}
	default:
		checkNoEntry();
		return NAME_None;
	}
#undef PCGEX_LIB_FN
}

#undef LOCTEXT_NAMESPACE
