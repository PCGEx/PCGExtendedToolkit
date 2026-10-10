// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorHooks.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "PCGExProperty.h"
#include "PCGExPropertyTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Mediator/PCGExPropertyTypeCatalog.h"
#include "UObject/Class.h"

namespace PCGExPropertyMediatorHooks
{
	using namespace PCGExMediator;

	struct FState
	{
		TMap<const UScriptStruct*, FPCGExPropertyMediatorHooks> Hooks;
		FPCGExPropertyMediatorHooks OutputTypeHooks;
		bool bDefaultBuilt = false;
	};

	FState& State()
	{
		check(IsInGameThread());
		static FState Instance;
		return Instance;
	}

	TSharedPtr<FJsonValue> EncodeAtOutputType(const FPCGExProperty& Property)
	{
		const EPCGMetadataTypes Type = Property.GetOutputType();
		if (!Values::IsSupported(Type)) { return nullptr; }

		TSharedPtr<FJsonValue> Out;
#define PCGEX_MEDIATOR_ENCODE(_TYPE, _NAME) { _TYPE Tmp{}; if (Property.TryWriteValue(Type, &Tmp)) { Out = Values::Encode<_TYPE>(Tmp); } }
		PCGEX_EXECUTEWITHRIGHTTYPE(Type, PCGEX_MEDIATOR_ENCODE)
#undef PCGEX_MEDIATOR_ENCODE
		return Out;
	}

	bool DecodeAtOutputType(FPCGExProperty& Property, const TSharedPtr<FJsonValue>& Json)
	{
		const EPCGMetadataTypes Type = Property.GetOutputType();
		if (!Values::IsSupported(Type))
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("type %s has no JSON value representation"), *Property.GetTypeName().ToString()));
			return false;
		}

		bool bDecoded = false;
		bool bAccepted = false;
#define PCGEX_MEDIATOR_DECODE(_TYPE, _NAME) { _TYPE Tmp{}; bDecoded = Values::Decode<_TYPE>(Json, Tmp); if (bDecoded) { bAccepted = Property.TryReadValue(Type, &Tmp); } }
		PCGEX_EXECUTEWITHRIGHTTYPE(Type, PCGEX_MEDIATOR_DECODE)
#undef PCGEX_MEDIATOR_DECODE

		if (bDecoded && !bAccepted)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("type %s refused the value"), *Property.GetTypeName().ToString()));
		}
		return bDecoded && bAccepted;
	}

	TSharedPtr<FJsonObject> DescribeAtOutputType(const FPCGExProperty* Prototype)
	{
		return Prototype ? Values::DescribeShape(Prototype->GetOutputType()) : nullptr;
	}

	// A structural key is optional (OutValue null when absent); present, a wrong shape is an error at the key.
	bool ReadStructural(const FJsonObject& Entry, const TCHAR* Key, const EJson Shape, TSharedPtr<FJsonValue>& OutValue)
	{
		OutValue = Entry.TryGetField(Key);
		if (!OutValue.IsValid()) { return true; }
		if (OutValue->Type == Shape) { return true; }
		Report(EPCGExMediatorSeverity::Error, Key, Shape == EJson::String ? TEXT("expected a string") : TEXT("expected an object"));
		OutValue = nullptr;
		return false;
	}

	// --- Enum ---

	FPCGExPropertyMediatorHooks MakeEnumHooks()
	{
		FPCGExPropertyMediatorHooks H;
		H.EncodeValue = [](const FPCGExProperty& P)
		{
			const FPCGExProperty_Enum& E = static_cast<const FPCGExProperty_Enum&>(P);
			return Values::EncodeEnum(E.Value.Class, E.Value.Value);
		};
		H.DecodeValue = [](FPCGExProperty& P, const TSharedPtr<FJsonValue>& Json)
		{
			FPCGExProperty_Enum& E = static_cast<FPCGExProperty_Enum&>(P);
			int64 V = 0;
			if (!Values::DecodeEnum(E.Value.Class, Json, V)) { return false; }
			E.Value.Value = V;
			return true;
		};
		H.EncodeStructural = [](const FPCGExProperty& P, FJsonObject& Entry)
		{
			const FPCGExProperty_Enum& E = static_cast<const FPCGExProperty_Enum&>(P);
			if (E.Value.Class) { Entry.SetStringField(TEXT("enum"), E.Value.Class->GetPathName()); }
		};
		H.DecodeStructural = [](FPCGExProperty& P, const FJsonObject& Entry)
		{
			TSharedPtr<FJsonValue> Path;
			if (!ReadStructural(Entry, TEXT("enum"), EJson::String, Path)) { return false; }
			if (!Path.IsValid()) { return true; }
			FPathScope Scope(TEXT("enum"));
			UEnum* Enum = PCGExPropertyMediator::ResolveEnum(Path->AsString());
			if (!Enum) { return false; }
			static_cast<FPCGExProperty_Enum&>(P).Value.Class = Enum;
			return true;
		};
		H.DescribeValue = [](const FPCGExProperty* Prototype)
		{
			return Values::DescribeEnumShape(Prototype ? static_cast<const FPCGExProperty_Enum*>(Prototype)->Value.Class.Get() : nullptr);
		};
		H.DescribeStructural = [](FJsonObject& Props)
		{
			Props.SetObjectField(TEXT("enum"), Schema::String(TEXT("enum class path, e.g. /Script/Engine.ECollisionChannel or /Game/Enums/E_Kind.E_Kind")));
		};
		return H;
	}

	// --- Soft paths: value at output type, "allowedClass" structural ---

	template <typename T>
	FPCGExPropertyMediatorHooks MakeSoftPathHooks()
	{
		FPCGExPropertyMediatorHooks H = PCGExPropertyMediator::MakeOutputTypeHooks();
		H.EncodeStructural = [](const FPCGExProperty& P, FJsonObject& Entry)
		{
			const T& Typed = static_cast<const T&>(P);
			if (Typed.AllowedClass) { Entry.SetStringField(TEXT("allowedClass"), Typed.AllowedClass->GetPathName()); }
		};
		H.DecodeStructural = [](FPCGExProperty& P, const FJsonObject& Entry)
		{
			TSharedPtr<FJsonValue> Path;
			if (!ReadStructural(Entry, TEXT("allowedClass"), EJson::String, Path)) { return false; }
			if (!Path.IsValid()) { return true; }
			FPathScope Scope(TEXT("allowedClass"));
			UClass* Class = PCGExPropertyMediator::ResolveClass(Path->AsString());
			if (!Class && !Path->AsString().IsEmpty()) { return false; }
			static_cast<T&>(P).AllowedClass = Class;
			return true;
		};
		H.DescribeStructural = [](FJsonObject& Props)
		{
			Props.SetObjectField(TEXT("allowedClass"), Schema::String(TEXT("class path narrowing the editor picker; \"\" for any")));
		};
		return H;
	}

	// --- Numerics: value at output type, "range" structural ---

	template <typename T>
	FPCGExPropertyMediatorHooks MakeNumericHooks()
	{
		FPCGExPropertyMediatorHooks H = PCGExPropertyMediator::MakeOutputTypeHooks();
		H.EncodeStructural = [](const FPCGExProperty& P, FJsonObject& Entry)
		{
			if (const TSharedPtr<FJsonObject> Range = PCGExPropertyMediator::EncodeRange(static_cast<const T&>(P).Range))
			{
				Entry.SetObjectField(TEXT("range"), Range);
			}
		};
		H.DecodeStructural = [](FPCGExProperty& P, const FJsonObject& Entry)
		{
			TSharedPtr<FJsonValue> Range;
			if (!ReadStructural(Entry, TEXT("range"), EJson::Object, Range)) { return false; }
			if (!Range.IsValid()) { return true; }
			FPathScope Scope(TEXT("range"));
			return PCGExPropertyMediator::DecodeRange(*Range->AsObject(), static_cast<T&>(P).Range);
		};
		H.DescribeStructural = [](FJsonObject& Props)
		{
			Props.SetObjectField(TEXT("range"), PCGExPropertyMediator::DescribeRange().ToSharedRef());
		};
		return H;
	}
}

TArray<const TArray<FInstancedStruct>*>& FPCGExOverridesSchemaStack::Get()
{
	static thread_local TArray<const TArray<FInstancedStruct>*> Stack;
	return Stack;
}

void PCGExPropertyMediator::RegisterHooks(const UScriptStruct* Struct, const FPCGExPropertyMediatorHooks& Hooks)
{
	if (!Struct) { return; }
	PCGExPropertyMediatorHooks::State().Hooks.Add(Struct, Hooks);
}

void PCGExPropertyMediator::UnregisterHooks(const UScriptStruct* Struct)
{
	PCGExPropertyMediatorHooks::State().Hooks.Remove(Struct);
}

const FPCGExPropertyMediatorHooks* PCGExPropertyMediator::FindHooks(const UScriptStruct* Struct)
{
	PCGExPropertyMediatorHooks::FState& S = PCGExPropertyMediatorHooks::State();
	if (const FPCGExPropertyMediatorHooks* Found = S.Hooks.Find(Struct)) { return Found; }

	const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::FindByStruct(Struct);
	if (!Info || !PCGExMediator::Values::IsSupported(Info->Entry.OutputType)) { return nullptr; }

	if (!S.bDefaultBuilt)
	{
		S.OutputTypeHooks = MakeOutputTypeHooks();
		S.bDefaultBuilt = true;
	}
	return &S.OutputTypeHooks;
}

bool PCGExPropertyMediator::HasValueSupport(const UScriptStruct* Struct)
{
	const FPCGExPropertyMediatorHooks* Hooks = FindHooks(Struct);
	return Hooks && Hooks->HasValue();
}

void PCGExPropertyMediator::GetTypesWithoutValueSupport(TArray<FName>& OutTypeNames)
{
	for (const FPCGExPropertyTypeInfo& Info : PCGExPropertyCatalog::Get())
	{
		if (!HasValueSupport(Info.Struct))
		{
			OutTypeNames.Add(Info.Entry.TypeName.IsNone() ? Info.Struct->GetFName() : Info.Entry.TypeName);
		}
	}
}

FPCGExPropertyMediatorHooks PCGExPropertyMediator::MakeOutputTypeHooks()
{
	FPCGExPropertyMediatorHooks H;
	H.EncodeValue = &PCGExPropertyMediatorHooks::EncodeAtOutputType;
	H.DecodeValue = &PCGExPropertyMediatorHooks::DecodeAtOutputType;
	H.DescribeValue = &PCGExPropertyMediatorHooks::DescribeAtOutputType;
	return H;
}

#pragma region Range

TSharedPtr<FJsonObject> PCGExPropertyMediator::EncodeRange(const FPCGExNumericRange& Range)
{
	if (!Range.bClampMin && !Range.bClampMax) { return nullptr; }
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("min"), Range.Min);
	Obj->SetNumberField(TEXT("max"), Range.Max);
	Obj->SetBoolField(TEXT("clampMin"), Range.bClampMin);
	Obj->SetBoolField(TEXT("clampMax"), Range.bClampMax);
	return Obj;
}

bool PCGExPropertyMediator::DecodeRange(const FJsonObject& Json, FPCGExNumericRange& OutRange)
{
	using namespace PCGExMediator;

	FPCGExNumericRange Temp = OutRange;
	bool bOk = true;
	auto Number = [&](const TCHAR* Key, double& Out)
	{
		if (const TSharedPtr<FJsonValue> V = Json.TryGetField(Key))
		{
			if (V->Type != EJson::Number)
			{
				Report(EPCGExMediatorSeverity::Error, Key, TEXT("expected a number"));
				bOk = false;
				return;
			}
			Out = V->AsNumber();
		}
	};
	auto Flag = [&](const TCHAR* Key, bool& Out)
	{
		if (const TSharedPtr<FJsonValue> V = Json.TryGetField(Key))
		{
			if (V->Type != EJson::Boolean)
			{
				Report(EPCGExMediatorSeverity::Error, Key, TEXT("expected a boolean"));
				bOk = false;
				return;
			}
			Out = V->AsBool();
		}
	};
	Number(TEXT("min"), Temp.Min);
	Number(TEXT("max"), Temp.Max);
	Flag(TEXT("clampMin"), Temp.bClampMin);
	Flag(TEXT("clampMax"), Temp.bClampMax);
	if (!bOk) { return false; }
	OutRange = Temp;
	return true;
}

TSharedPtr<FJsonObject> PCGExPropertyMediator::DescribeRange()
{
	using namespace PCGExMediator;

	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(TEXT("min"), Schema::Number(TEXT("lower picker bound")));
	Props->SetObjectField(TEXT("max"), Schema::Number(TEXT("upper picker bound")));
	Props->SetObjectField(TEXT("clampMin"), Schema::Boolean(TEXT("enforce min in the editor picker")));
	Props->SetObjectField(TEXT("clampMax"), Schema::Boolean(TEXT("enforce max in the editor picker")));
	return Schema::Object(Props, TEXT("editor picker hints only; values written programmatically are not clamped"));
}

#pragma endregion

UEnum* PCGExPropertyMediator::ResolveEnum(const FString& Path)
{
	using namespace PCGExMediator;

	if (Path.IsEmpty())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("empty enum path"));
		return nullptr;
	}
	if (UEnum* Enum = LoadType<UEnum>(Path)) { return Enum; }
	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("enum '%s' not found"), *Path));
	return nullptr;
}

UClass* PCGExPropertyMediator::ResolveClass(const FString& Path)
{
	using namespace PCGExMediator;

	if (Path.IsEmpty()) { return nullptr; }
	if (UClass* Class = LoadType<UClass>(Path)) { return Class; }
	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("class '%s' not found"), *Path));
	return nullptr;
}

void PCGExPropertyMediator::RegisterBuiltInHooks()
{
	using namespace PCGExPropertyMediatorHooks;

	RegisterHooks(FPCGExProperty_Enum::StaticStruct(), MakeEnumHooks());
	RegisterHooks(FPCGExProperty_SoftObjectPath::StaticStruct(), MakeSoftPathHooks<FPCGExProperty_SoftObjectPath>());
	RegisterHooks(FPCGExProperty_SoftClassPath::StaticStruct(), MakeSoftPathHooks<FPCGExProperty_SoftClassPath>());
	RegisterHooks(FPCGExProperty_Int32::StaticStruct(), MakeNumericHooks<FPCGExProperty_Int32>());
	RegisterHooks(FPCGExProperty_Int64::StaticStruct(), MakeNumericHooks<FPCGExProperty_Int64>());
	RegisterHooks(FPCGExProperty_Float::StaticStruct(), MakeNumericHooks<FPCGExProperty_Float>());
	RegisterHooks(FPCGExProperty_Double::StaticStruct(), MakeNumericHooks<FPCGExProperty_Double>());
}

void PCGExPropertyMediator::UnregisterBuiltInHooks()
{
	UnregisterHooks(FPCGExProperty_Enum::StaticStruct());
	UnregisterHooks(FPCGExProperty_SoftObjectPath::StaticStruct());
	UnregisterHooks(FPCGExProperty_SoftClassPath::StaticStruct());
	UnregisterHooks(FPCGExProperty_Int32::StaticStruct());
	UnregisterHooks(FPCGExProperty_Int64::StaticStruct());
	UnregisterHooks(FPCGExProperty_Float::StaticStruct());
	UnregisterHooks(FPCGExProperty_Double::StaticStruct());
}
