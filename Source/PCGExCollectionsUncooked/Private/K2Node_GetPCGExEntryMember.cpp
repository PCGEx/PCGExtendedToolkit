// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "K2Node_GetPCGExEntryMember.h"

#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_GetPCGExEntryMember"

FName UK2Node_GetPCGExEntryMember::GetWildcardFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetEntryMemberValue);
}

FName UK2Node_GetPCGExEntryMember::GetSoftObjectFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetEntryMemberSoftObject);
}

FName UK2Node_GetPCGExEntryMember::GetSoftClassFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetEntryMemberSoftClass);
}

FText UK2Node_GetPCGExEntryMember::GetBaseTitle() const
{
	return LOCTEXT("BaseTitle", "Get Entry Member");
}

#undef LOCTEXT_NAMESPACE
