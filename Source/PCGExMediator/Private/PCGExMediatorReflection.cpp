// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorReflection.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorValues.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "JsonObjectConverter.h"
#include "JsonObjectStructInterface.h"
#include "Misc/PackageName.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace PCGExMediatorReflection
{
	using namespace PCGExMediator;

	const FObjectProperty* AsPlainObjectRef(const FProperty* Property)
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		if (!ObjectProperty || Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance)) { return nullptr; }
		return ObjectProperty;
	}

	const UEnum* EnumOf(const FProperty* Property, const FNumericProperty*& OutUnderlying)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutUnderlying = EnumProperty->GetUnderlyingProperty();
			return EnumProperty->GetEnum();
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			if (const UEnum* Enum = Numeric->GetIntPropertyEnum())
			{
				OutUnderlying = Numeric;
				return Enum;
			}
		}
		OutUnderlying = nullptr;
		return nullptr;
	}

	// Engine shapes for what the dialect does not cover (maps, instanced structs and objects).
	TSharedPtr<FJsonValue> EngineEncode(const FProperty* Property, const void* ValuePtr)
	{
		return FJsonObjectConverter::UPropertyToJsonValue(const_cast<FProperty*>(Property), ValuePtr);
	}

	bool EngineDecode(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
	{
		FText FailReason;
		if (!FJsonObjectConverter::JsonValueToUProperty(Json, const_cast<FProperty*>(Property), ValuePtr, 0, 0, false, &FailReason))
		{
			Report(EPCGExMediatorSeverity::Error, FailReason.IsEmpty() ? TEXT("value rejected") : FailReason.ToString());
			return false;
		}
		return true;
	}

	bool ExpectString(const TSharedPtr<FJsonValue>& Json, const TCHAR* What, FString& Out)
	{
		if (!Json.IsValid() || Json->Type != EJson::String)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("expected %s"), What));
			return false;
		}
		Out = Json->AsString();
		return true;
	}

	UObject* ResolveObjectPath(const FString& Text)
	{
		const FString Path = FPackageName::ExportTextPathToObjectPath(Text);
		const FSoftObjectPath SoftPath(Path);
		if (SoftPath.IsNull()) { return nullptr; }
		if (UObject* Found = SoftPath.ResolveObject()) { return Found; }
		PCGExHelpers::LoadBlocking_AnyThread(SoftPath);
		return SoftPath.ResolveObject();
	}

	TSharedRef<FJsonObject> Schema(const TCHAR* Type, const FString& Description = FString())
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		if (Type) { S->SetStringField(TEXT("type"), Type); }
		if (!Description.IsEmpty()) { S->SetStringField(TEXT("description"), Description); }
		return S;
	}

	TSharedRef<FJsonObject> ArrayOf(const TSharedPtr<FJsonObject>& Items, const FString& Description = FString())
	{
		TSharedRef<FJsonObject> S = Schema(TEXT("array"), Description);
		if (Items.IsValid()) { S->SetObjectField(TEXT("items"), Items.ToSharedRef()); }
		return S;
	}
}

bool PCGExMediator::Reflect::IncludeAll(const FProperty*)
{
	return true;
}

EPCGMetadataTypes PCGExMediator::Reflect::DialectTypeOf(const UScriptStruct* Struct)
{
	if (!Struct) { return EPCGMetadataTypes::Unknown; }
	if (Struct == TBaseStructure<FVector2D>::Get()) { return EPCGMetadataTypes::Vector2; }
	if (Struct == TBaseStructure<FVector>::Get()) { return EPCGMetadataTypes::Vector; }
	if (Struct == TBaseStructure<FVector4>::Get()) { return EPCGMetadataTypes::Vector4; }
	if (Struct == TBaseStructure<FRotator>::Get()) { return EPCGMetadataTypes::Rotator; }
	if (Struct == TBaseStructure<FQuat>::Get()) { return EPCGMetadataTypes::Quaternion; }
	if (Struct == TBaseStructure<FTransform>::Get()) { return EPCGMetadataTypes::Transform; }
	if (Struct == TBaseStructure<FSoftObjectPath>::Get()) { return EPCGMetadataTypes::SoftObjectPath; }
	if (Struct == TBaseStructure<FSoftClassPath>::Get()) { return EPCGMetadataTypes::SoftClassPath; }
	return EPCGMetadataTypes::Unknown;
}

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeProperty(const FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr)
{
	using namespace PCGExMediatorReflection;

	if (Property->ArrayDim != 1) { return EngineEncode(Property, ValuePtr); }

	if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
	{
		const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : FString());
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		return MakeShared<FJsonValueString>(SoftProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath().ToString());
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying))
	{
		return Values::EncodeEnum(Enum, Underlying->GetSignedIntPropertyValue(ValuePtr));
	}
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
	}
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsFloatingPoint()) { return MakeShared<FJsonValueNumber>(Numeric->GetFloatingPointPropertyValue(ValuePtr)); }
		return Values::Encode<int64>(Numeric->GetSignedIntPropertyValue(ValuePtr));
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return Values::Encode<FName>(NameProperty->GetPropertyValue(ValuePtr));
	}
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown)
		{
			return Values::Encode(Type, ValuePtr);
		}
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct))
		{
			TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
			switch (Format->Converter->ConvertToJson(ValuePtr, Body))
			{
			case EJsonObjectConvertResult::Converted: return MakeShared<FJsonValueObject>(Body);
			case EJsonObjectConvertResult::IgnoreAndContinue: return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
			case EJsonObjectConvertResult::UseDefaultConverter: break;
			default: return nullptr;
			}
		}
		if (Struct == FInstancedStruct::StaticStruct()) { return EngineEncode(Property, ValuePtr); }

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		EncodeStruct(Struct, ValuePtr, DefaultValuePtr, *Out);
		return MakeShared<FJsonValueObject>(Out);
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(Helper.Num());
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			FPathScope P(i);
			TSharedPtr<FJsonValue> Item = EncodeProperty(ArrayProperty->Inner, Helper.GetRawPtr(i));
			Items.Add(Item.IsValid() ? Item : MakeShared<FJsonValueNull>());
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (FScriptSetHelper::FIterator It(Helper); It; ++It)
		{
			TSharedPtr<FJsonValue> Item = EncodeProperty(SetProperty->ElementProp, Helper.GetElementPtr(It));
			if (Item.IsValid()) { Items.Add(Item); }
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (Property->IsA<FMapProperty>() || Property->IsA<FObjectProperty>())
	{
		return EngineEncode(Property, ValuePtr);
	}
	return nullptr;
}

bool PCGExMediator::Reflect::DecodeProperty(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
{
	using namespace PCGExMediatorReflection;

	if (!Json.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("missing value"));
		return false;
	}
	if (Property->ArrayDim != 1) { return EngineDecode(Property, ValuePtr, Json); }

	if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("an object path string"), Text)) { return false; }
		UObject* Object = nullptr;
		if (!Text.IsEmpty())
		{
			Object = ResolveObjectPath(Text);
			if (!Object)
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' not found"), *Text));
				return false;
			}
			if (!Object->IsA(ObjectProperty->PropertyClass))
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a %s"), *Text, *ObjectProperty->PropertyClass->GetName()));
				return false;
			}
		}
		ObjectProperty->SetObjectPropertyValue(ValuePtr, Object);
		return true;
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		FSoftObjectPath Path;
		if (!Values::Decode<FSoftObjectPath>(Json, Path)) { return false; }
		SoftProperty->SetPropertyValue(ValuePtr, FSoftObjectPtr(Path));
		return true;
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying))
	{
		int64 Value = 0;
		if (!Values::DecodeEnum(Enum, Json, Value)) { return false; }
		Underlying->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		bool bValue = false;
		if (!Values::Decode<bool>(Json, bValue)) { return false; }
		BoolProperty->SetPropertyValue(ValuePtr, bValue);
		return true;
	}
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsFloatingPoint())
		{
			double Value = 0;
			if (!Values::Decode<double>(Json, Value)) { return false; }
			Numeric->SetFloatingPointPropertyValue(ValuePtr, Value);
			return true;
		}
		int64 Value = 0;
		if (!Values::Decode<int64>(Json, Value)) { return false; }
		Numeric->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("a string"), Text)) { return false; }
		StringProperty->SetPropertyValue(ValuePtr, Text);
		return true;
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		FName Name;
		if (!Values::Decode<FName>(Json, Name)) { return false; }
		NameProperty->SetPropertyValue(ValuePtr, Name);
		return true;
	}
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("a string"), Text)) { return false; }
		TextProperty->SetPropertyValue(ValuePtr, FText::FromString(Text));
		return true;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown)
		{
			return Values::Decode(Json, Type, ValuePtr);
		}
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct))
		{
			if (Json->Type != EJson::Object)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected an object"));
				return false;
			}
			switch (Format->Converter->ConvertFromJson(ValuePtr, Json->AsObject()))
			{
			case EJsonObjectConvertResult::Converted:
			case EJsonObjectConvertResult::IgnoreAndContinue: return true;
			case EJsonObjectConvertResult::UseDefaultConverter: break;
			default: return false;
			}
		}
		if (Struct == FInstancedStruct::StaticStruct()) { return EngineDecode(Property, ValuePtr, Json); }

		if (Json->Type != EJson::Object)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an object"));
			return false;
		}
		// Merge into a copy so a failing key leaves the live struct whole.
		FInstancedStruct Temp;
		Temp.InitializeAs(Struct, static_cast<const uint8*>(ValuePtr));
		if (!DecodeStruct(Struct, Temp.GetMutableMemory(), *Json->AsObject())) { return false; }
		Struct->CopyScriptStruct(ValuePtr, Temp.GetMemory());
		return true;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (Json->Type != EJson::Array)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		// Resize in place like the engine: surviving rows keep their state and merge, new rows start at default.
		Helper.Resize(Items.Num());
		bool bOk = true;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			if (!DecodeProperty(ArrayProperty->Inner, Helper.GetRawPtr(i), Items[i])) { bOk = false; }
		}
		return bOk;
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		if (Json->Type != EJson::Array)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		Helper.EmptyElements(Items.Num());
		bool bOk = true;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
			if (!DecodeProperty(SetProperty->ElementProp, Helper.GetElementPtr(Index), Items[i])) { bOk = false; }
		}
		Helper.Rehash();
		return bOk;
	}
	if (Property->IsA<FMapProperty>() || Property->IsA<FObjectProperty>())
	{
		return EngineDecode(Property, ValuePtr, Json);
	}

	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s has no JSON representation"), *Property->GetClass()->GetName()));
	return false;
}

void PCGExMediator::Reflect::EncodeStruct(const UStruct* Struct, const void* Memory, const void* Defaults, FJsonObject& Out, FPropertyFilter Filter)
{
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Filter(Property)) { continue; }
		if (Defaults && Property->Identical_InContainer(Memory, Defaults)) { continue; }

		FPathScope P(Property->GetName());
		const TSharedPtr<FJsonValue> Value = EncodeProperty(Property, Property->ContainerPtrToValuePtr<void>(Memory), Defaults ? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr);
		if (!Value.IsValid())
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("no JSON representation; omitted"));
			continue;
		}
		Out.SetField(Property->GetName(), Value);
	}
}

bool PCGExMediator::Reflect::DecodeStruct(const UStruct* Struct, void* Memory, const FJsonObject& In, FPropertyFilter Filter, TConstArrayView<FString> IgnoredKeys)
{
	bool bOk = true;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : In.Values)
	{
		if (IgnoredKeys.Contains(Pair.Key)) { continue; }
		FPathScope P(Pair.Key);
		const FProperty* Property = FindFProperty<FProperty>(Struct, *Pair.Key);
		if (!Property || !Filter(Property))
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("not an authored field; ignored"));
			continue;
		}
		if (!DecodeProperty(Property, Property->ContainerPtrToValuePtr<void>(Memory), Pair.Value)) { bOk = false; }
	}
	return bOk;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeProperty(const FProperty* Property, const int32 Depth)
{
	using namespace PCGExMediatorReflection;

	if (AsPlainObjectRef(Property))
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		return Schema(TEXT("string"), FString::Printf(TEXT("object path of a %s; \"\" for none"), *ObjectProperty->PropertyClass->GetName()));
	}
	if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
	{
		return Schema(TEXT("string"), FString::Printf(TEXT("class path of a %s subclass; \"\" for none"), *GetNameSafe(SoftClass->MetaClass)));
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		return Schema(TEXT("string"), FString::Printf(TEXT("object path of a %s; \"\" for none"), *GetNameSafe(SoftProperty->PropertyClass)));
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying)) { return Values::DescribeEnumShape(Enum); }
	if (Property->IsA<FBoolProperty>()) { return Schema(TEXT("boolean")); }
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		return Schema(Numeric->IsFloatingPoint() ? TEXT("number") : TEXT("integer"));
	}
	if (Property->IsA<FStrProperty>() || Property->IsA<FTextProperty>()) { return Schema(TEXT("string")); }
	if (Property->IsA<FNameProperty>()) { return Values::DescribeShape(EPCGMetadataTypes::Name); }

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown) { return Values::DescribeShape(Type); }
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct); Format.IsValid() && Format->Describe)
		{
			TSharedPtr<FJsonObject> S = Format->Describe();
			if (S.IsValid()) { S->SetStringField(TEXT("$comment"), FString::Printf(TEXT("format %s"), *Format->Id.ToString())); }
			return S;
		}
		if (Struct == FInstancedStruct::StaticStruct())
		{
			return Schema(TEXT("object"), TEXT("instanced struct: \"_structType\" (struct path) plus its fields in the engine's reflected shape"));
		}
		if (Depth <= 0)
		{
			// Collapsed: a descriptor tree is tens of KB; the fragment names where the full shape is.
			TSharedRef<FJsonObject> S = Schema(TEXT("object"), FString::Printf(TEXT("%s: fields by UPROPERTY name, collapsed for size"), *Struct->GetName()));
			S->SetStringField(TEXT("$comment"), FString::Printf(TEXT("DescribeFormat(\"%s\") lists them"), *Struct->GetPathName()));
			return S;
		}
		TSharedPtr<FJsonObject> S = DescribeStruct(Struct, &IncludeAll, Depth - 1);
		if (S.IsValid()) { S->SetStringField(TEXT("description"), Struct->GetName()); }
		return S;
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return ArrayOf(DescribeProperty(ArrayProperty->Inner, Depth), TEXT("rows merge by position: row i of the document into row i of the host, extra rows are created, missing rows removed"));
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return ArrayOf(DescribeProperty(SetProperty->ElementProp, Depth), TEXT("set; the document replaces the whole set"));
	}
	if (Property->IsA<FMapProperty>()) { return Schema(TEXT("object"), TEXT("map in the engine's reflected shape")); }
	if (Property->IsA<FObjectProperty>()) { return Schema(TEXT("object"), TEXT("instanced object in the engine's reflected shape")); }
	return Schema(nullptr, FString::Printf(TEXT("%s: not representable"), *Property->GetClass()->GetName()));
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeStruct(const UStruct* Struct, FPropertyFilter Filter, const int32 Depth)
{
	using namespace PCGExMediatorReflection;

	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Filter(Property)) { continue; }
		if (const TSharedPtr<FJsonObject> S = DescribeProperty(Property, Depth)) { Props->SetObjectField(Property->GetName(), S.ToSharedRef()); }
	}
	TSharedRef<FJsonObject> S = Schema(TEXT("object"));
	S->SetObjectField(TEXT("properties"), Props);
	return S;
}
