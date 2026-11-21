// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/K2Node_PCGExComponentPropertyBase.h"

#include "PCGExPropertyBlueprintLibrary.h"
#include "PCGExPropertyCollectionComponent.h"

#define LOCTEXT_NAMESPACE "K2Node_PCGExComponentPropertyBase"

namespace PCGExK2ComponentPropertyBase
{
	// Matches the library functions' parameter name.
	const FName ComponentPinName(TEXT("Component"));
}

FText UK2Node_PCGExComponentPropertyBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return IsSetNode() ? LOCTEXT("SetTitle", "PCGEx | Set Property") : LOCTEXT("GetTitle", "PCGEx | Get Property");
}

FText UK2Node_PCGExComponentPropertyBase::GetTooltipText() const
{
	return IsSetNode()
		? LOCTEXT("SetTooltip",
		          "Writes a value to a named property on a PCGEx Property Collection Component and "
		          "reads back the value the property now stores.\n"
		          "New Value and Readback share the same type at compile time; connecting either to "
		          "a typed pin retypes both.\n"
		          "Returns true when the write (and conversion) succeeded.")
		: LOCTEXT("GetTooltip",
		          "Reads a named property from a PCGEx Property Collection Component.\n"
		          "The output pin's type drives how the stored value is converted.\n"
		          "Returns true when the conversion succeeded.");
}

FText UK2Node_PCGExComponentPropertyBase::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "PCGEx|Property");
}

FName UK2Node_PCGExComponentPropertyBase::GetTargetPinName() const
{
	return PCGExK2ComponentPropertyBase::ComponentPinName;
}

UClass* UK2Node_PCGExComponentPropertyBase::GetTargetPinClass() const
{
	return UPCGExPropertyCollectionComponent::StaticClass();
}

UClass* UK2Node_PCGExComponentPropertyBase::GetLibraryClass() const
{
	return UPCGExPropertyBlueprintLibrary::StaticClass();
}

FName UK2Node_PCGExComponentPropertyBase::GetLibraryFunctionName(const bool bSet, const EFlavor Flavor) const
{
#define PCGEX_LIB_FN(_NAME) GET_FUNCTION_NAME_CHECKED(UPCGExPropertyBlueprintLibrary, _NAME)
	switch (Flavor)
	{
	case EFlavor::Object: return bSet ? PCGEX_LIB_FN(TrySetPCGExPropertyObject) : PCGEX_LIB_FN(TryGetPCGExPropertyObject);
	case EFlavor::Class: return bSet ? PCGEX_LIB_FN(TrySetPCGExPropertyClass) : PCGEX_LIB_FN(TryGetPCGExPropertyClass);
	default: return bSet ? PCGEX_LIB_FN(TrySetPCGExPropertyValue) : PCGEX_LIB_FN(TryGetPCGExPropertyValue);
	}
#undef PCGEX_LIB_FN
}

#undef LOCTEXT_NAMESPACE
