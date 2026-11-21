// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "K2Node_GetPCGExCollectionMember.h"

#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_GetPCGExCollectionMember"

FName UK2Node_GetPCGExCollectionMember::GetWildcardFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetCollectionMemberValue);
}

FName UK2Node_GetPCGExCollectionMember::GetSoftObjectFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetCollectionMemberSoftObject);
}

FName UK2Node_GetPCGExCollectionMember::GetSoftClassFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, TryGetCollectionMemberSoftClass);
}

FText UK2Node_GetPCGExCollectionMember::GetBaseTitle() const
{
	return LOCTEXT("BaseTitle", "Get Collection Member");
}

#undef LOCTEXT_NAMESPACE
