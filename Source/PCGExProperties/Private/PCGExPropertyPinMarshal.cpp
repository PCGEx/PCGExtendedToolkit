// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyPinMarshal.h"

#include "PCGExProperty.h"
#include "Helpers/PCGPropertyHelpers.h"

namespace PCGExPropertyPinMarshal
{
	UObject* ResolveObjectFromPath(const FSoftObjectPath& SoftPath, const UClass* ExpectedClass)
	{
		UObject* Resolved = SoftPath.ResolveObject();
		if (!Resolved)
		{
			Resolved = SoftPath.TryLoad();
		}
		if (!Resolved || (ExpectedClass && !Resolved->IsA(ExpectedClass)))
		{
			return nullptr;
		}
		return Resolved;
	}

	UClass* ResolveClassFromPath(const FSoftClassPath& SoftPath, const UClass* ExpectedClass)
	{
		UClass* Resolved = Cast<UClass>(SoftPath.ResolveObject());
		if (!Resolved)
		{
			Resolved = SoftPath.TryLoadClass<UObject>();
		}
		if (!Resolved || (ExpectedClass && !Resolved->IsChildOf(ExpectedClass)))
		{
			return nullptr;
		}
		return Resolved;
	}

	bool TryWriteToPin(const FPCGExProperty* Prop, const FProperty* OutProp, void* OutMem)
	{
		if (!Prop || !OutProp || !OutMem)
		{
			return false;
		}

		const EPCGMetadataTypes TargetType = PCGPropertyHelpers::GetMetadataTypeFromProperty(OutProp);
		if (TargetType == EPCGMetadataTypes::Unknown)
		{
			return TryWriteToObjectPin(Prop, OutProp, OutMem);
		}

		return Prop->TryWriteValue(TargetType, OutMem);
	}

	bool TryReadFromPin(FPCGExProperty* Prop, const FProperty* InProp, const void* InMem)
	{
		if (!Prop || !InProp || !InMem)
		{
			return false;
		}

		const EPCGMetadataTypes SourceType = PCGPropertyHelpers::GetMetadataTypeFromProperty(InProp);
		return (SourceType != EPCGMetadataTypes::Unknown)
			? Prop->TryReadValue(SourceType, InMem)
			: TryReadFromObjectPin(Prop, InProp, InMem);
	}

	bool TryWriteToObjectPin(const FPCGExProperty* Prop, const FProperty* OutProp, void* OutMem)
	{
		if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(OutProp))
		{
			FSoftObjectPath SoftPath;
			if (!Prop->TryWriteValue(EPCGMetadataTypes::SoftObjectPath, &SoftPath))
			{
				return false;
			}
			UObject* Resolved = ResolveObjectFromPath(SoftPath, ObjProp->PropertyClass);
			if (!Resolved)
			{
				return false;
			}
			ObjProp->SetObjectPropertyValue(OutMem, Resolved);
			return true;
		}

		if (const FClassProperty* ClassProp = CastField<FClassProperty>(OutProp))
		{
			FSoftClassPath SoftPath;
			if (!Prop->TryWriteValue(EPCGMetadataTypes::SoftClassPath, &SoftPath))
			{
				return false;
			}
			UClass* Resolved = ResolveClassFromPath(SoftPath, ClassProp->MetaClass);
			if (!Resolved)
			{
				return false;
			}
			ClassProp->SetObjectPropertyValue(OutMem, Resolved);
			return true;
		}

		return false;
	}

	bool TryReadFromObjectPin(FPCGExProperty* Prop, const FProperty* InProp, const void* InMem)
	{
		if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(InProp))
		{
			UObject* InObj = ObjProp->GetObjectPropertyValue(InMem);
			FSoftObjectPath SoftPath(InObj);
			return Prop->TryReadValue(EPCGMetadataTypes::SoftObjectPath, &SoftPath);
		}

		if (const FClassProperty* ClassProp = CastField<FClassProperty>(InProp))
		{
			UClass* InClass = Cast<UClass>(ClassProp->GetObjectPropertyValue(InMem));
			FSoftClassPath SoftPath(InClass);
			return Prop->TryReadValue(EPCGMetadataTypes::SoftClassPath, &SoftPath);
		}

		return false;
	}
}
