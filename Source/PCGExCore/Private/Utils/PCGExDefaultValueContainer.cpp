// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/PCGExDefaultValueContainer.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "Helpers/PCGGraphParameterExtension.h"
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

const FProperty* FPCGExDefaultValueContainer::CreateNewProperty(const FName PropertyName, const EPCGMetadataTypes Type)
{
	if (PropertyName == NAME_None || !PCGMetadataHelpers::MetadataTypeSupportsDefaultValues(Type))
	{
		return nullptr;
	}

	if (PropertyBag.FindPropertyDescByName(PropertyName))
	{
		PropertyBag.RemovePropertyByName(PropertyName);
	}

	const FPropertyBagPropertyDesc PropertyDesc = PCGPropertyHelpers::CreatePropertyBagDescWithMetadataType(PropertyName, Type);
	PropertyBag.AddProperties({PropertyDesc});
	if (const FPropertyBagPropertyDesc* OutPropertyDesc = PropertyBag.FindPropertyDescByName(PropertyName))
	{
		return OutPropertyDesc->CachedProperty;
	}
	return nullptr;
}

const FProperty* FPCGExDefaultValueContainer::FindProperty(const FName PropertyName) const
{
	const FPropertyBagPropertyDesc* PropertyDesc = PropertyBag.FindPropertyDescByName(PropertyName);
	return PropertyDesc ? PropertyDesc->CachedProperty : nullptr;
}

void FPCGExDefaultValueContainer::RemoveProperty(const FName PropertyName)
{
	PropertyBag.RemovePropertyByName(PropertyName);
}

EPCGMetadataTypes FPCGExDefaultValueContainer::GetCurrentPropertyType(const FName PropertyName) const
{
	const FProperty* Property = FindProperty(PropertyName);
	return Property ? PCGPropertyHelpers::GetMetadataTypeFromProperty(Property) : EPCGMetadataTypes::Unknown;
}

FString FPCGExDefaultValueContainer::GetPropertyValueAsString(const FName PropertyName) const
{
	TValueOrError<FString, EPropertyBagResult> Result = PropertyBag.GetValueSerializedString(PropertyName);

	return Result.HasValue() ? Result.GetValue() : FString(TEXT("Error"));
}

const UPCGParamData* FPCGExDefaultValueContainer::CreateParamData(FPCGContext* Context, const FName PropertyName) const
{
	if (const FProperty* PropertyPtr = FindProperty(PropertyName))
	{
		const TObjectPtr<UPCGParamData> NewParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		if (NewParamData->Metadata->CreateAttributeFromDataProperty(NAME_None, PropertyBag.GetValue().GetMemory(), PropertyPtr))
		{
			return NewParamData;
		}
	}

	return nullptr;
}

bool FPCGExDefaultValueContainer::IsPropertyActivated(const FName PropertyName) const
{
	return ActivatedProperties.Contains(PropertyName);
}

#if WITH_EDITOR
const FProperty* FPCGExDefaultValueContainer::ConvertPropertyType(const FName PropertyName, const EPCGMetadataTypes Type)
{
	if (!PCGMetadataHelpers::MetadataTypeSupportsDefaultValues(Type) || Type == GetCurrentPropertyType(PropertyName))
	{
		return nullptr;
	}

	return CreateNewProperty(PropertyName, Type);
}

bool FPCGExDefaultValueContainer::SetPropertyValueFromString(const FName PropertyName, const FString& ValueString)
{
	if (PropertyName == NAME_None)
	{
		return false;
	}

	SetPropertyActivated(PropertyName, /*bIsActive=*/true);
	return PropertyBag.SetValueSerializedString(PropertyName, ValueString) == EPropertyBagResult::Success;
}

bool FPCGExDefaultValueContainer::SetPropertyActivated(const FName PropertyName, const bool bIsActivated)
{
	if ((PropertyName != NAME_None) && (bIsActivated != ActivatedProperties.Contains(PropertyName)))
	{
		if (bIsActivated)
		{
			ActivatedProperties.Add(PropertyName);
			return true;
		}
		ActivatedProperties.Remove(PropertyName);
		return true;
	}
	return false;
}

void FPCGExDefaultValueContainer::Reset()
{
	ActivatedProperties.Reset();
	PropertyBag.Reset();
}
#endif // WITH_EDITOR
