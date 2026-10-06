// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyBlueprintLibrary.h"

#include "PCGExProperty.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExPropertyPinMarshal.h"

namespace PCGExPropertyBlueprintLibrary_Private
{
	bool ReadInto(
		const UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		const FProperty* OutProp,
		void* OutMem)
	{
		if (!Component || !OutProp || !OutMem)
		{
			return false;
		}

		const FInstancedStruct* Source = Component->GetProperties().GetPropertyByName(PropertyName);
		if (!Source)
		{
			return false;
		}

		const FPCGExProperty* Prop = Source->GetPtr<FPCGExProperty>();
		if (!Prop)
		{
			return false;
		}

		return PCGExPropertyPinMarshal::TryWriteToPin(Prop, OutProp, OutMem);
	}

	// Resolve a writable FPCGExProperty target. Locals first (FindByNameMutable is locals-only --
	// imports would mutate the source asset globally). Then ImportOverrides: an import hit
	// auto-enables the toggle via SetOverrideEnabled so the just-written value participates in
	// resolution.
	FPCGExProperty* ResolveWritableProperty(UPCGExPropertyCollectionComponent* Component, FName PropertyName)
	{
		if (!Component)
		{
			return nullptr;
		}

		FPCGExPropertySchemaCollection& Props = Component->GetPropertiesMutable();

		if (FPCGExPropertySchema* Schema = Props.FindByNameMutable(PropertyName))
		{
			if (FPCGExProperty* Prop = Schema->GetPropertyMutable())
			{
				return Prop;
			}
		}

		if (FPCGExPropertyOverrideEntry* Entry = Props.ImportOverrides.FindEntryMutableByName(PropertyName))
		{
			if (FPCGExProperty* Prop = Entry->Value.GetMutablePtr<FPCGExProperty>())
			{
				Component->SetOverrideEnabled(PropertyName, true);
				return Prop;
			}
		}

		return nullptr;
	}

	bool WriteFrom(
		UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		const FProperty* InProp,
		const void* InMem)
	{
		if (!Component || !InProp || !InMem)
		{
			return false;
		}

		FPCGExProperty* Prop = ResolveWritableProperty(Component, PropertyName);
		if (!Prop)
		{
			return false;
		}

		return PCGExPropertyPinMarshal::TryReadFromPin(Prop, InProp, InMem);
	}

	// Shared lookup helpers for the well-typed Object/Class accessors. These avoid the
	// CustomThunk wildcard path entirely so the BP compiler marshalling stays correct
	// for Object/Class pins (where the wildcard CustomStructureParam mechanism would
	// otherwise mis-size the frame slot and corrupt the property's FSoftObjectPath::Value).
	bool ReadSoftPath(
		const UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		void* OutPath)
	{
		if (!Component)
		{
			return false;
		}
		const FInstancedStruct* Source = Component->GetProperties().GetPropertyByName(PropertyName);
		if (!Source)
		{
			return false;
		}
		const FPCGExProperty* Prop = Source->GetPtr<FPCGExProperty>();
		if (!Prop)
		{
			return false;
		}
		return Prop->TryWriteValue(PathType, OutPath);
	}

	bool WriteSoftPath(
		UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		const void* InPath)
	{
		FPCGExProperty* Prop = ResolveWritableProperty(Component, PropertyName);
		if (!Prop)
		{
			return false;
		}
		return Prop->TryReadValue(PathType, InPath);
	}
}

bool UPCGExPropertyBlueprintLibrary::TryGetPCGExPropertyValue(
	const UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExPropertyBlueprintLibrary::execTryGetPCGExPropertyValue)
{
	P_GET_OBJECT(UPCGExPropertyCollectionComponent, Component);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	// Wildcard OutValue: the K2 node wires its actual pin type here at compile time.
	// Read the FProperty descriptor + destination memory off the stack manually.
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExPropertyBlueprintLibrary_Private::ReadInto(Component, PropertyName, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExPropertyBlueprintLibrary::TrySetPCGExPropertyValue(
	UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExPropertyBlueprintLibrary::execTrySetPCGExPropertyValue)
{
	P_GET_OBJECT(UPCGExPropertyCollectionComponent, Component);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExPropertyBlueprintLibrary_Private::WriteFrom(
			Component, PropertyName, InProp, InMem);
	P_NATIVE_END;
}

UObject* UPCGExPropertyBlueprintLibrary::TryGetPCGExPropertyObject(
	const UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftObjectPath SoftPath;
	if (!PCGExPropertyBlueprintLibrary_Private::ReadSoftPath(
		Component, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath))
	{
		return nullptr;
	}

	UObject* Resolved = PCGExPropertyPinMarshal::ResolveObjectFromPath(SoftPath, ExpectedClass);
	if (!Resolved)
	{
		return nullptr;
	}

	bSuccess = true;
	return Resolved;
}

bool UPCGExPropertyBlueprintLibrary::TrySetPCGExPropertyObject(
	UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	UObject* NewObject)
{
	const FSoftObjectPath SoftPath(NewObject);
	return PCGExPropertyBlueprintLibrary_Private::WriteSoftPath(
		Component, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath);
}

TSubclassOf<UObject> UPCGExPropertyBlueprintLibrary::TryGetPCGExPropertyClass(
	const UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftClassPath SoftPath;
	if (!PCGExPropertyBlueprintLibrary_Private::ReadSoftPath(
		Component, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath))
	{
		return nullptr;
	}

	UClass* Resolved = PCGExPropertyPinMarshal::ResolveClassFromPath(SoftPath, ExpectedClass);
	if (!Resolved)
	{
		return nullptr;
	}

	bSuccess = true;
	return Resolved;
}

bool UPCGExPropertyBlueprintLibrary::TrySetPCGExPropertyClass(
	UPCGExPropertyCollectionComponent* Component,
	FName PropertyName,
	UClass* NewClass)
{
	const FSoftClassPath SoftPath(NewClass);
	return PCGExPropertyBlueprintLibrary_Private::WriteSoftPath(
		Component, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath);
}
