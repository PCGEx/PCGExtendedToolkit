// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorFormats.h"

#include "PCGExEnumSelector.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorValues.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
#include "PCGExPropertyTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Mediator/PCGExPropertyTypeCatalog.h"
#include "StructUtils/InstancedStruct.h"

namespace PCGExPropertyMediatorFormats
{
	using namespace PCGExMediator;

	namespace Keys
	{
		const TCHAR* Properties = TEXT("properties");
		const TCHAR* Name = TEXT("name");
		const TCHAR* Type = TEXT("type");
		const TCHAR* Default = TEXT("default");
		const TCHAR* Imports = TEXT("imports");
		const TCHAR* ImportOverrides = TEXT("importOverrides");
		const TCHAR* Value = TEXT("value");
		const TCHAR* Enum = TEXT("enum");
	}

	// Runs Body under a private sink; every report is forwarded, and any error turns into FailAndAbort.
	EJsonObjectConvertResult Guarded(TFunctionRef<bool()> Body)
	{
		FPCGExMediatorDiagnostics Local;
		bool bOk = false;
		{
			FScope Scope(Local);
			bOk = Body();
		}
		Forward(Local);
		return (bOk && !Local.HasErrors()) ? EJsonObjectConvertResult::Converted : EJsonObjectConvertResult::FailAndAbort;
	}

	bool ReadPropertyName(const FJsonObject& Obj, const TCHAR* Key, FName& OutName)
	{
		FString Text;
		if (!Obj.TryGetStringField(Key, Text) || Text.IsEmpty())
		{
			Report(EPCGExMediatorSeverity::Error, Key, TEXT("missing or empty"));
			return false;
		}
		if (Text.Len() >= NAME_SIZE)
		{
			Report(EPCGExMediatorSeverity::Error, Key, TEXT("longer than the FName limit"));
			return false;
		}
		OutName = FName(*Text);
		return true;
	}

	bool IsTypedValue(const TSharedPtr<FJsonValue>& Json)
	{
		return Json.IsValid() && Json->Type == EJson::Object && Json->AsObject()->HasField(Keys::Type) && Json->AsObject()->HasField(Keys::Value);
	}

#pragma region Overrides body

	void EncodeOverridesBody(const FPCGExPropertyOverrides& Overrides, FJsonObject& Out)
	{
		for (const FPCGExPropertyOverrideEntry& Entry : Overrides.Overrides)
		{
			if (!Entry.bEnabled) { continue; }
			const FPCGExProperty* Property = Entry.GetProperty();
			const FName Name = Entry.GetPropertyName();
			if (!Property || Name.IsNone()) { continue; }

			FPathScope P(Name.ToString());
			const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Entry.Value.GetScriptStruct());
			const TSharedPtr<FJsonValue> Value = (Hooks && Hooks->HasValue()) ? Hooks->EncodeValue(*Property) : nullptr;
			if (!Value.IsValid())
			{
				Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("type %s has no JSON value representation; override skipped"), *Property->GetTypeName().ToString()));
				continue;
			}
			Out.SetField(Name.ToString(), Value);
		}
	}

	// Target must be schema-parallel, or an FOverridesSchemaScope must be active; with neither, only the
	// { "type", "value" } form can create entries (the host's next schema sync matches them by name).
	bool DecodeOverridesBody(const FJsonObject& In, FPCGExPropertyOverrides& Target)
	{
		FPCGExPropertyOverrides Temp = Target;
		if (const TArray<FInstancedStruct>* Scope = PCGExPropertyMediator::FOverridesSchemaScope::Current())
		{
			Temp.SyncToSchema(*Scope);
		}
		const bool bHasContext = !Temp.Overrides.IsEmpty();

		bool bOk = true;
		TSet<FName> Named;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : In.Values)
		{
			FPathScope P(Pair.Key);
			if (Pair.Key.IsEmpty() || Pair.Key.Len() >= NAME_SIZE)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("not a valid property name"));
				bOk = false;
				continue;
			}
			const FName Name(*Pair.Key);
			const bool bTyped = IsTypedValue(Pair.Value);
			const TSharedPtr<FJsonValue> ValueJson = bTyped ? Pair.Value->AsObject()->TryGetField(Keys::Value) : Pair.Value;

			FPCGExPropertyOverrideEntry* Entry = Temp.FindEntryMutableByName(Name);
			if (!Entry)
			{
				if (bHasContext)
				{
					Report(EPCGExMediatorSeverity::Error, TEXT("not a property of the schema"));
					bOk = false;
					continue;
				}
				if (!bTyped)
				{
					Report(EPCGExMediatorSeverity::Error, TEXT("no schema context; use { \"type\": <type>, \"value\": <value> } to create the entry"));
					bOk = false;
					continue;
				}
				FString TypeText;
				Pair.Value->AsObject()->TryGetStringField(Keys::Type, TypeText);
				const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::Find(TypeText);
				if (!Info)
				{
					Report(EPCGExMediatorSeverity::Error, Keys::Type, FString::Printf(TEXT("unknown property type '%s'; known: %s"), *TypeText, *PCGExPropertyCatalog::ListTypeNames()));
					bOk = false;
					continue;
				}
				FInstancedStruct NewProperty;
				PCGExPropertyCatalog::MakeProperty(*Info, Name, NewProperty);
				Temp.Overrides.Add(FPCGExPropertyOverrideEntry(NewProperty, true));
				Entry = &Temp.Overrides.Last();
			}
			else if (bTyped)
			{
				FString TypeText;
				Pair.Value->AsObject()->TryGetStringField(Keys::Type, TypeText);
				const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::Find(TypeText);
				if (!Info || Info->Struct != Entry->Value.GetScriptStruct())
				{
					Report(EPCGExMediatorSeverity::Error, Keys::Type, FString::Printf(TEXT("'%s' does not match the schema's type for this property"), *TypeText));
					bOk = false;
					continue;
				}
			}

			FPCGExProperty* Property = Entry->GetPropertyMutable();
			const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Entry->Value.GetScriptStruct());
			if (!Property || !Hooks || !Hooks->HasValue())
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("type %s accepts no JSON value"), Property ? *Property->GetTypeName().ToString() : TEXT("?")));
				bOk = false;
				continue;
			}
			if (!Hooks->DecodeValue(*Property, ValueJson))
			{
				bOk = false;
				continue;
			}
			Entry->bEnabled = true;
			Named.Add(Name);
		}

		if (!bOk) { return false; }

		for (FPCGExPropertyOverrideEntry& Entry : Temp.Overrides)
		{
			if (!Named.Contains(Entry.GetPropertyName())) { Entry.bEnabled = false; }
		}
		Target = MoveTemp(Temp);
		return true;
	}

	TSharedPtr<FJsonObject> DescribeOverridesBody()
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetBoolField(TEXT("additionalProperties"), true);
		S->SetStringField(TEXT("description"),
		                  TEXT("Enabled overrides by property name; every name absent from the document is disabled. A value takes the shape of the "
			                  "property's type (see the property-schema format's $defs/values). Without a schema to resolve names against, an entry "
			                  "can be created as { \"type\": <type name>, \"value\": <value> }."));
		return S;
	}

#pragma endregion

#pragma region Schema body

	void EncodeSchemaBody(const FPCGExPropertySchemaCollection& Collection, FJsonObject& Out)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		Entries.Reserve(Collection.Schemas.Num());

		FPathScope PropertiesPath(Keys::Properties);
		for (int32 i = 0; i < Collection.Schemas.Num(); ++i)
		{
			FPathScope P(i);
			const FPCGExPropertySchema& Schema = Collection.Schemas[i];
			const FPCGExProperty* Property = Schema.GetProperty();
			if (!Property || Schema.Name.IsNone())
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("invalid entry skipped"));
				continue;
			}

			const UScriptStruct* Struct = Schema.Property.GetScriptStruct();
			const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::FindByStruct(Struct);

			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(Keys::Name, Schema.Name.ToString());
			Entry->SetStringField(Keys::Type, (Info && !Info->TypeName.IsNone()) ? Info->TypeName.ToString() : Struct->GetPathName());

			if (const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Struct))
			{
				if (Hooks->EncodeStructural) { Hooks->EncodeStructural(*Property, *Entry); }
				const TSharedPtr<FJsonValue> Value = Hooks->HasValue() ? Hooks->EncodeValue(*Property) : nullptr;
				if (Value.IsValid()) { Entry->SetField(Keys::Default, Value); }
				else { Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("type %s has no JSON value representation; default omitted"), *Property->GetTypeName().ToString())); }
			}
			else
			{
				Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("type %s has no JSON hooks; default omitted"), *Property->GetTypeName().ToString()));
			}
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Out.SetArrayField(Keys::Properties, Entries);

		if (!Collection.ImportedSchemas.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Imports;
			for (const TObjectPtr<UPCGExPropertySchemaAsset>& Asset : Collection.ImportedSchemas)
			{
				if (Asset) { Imports.Add(MakeShared<FJsonValueString>(Asset->GetPathName())); }
			}
			Out.SetArrayField(Keys::Imports, Imports);
		}

		if (Collection.ImportOverrides.GetEnabledCount() > 0)
		{
			FPathScope OverridesPath(Keys::ImportOverrides);
			TSharedRef<FJsonObject> Overrides = MakeShared<FJsonObject>();
			EncodeOverridesBody(Collection.ImportOverrides, *Overrides);
			Out.SetObjectField(Keys::ImportOverrides, Overrides);
		}
	}

	bool DecodeSchemaBody(const FJsonObject& In, const FPCGExPropertySchemaCollection& Live, FPCGExPropertySchemaCollection& OutTemp)
	{
		bool bOk = true;
		FPCGExPropertySchemaCollection Temp;

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!In.TryGetArrayField(Keys::Properties, Entries))
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Properties, TEXT("missing array"));
			return false;
		}

		{
			FPathScope PropertiesPath(Keys::Properties);
			TSet<FName> Seen;
			for (int32 i = 0; i < Entries->Num(); ++i)
			{
				FPathScope P(i);
				const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[i];
				if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
				{
					Report(EPCGExMediatorSeverity::Error, TEXT("expected an object"));
					bOk = false;
					continue;
				}
				const FJsonObject& Entry = *EntryValue->AsObject();

				FName Name;
				if (!ReadPropertyName(Entry, Keys::Name, Name))
				{
					bOk = false;
					continue;
				}
				if (Seen.Contains(Name))
				{
					Report(EPCGExMediatorSeverity::Error, Keys::Name, FString::Printf(TEXT("'%s' declared twice"), *Name.ToString()));
					bOk = false;
					continue;
				}
				Seen.Add(Name);

				FString TypeText;
				Entry.TryGetStringField(Keys::Type, TypeText);
				const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::Find(TypeText);
				if (!Info)
				{
					Report(EPCGExMediatorSeverity::Error, Keys::Type, FString::Printf(TEXT("unknown property type '%s'; known: %s"), *TypeText, *PCGExPropertyCatalog::ListTypeNames()));
					bOk = false;
					continue;
				}

				FPCGExPropertySchema& Row = Temp.Schemas.AddDefaulted_GetRef();
				Row.Name = Name;
				PCGExPropertyCatalog::MakeProperty(*Info, Name, Row.Property);
				FPCGExProperty* Property = Row.GetPropertyMutable();

				// Same name on the live target = same column: keep its identity so existing overrides survive.
				for (const FPCGExPropertySchema& Existing : Live.Schemas)
				{
					if (Existing.Name == Name)
					{
						Row.HeaderId = Existing.HeaderId;
						break;
					}
				}

				const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Info->Struct);
				if (Hooks && Hooks->DecodeStructural && !Hooks->DecodeStructural(*Property, Entry))
				{
					bOk = false;
					continue;
				}
				if (const TSharedPtr<FJsonValue> Default = Entry.TryGetField(Keys::Default))
				{
					FPathScope D(Keys::Default);
					if (!Hooks || !Hooks->HasValue())
					{
						Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("type %s accepts no JSON value; default kept"), *Info->TypeName.ToString()));
					}
					else if (!Hooks->DecodeValue(*Property, Default))
					{
						bOk = false;
						continue;
					}
				}
			}
		}

		if (const TArray<TSharedPtr<FJsonValue>>* Imports = nullptr; In.TryGetArrayField(Keys::Imports, Imports))
		{
			FPathScope ImportsPath(Keys::Imports);
			for (int32 i = 0; i < Imports->Num(); ++i)
			{
				FPathScope P(i);
				const TSharedPtr<FJsonValue>& ImportValue = (*Imports)[i];
				if (!ImportValue.IsValid() || ImportValue->Type != EJson::String)
				{
					Report(EPCGExMediatorSeverity::Error, TEXT("expected a Property Schema asset path"));
					bOk = false;
					continue;
				}
				UPCGExPropertySchemaAsset* Asset = Cast<UPCGExPropertySchemaAsset>(PCGExPropertyMediator::ResolveObject(ImportValue->AsString()));
				if (!Asset)
				{
					Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a Property Schema asset"), *ImportValue->AsString()));
					bOk = false;
					continue;
				}
				Temp.ImportedSchemas.Add(Asset);
			}
		}

		if (!bOk) { return false; }

		Temp.SyncAllSchemas();

		if (const TSharedPtr<FJsonObject>* Overrides = nullptr; In.TryGetObjectField(Keys::ImportOverrides, Overrides))
		{
			FPathScope OverridesPath(Keys::ImportOverrides);
			if (!IsInGameThread())
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("import overrides reconcile on the game thread only; ignored"));
			}
			else
			{
				Temp.ReconcileImportOverrides();
				if (!DecodeOverridesBody(**Overrides, Temp.ImportOverrides)) { return false; }
			}
		}
		else if (IsInGameThread())
		{
			Temp.ReconcileImportOverrides();
		}

		OutTemp = MoveTemp(Temp);
		return true;
	}

	TSharedPtr<FJsonObject> DescribeSchemaBody()
	{
		TSharedRef<FJsonObject> EntryProps = MakeShared<FJsonObject>();
		{
			TSharedRef<FJsonObject> NameSchema = MakeShared<FJsonObject>();
			NameSchema->SetStringField(TEXT("type"), TEXT("string"));
			NameSchema->SetStringField(TEXT("description"), TEXT("unique within the schema; the output attribute name"));
			EntryProps->SetObjectField(Keys::Name, NameSchema);

			TArray<TSharedPtr<FJsonValue>> TypeNames;
			for (const FPCGExPropertyTypeInfo& Info : PCGExPropertyCatalog::Get())
			{
				if (!Info.TypeName.IsNone()) { TypeNames.Add(MakeShared<FJsonValueString>(Info.TypeName.ToString())); }
			}
			TSharedRef<FJsonObject> TypeSchema = MakeShared<FJsonObject>();
			TypeSchema->SetStringField(TEXT("type"), TEXT("string"));
			TypeSchema->SetArrayField(TEXT("enum"), TypeNames);
			TypeSchema->SetStringField(TEXT("description"), TEXT("property type name (a struct path is also accepted)"));
			EntryProps->SetObjectField(Keys::Type, TypeSchema);

			TSharedRef<FJsonObject> DefaultSchema = MakeShared<FJsonObject>();
			DefaultSchema->SetStringField(TEXT("description"), TEXT("the entry's default value, in the shape of its type: see $defs/values/<type>"));
			EntryProps->SetObjectField(Keys::Default, DefaultSchema);
		}

		TSharedRef<FJsonObject> Defs = MakeShared<FJsonObject>();
		for (const FPCGExPropertyTypeInfo& Info : PCGExPropertyCatalog::Get())
		{
			const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Info.Struct);
			if (!Hooks) { continue; }
			if (Hooks->DescribeStructural) { Hooks->DescribeStructural(*EntryProps); }
			if (Hooks->DescribeValue && !Info.TypeName.IsNone())
			{
				FInstancedStruct Prototype;
				PCGExPropertyCatalog::MakeProperty(Info, NAME_None, Prototype);
				if (const TSharedPtr<FJsonObject> Shape = Hooks->DescribeValue(Prototype.GetPtr<FPCGExProperty>()))
				{
					Defs->SetObjectField(Info.TypeName.ToString(), Shape.ToSharedRef());
				}
			}
		}

		TSharedRef<FJsonObject> EntrySchema = MakeShared<FJsonObject>();
		EntrySchema->SetStringField(TEXT("type"), TEXT("object"));
		EntrySchema->SetObjectField(TEXT("properties"), EntryProps);
		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(Keys::Name));
		Required.Add(MakeShared<FJsonValueString>(Keys::Type));
		EntrySchema->SetArrayField(TEXT("required"), Required);

		TSharedRef<FJsonObject> PropertiesSchema = MakeShared<FJsonObject>();
		PropertiesSchema->SetStringField(TEXT("type"), TEXT("array"));
		PropertiesSchema->SetObjectField(TEXT("items"), EntrySchema);

		TSharedRef<FJsonObject> ImportItem = MakeShared<FJsonObject>();
		ImportItem->SetStringField(TEXT("type"), TEXT("string"));
		ImportItem->SetStringField(TEXT("description"), TEXT("Property Schema asset path"));
		TSharedRef<FJsonObject> ImportsSchema = MakeShared<FJsonObject>();
		ImportsSchema->SetStringField(TEXT("type"), TEXT("array"));
		ImportsSchema->SetObjectField(TEXT("items"), ImportItem);

		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Properties, PropertiesSchema);
		Props->SetObjectField(Keys::Imports, ImportsSchema);
		Props->SetObjectField(Keys::ImportOverrides, DescribeOverridesBody().ToSharedRef());

		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		TArray<TSharedPtr<FJsonValue>> RequiredTop;
		RequiredTop.Add(MakeShared<FJsonValueString>(Keys::Properties));
		S->SetArrayField(TEXT("required"), RequiredTop);
		TSharedRef<FJsonObject> ValuesDefs = MakeShared<FJsonObject>();
		ValuesDefs->SetObjectField(TEXT("values"), Defs);
		S->SetObjectField(TEXT("$defs"), ValuesDefs);
		S->SetStringField(TEXT("description"), TEXT("A property schema: local entries, imported Property Schema assets, and enabled value overrides on imported entries."));
		return S;
	}

#pragma endregion

	TSharedPtr<FJsonObject> DescribeWeightedOverridesBody()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> WeightSchema = MakeShared<FJsonObject>();
		WeightSchema->SetStringField(TEXT("type"), TEXT("integer"));
		WeightSchema->SetNumberField(TEXT("minimum"), 0);
		WeightSchema->SetStringField(TEXT("description"), TEXT("distribution weight; 0 never picks the row"));
		Props->SetObjectField(TEXT("weight"), WeightSchema);
		Props->SetObjectField(TEXT("values"), DescribeOverridesBody().ToSharedRef());
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		return S;
	}

	TSharedPtr<FJsonObject> DescribeEnumSelector()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> EnumSchema = MakeShared<FJsonObject>();
		EnumSchema->SetStringField(TEXT("type"), TEXT("string"));
		EnumSchema->SetStringField(TEXT("description"), TEXT("enum class path"));
		Props->SetObjectField(Keys::Enum, EnumSchema);
		Props->SetObjectField(Keys::Value, Values::DescribeEnumShape(nullptr).ToSharedRef());
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		return S;
	}
}

#pragma region FPCGExPropertySchemaCollectionJsonConverter

EJsonObjectConvertResult FPCGExPropertySchemaCollectionJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		PCGExPropertyMediatorFormats::EncodeSchemaBody(*static_cast<const FPCGExPropertySchemaCollection*>(StructMemory), *OutJsonObject);
		return true;
	});
}

EJsonObjectConvertResult FPCGExPropertySchemaCollectionJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	FPCGExPropertySchemaCollection& Live = *static_cast<FPCGExPropertySchemaCollection*>(StructMemory);
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		FPCGExPropertySchemaCollection Temp;
		if (!PCGExPropertyMediatorFormats::DecodeSchemaBody(*InJsonObject, Live, Temp)) { return false; }
		Live = MoveTemp(Temp);
		return true;
	});
}

#pragma endregion

#pragma region FPCGExPropertyOverridesJsonConverter

EJsonObjectConvertResult FPCGExPropertyOverridesJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		PCGExPropertyMediatorFormats::EncodeOverridesBody(*static_cast<const FPCGExPropertyOverrides*>(StructMemory), *OutJsonObject);
		return true;
	});
}

EJsonObjectConvertResult FPCGExPropertyOverridesJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		return PCGExPropertyMediatorFormats::DecodeOverridesBody(*InJsonObject, *static_cast<FPCGExPropertyOverrides*>(StructMemory));
	});
}

#pragma endregion

#pragma region FPCGExWeightedPropertyOverridesJsonConverter

EJsonObjectConvertResult FPCGExWeightedPropertyOverridesJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	const FPCGExWeightedPropertyOverrides& Row = *static_cast<const FPCGExWeightedPropertyOverrides*>(StructMemory);
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		OutJsonObject->SetNumberField(TEXT("weight"), Row.Weight);
		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		{
			PCGExMediator::FPathScope P(TEXT("values"));
			PCGExPropertyMediatorFormats::EncodeOverridesBody(Row, *Values);
		}
		OutJsonObject->SetObjectField(TEXT("values"), Values);
		return true;
	});
}

EJsonObjectConvertResult FPCGExWeightedPropertyOverridesJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	FPCGExWeightedPropertyOverrides& Live = *static_cast<FPCGExWeightedPropertyOverrides*>(StructMemory);
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		FPCGExWeightedPropertyOverrides Temp = Live;
		if (const TSharedPtr<FJsonValue> Weight = InJsonObject->TryGetField(TEXT("weight")))
		{
			PCGExMediator::FPathScope P(TEXT("weight"));
			int32 W = 0;
			if (!PCGExMediator::Values::Decode<int32>(Weight, W)) { return false; }
			if (W < 0)
			{
				PCGExMediator::Report(EPCGExMediatorSeverity::Error, TEXT("weight must be >= 0"));
				return false;
			}
			Temp.Weight = W;
		}
		if (const TSharedPtr<FJsonObject>* Values = nullptr; InJsonObject->TryGetObjectField(TEXT("values"), Values))
		{
			PCGExMediator::FPathScope P(TEXT("values"));
			// The base-typed reference assigns the override set only; Weight stays as decoded above.
			FPCGExPropertyOverrides& Base = Temp;
			if (!PCGExPropertyMediatorFormats::DecodeOverridesBody(**Values, Base)) { return false; }
		}
		Live = MoveTemp(Temp);
		return true;
	});
}

#pragma endregion

#pragma region FPCGExEnumSelectorJsonConverter

EJsonObjectConvertResult FPCGExEnumSelectorJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	using namespace PCGExPropertyMediatorFormats;

	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	const FPCGExEnumSelector& Selector = *static_cast<const FPCGExEnumSelector*>(StructMemory);
	if (Selector.Class) { OutJsonObject->SetStringField(Keys::Enum, Selector.Class->GetPathName()); }
	OutJsonObject->SetField(Keys::Value, Values::EncodeEnum(Selector.Class, Selector.Value));
	return EJsonObjectConvertResult::Converted;
}

EJsonObjectConvertResult FPCGExEnumSelectorJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	using namespace PCGExPropertyMediatorFormats;

	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	FPCGExEnumSelector& Live = *static_cast<FPCGExEnumSelector*>(StructMemory);
	return Guarded([&]()
	{
		FPCGExEnumSelector Temp = Live;
		FString EnumPath;
		if (InJsonObject->TryGetStringField(Keys::Enum, EnumPath))
		{
			FPathScope P(Keys::Enum);
			UEnum* Enum = PCGExPropertyMediator::ResolveEnum(EnumPath);
			if (!Enum) { return false; }
			Temp.Class = Enum;
		}
		if (const TSharedPtr<FJsonValue> Value = InJsonObject->TryGetField(Keys::Value))
		{
			FPathScope P(Keys::Value);
			int64 V = 0;
			if (!Values::DecodeEnum(Temp.Class, Value, V)) { return false; }
			Temp.Value = V;
		}
		Live = Temp;
		return true;
	});
}

#pragma endregion

#pragma region FPCGExNumericRangeJsonConverter

EJsonObjectConvertResult FPCGExNumericRangeJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	const FPCGExNumericRange& Range = *static_cast<const FPCGExNumericRange*>(StructMemory);
	// Always the full object here: the generic walk has no "omitted" notion.
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	OutJsonObject->SetNumberField(TEXT("min"), Range.Min);
	OutJsonObject->SetNumberField(TEXT("max"), Range.Max);
	OutJsonObject->SetBoolField(TEXT("clampMin"), Range.bClampMin);
	OutJsonObject->SetBoolField(TEXT("clampMax"), Range.bClampMax);
	return EJsonObjectConvertResult::Converted;
}

EJsonObjectConvertResult FPCGExNumericRangeJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	return PCGExPropertyMediatorFormats::Guarded([&]()
	{
		return PCGExPropertyMediator::DecodeRange(*InJsonObject, *static_cast<FPCGExNumericRange*>(StructMemory));
	});
}

#pragma endregion

void PCGExPropertyMediatorFormats::Register()
{
	static FPCGExPropertySchemaCollectionJsonConverter SchemaConverter;
	static FPCGExPropertyOverridesJsonConverter OverridesConverter;
	static FPCGExWeightedPropertyOverridesJsonConverter WeightedOverridesConverter;
	static FPCGExEnumSelectorJsonConverter EnumSelectorConverter;
	static FPCGExNumericRangeJsonConverter NumericRangeConverter;

	{
		FPCGExMediatorFormat F;
		F.Id = SchemaFormatId;
		F.Version = 1;
		F.Struct = FPCGExPropertySchemaCollection::StaticStruct();
		F.Converter = &SchemaConverter;
		F.Describe = &DescribeSchemaBody;
		F.Summary = TEXT("Property schema: typed entries with defaults, imported schema assets, import overrides.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
	{
		FPCGExMediatorFormat F;
		F.Id = OverridesFormatId;
		F.Version = 1;
		F.Struct = FPCGExPropertyOverrides::StaticStruct();
		F.Converter = &OverridesConverter;
		F.Describe = &DescribeOverridesBody;
		F.Summary = TEXT("Enabled property overrides by name against a schema.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
	{
		FPCGExMediatorFormat F;
		F.Id = WeightedOverridesFormatId;
		F.Version = 1;
		F.Struct = FPCGExWeightedPropertyOverrides::StaticStruct();
		F.Converter = &WeightedOverridesConverter;
		F.Describe = &DescribeWeightedOverridesBody;
		F.Summary = TEXT("Weighted override row: distribution weight plus enabled overrides.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
	{
		FPCGExMediatorFormat F;
		F.Id = EnumSelectorFormatId;
		F.Version = 1;
		F.Struct = FPCGExEnumSelector::StaticStruct();
		F.Converter = &EnumSelectorConverter;
		F.Describe = &DescribeEnumSelector;
		F.Summary = TEXT("Enum class path plus enumerator name.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
	{
		FPCGExMediatorFormat F;
		F.Id = NumericRangeFormatId;
		F.Version = 1;
		F.Struct = FPCGExNumericRange::StaticStruct();
		F.Converter = &NumericRangeConverter;
		F.Describe = &PCGExPropertyMediator::DescribeRange;
		F.Summary = TEXT("Editor numeric range hints.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
}

void PCGExPropertyMediatorFormats::Unregister()
{
	FPCGExMediatorRegistry::UnregisterFormat(SchemaFormatId);
	FPCGExMediatorRegistry::UnregisterFormat(OverridesFormatId);
	FPCGExMediatorRegistry::UnregisterFormat(WeightedOverridesFormatId);
	FPCGExMediatorRegistry::UnregisterFormat(EnumSelectorFormatId);
	FPCGExMediatorRegistry::UnregisterFormat(NumericRangeFormatId);
}
