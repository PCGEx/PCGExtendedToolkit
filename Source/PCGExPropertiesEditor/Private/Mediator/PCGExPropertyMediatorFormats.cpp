// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorFormats.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
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
		constexpr const TCHAR* const Properties = TEXT("properties");
		constexpr const TCHAR* const Name = TEXT("name");
		constexpr const TCHAR* const Type = TEXT("type");
		constexpr const TCHAR* const Default = TEXT("default");
		constexpr const TCHAR* const Imports = TEXT("imports");
		constexpr const TCHAR* const ImportOverrides = TEXT("importOverrides");
		constexpr const TCHAR* const Value = TEXT("value");
		constexpr const TCHAR* const Enum = TEXT("enum");
		constexpr const TCHAR* const Weight = TEXT("weight");
		constexpr const TCHAR* const Values = TEXT("values");
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

	// Temp is the caller's scratch copy. It must be schema-parallel, or an FOverridesSchemaScope must be active; with
	// neither, only the { "type", "value" } form can create entries (the host's next schema sync matches them by name).
	bool DecodeOverridesBody(const FJsonObject& In, FPCGExPropertyOverrides& Temp)
	{
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
		return true;
	}

	TSharedPtr<FJsonObject> DescribeOverridesBody()
	{
		TSharedRef<FJsonObject> S = Schema::Typed(TEXT("object"),
		                                          TEXT("Enabled overrides by property name; every name absent from the document is disabled. A value takes the shape of the "
			                                          "property's type (see the property-schema format's $defs/values). Without a schema to resolve names against, an entry "
			                                          "can be created as { \"type\": <type name>, \"value\": <value> }."));
		S->SetBoolField(TEXT("additionalProperties"), true);
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
			Entry->SetStringField(Keys::Type, (Info && !Info->Entry.TypeName.IsNone()) ? Info->Entry.TypeName.ToString() : Struct->GetPathName());

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

	// Temp is rebuilt from the document; Live only lends HeaderIds, by name, so existing overrides survive a re-import.
	bool DecodeSchemaBody(const FJsonObject& In, const FPCGExPropertySchemaCollection& Live, FPCGExPropertySchemaCollection& Temp)
	{
		bool bOk = true;
		Temp = FPCGExPropertySchemaCollection();

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
						Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("type %s accepts no JSON value; default kept"), *Info->Entry.TypeName.ToString()));
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
				UPCGExPropertySchemaAsset* Asset = Cast<UPCGExPropertySchemaAsset>(ResolveObject(ImportValue->AsString()));
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
		Temp.ReconcileImportOverrides();

		if (const TSharedPtr<FJsonObject>* Overrides = nullptr; In.TryGetObjectField(Keys::ImportOverrides, Overrides))
		{
			FPathScope OverridesPath(Keys::ImportOverrides);
			if (!DecodeOverridesBody(**Overrides, Temp.ImportOverrides)) { return false; }
		}
		return true;
	}

	TSharedPtr<FJsonObject> DescribeSchemaBody()
	{
		TArray<FString> TypeNames;
		for (const FPCGExPropertyTypeInfo& Info : PCGExPropertyCatalog::Get())
		{
			if (!Info.Entry.TypeName.IsNone()) { TypeNames.Add(Info.Entry.TypeName.ToString()); }
		}

		TSharedRef<FJsonObject> EntryProps = MakeShared<FJsonObject>();
		EntryProps->SetObjectField(Keys::Name, Schema::String(TEXT("unique within the schema; the output attribute name")));
		EntryProps->SetObjectField(Keys::Type, Schema::Enum(TypeNames, TEXT("property type name (a struct path is also accepted)")));
		EntryProps->SetObjectField(Keys::Default, Schema::Typed(nullptr, TEXT("the entry's default value, in the shape of its type: see $defs/values/<type>")));

		TSharedRef<FJsonObject> Defs = MakeShared<FJsonObject>();
		for (const FPCGExPropertyTypeInfo& Info : PCGExPropertyCatalog::Get())
		{
			const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Info.Struct);
			if (!Hooks) { continue; }
			if (Hooks->DescribeStructural) { Hooks->DescribeStructural(*EntryProps); }
			if (Hooks->DescribeValue && !Info.Entry.TypeName.IsNone())
			{
				FInstancedStruct Prototype;
				PCGExPropertyCatalog::MakeProperty(Info, NAME_None, Prototype);
				if (const TSharedPtr<FJsonObject> Shape = Hooks->DescribeValue(Prototype.GetPtr<FPCGExProperty>()))
				{
					Defs->SetObjectField(Info.Entry.TypeName.ToString(), Shape.ToSharedRef());
				}
			}
		}

		const FString EntryRequired[] = {Keys::Name, Keys::Type};
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Properties, Schema::Array(Schema::Object(EntryProps, FString(), EntryRequired)));
		Props->SetObjectField(Keys::Imports, Schema::Array(Schema::String(TEXT("Property Schema asset path"))));
		Props->SetObjectField(Keys::ImportOverrides, DescribeOverridesBody().ToSharedRef());

		const FString Required[] = {Keys::Properties};
		TSharedRef<FJsonObject> S = Schema::Object(Props, TEXT("A property schema: local entries, imported Property Schema assets, and enabled value overrides on imported entries."), Required);
		TSharedRef<FJsonObject> ValuesDefs = MakeShared<FJsonObject>();
		ValuesDefs->SetObjectField(TEXT("values"), Defs);
		S->SetObjectField(TEXT("$defs"), ValuesDefs);
		return S;
	}

#pragma endregion

	TSharedPtr<FJsonObject> DescribeWeightedOverridesBody()
	{
		TSharedRef<FJsonObject> WeightSchema = Schema::Integer(TEXT("distribution weight; 0 never picks the row"));
		WeightSchema->SetNumberField(TEXT("minimum"), 0);

		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Weight, WeightSchema);
		Props->SetObjectField(Keys::Values, DescribeOverridesBody().ToSharedRef());
		return Schema::Object(Props);
	}

	TSharedPtr<FJsonObject> DescribeEnumSelector()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Enum, Schema::String(TEXT("enum class path")));
		Props->SetObjectField(Keys::Value, Values::DescribeEnumShape(nullptr).ToSharedRef());
		return Schema::Object(Props);
	}
}

#pragma region FPCGExPropertySchemaCollectionJsonConverter

bool FPCGExPropertySchemaCollectionJsonConverter::Encode(const FPCGExPropertySchemaCollection& Value, FJsonObject& Out) const
{
	PCGExPropertyMediatorFormats::EncodeSchemaBody(Value, Out);
	return true;
}

bool FPCGExPropertySchemaCollectionJsonConverter::Decode(const FJsonObject& In, FPCGExPropertySchemaCollection& Temp, const FPCGExPropertySchemaCollection& Live) const
{
	return PCGExPropertyMediatorFormats::DecodeSchemaBody(In, Live, Temp);
}

#pragma endregion

#pragma region FPCGExPropertyOverridesJsonConverter

bool FPCGExPropertyOverridesJsonConverter::Encode(const FPCGExPropertyOverrides& Value, FJsonObject& Out) const
{
	PCGExPropertyMediatorFormats::EncodeOverridesBody(Value, Out);
	return true;
}

bool FPCGExPropertyOverridesJsonConverter::Decode(const FJsonObject& In, FPCGExPropertyOverrides& Temp, const FPCGExPropertyOverrides&) const
{
	return PCGExPropertyMediatorFormats::DecodeOverridesBody(In, Temp);
}

#pragma endregion

#pragma region FPCGExWeightedPropertyOverridesJsonConverter

bool FPCGExWeightedPropertyOverridesJsonConverter::Encode(const FPCGExWeightedPropertyOverrides& Value, FJsonObject& Out) const
{
	using namespace PCGExPropertyMediatorFormats;

	Out.SetNumberField(Keys::Weight, Value.Weight);
	TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
	{
		FPathScope P(Keys::Values);
		EncodeOverridesBody(Value, *Values);
	}
	Out.SetObjectField(Keys::Values, Values);
	return true;
}

bool FPCGExWeightedPropertyOverridesJsonConverter::Decode(const FJsonObject& In, FPCGExWeightedPropertyOverrides& Temp, const FPCGExWeightedPropertyOverrides&) const
{
	using namespace PCGExPropertyMediatorFormats;

	if (const TSharedPtr<FJsonValue> Weight = In.TryGetField(Keys::Weight))
	{
		FPathScope P(Keys::Weight);
		int32 W = 0;
		if (!Values::Decode<int32>(Weight, W)) { return false; }
		if (W < 0)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("weight must be >= 0"));
			return false;
		}
		Temp.Weight = W;
	}
	if (const TSharedPtr<FJsonObject>* Values = nullptr; In.TryGetObjectField(Keys::Values, Values))
	{
		FPathScope P(Keys::Values);
		if (!DecodeOverridesBody(**Values, Temp)) { return false; }
	}
	return true;
}

#pragma endregion

#pragma region FPCGExEnumSelectorJsonConverter

bool FPCGExEnumSelectorJsonConverter::Encode(const FPCGExEnumSelector& Value, FJsonObject& Out) const
{
	using namespace PCGExPropertyMediatorFormats;

	if (Value.Class) { Out.SetStringField(Keys::Enum, Value.Class->GetPathName()); }
	Out.SetField(Keys::Value, Values::EncodeEnum(Value.Class, Value.Value));
	return true;
}

bool FPCGExEnumSelectorJsonConverter::Decode(const FJsonObject& In, FPCGExEnumSelector& Temp, const FPCGExEnumSelector&) const
{
	using namespace PCGExPropertyMediatorFormats;

	FString EnumPath;
	if (In.TryGetStringField(Keys::Enum, EnumPath))
	{
		FPathScope P(Keys::Enum);
		UEnum* Enum = PCGExPropertyMediator::ResolveEnum(EnumPath);
		if (!Enum) { return false; }
		Temp.Class = Enum;
	}
	if (const TSharedPtr<FJsonValue> Value = In.TryGetField(Keys::Value))
	{
		FPathScope P(Keys::Value);
		int64 V = 0;
		if (!Values::DecodeEnum(Temp.Class, Value, V)) { return false; }
		Temp.Value = V;
	}
	return true;
}

#pragma endregion

#pragma region FPCGExNumericRangeJsonConverter

bool FPCGExNumericRangeJsonConverter::Encode(const FPCGExNumericRange& Value, FJsonObject& Out) const
{
	// Always the full object here: the generic walk has no "omitted" notion.
	Out.SetNumberField(TEXT("min"), Value.Min);
	Out.SetNumberField(TEXT("max"), Value.Max);
	Out.SetBoolField(TEXT("clampMin"), Value.bClampMin);
	Out.SetBoolField(TEXT("clampMax"), Value.bClampMax);
	return true;
}

bool FPCGExNumericRangeJsonConverter::Decode(const FJsonObject& In, FPCGExNumericRange& Temp, const FPCGExNumericRange&) const
{
	return PCGExPropertyMediator::DecodeRange(In, Temp);
}

#pragma endregion

void PCGExPropertyMediatorFormats::Register(FPCGExMediatorDomain& Domain)
{
	Domain.AddFormat<FPCGExPropertySchemaCollectionJsonConverter>(SchemaFormatId, 1, FPCGExPropertySchemaCollection::StaticStruct(), &DescribeSchemaBody,
	                                                               TEXT("Property schema: typed entries with defaults, imported schema assets, import overrides."));
	Domain.AddFormat<FPCGExPropertyOverridesJsonConverter>(OverridesFormatId, 1, FPCGExPropertyOverrides::StaticStruct(), &DescribeOverridesBody,
	                                                        TEXT("Enabled property overrides by name against a schema."));
	Domain.AddFormat<FPCGExWeightedPropertyOverridesJsonConverter>(WeightedOverridesFormatId, 1, FPCGExWeightedPropertyOverrides::StaticStruct(), &DescribeWeightedOverridesBody,
	                                                                TEXT("Weighted override row: distribution weight plus enabled overrides."));
	Domain.AddFormat<FPCGExEnumSelectorJsonConverter>(EnumSelectorFormatId, 1, FPCGExEnumSelector::StaticStruct(), &DescribeEnumSelector,
	                                                   TEXT("Enum class path plus enumerator name."));
	Domain.AddFormat<FPCGExNumericRangeJsonConverter>(NumericRangeFormatId, 1, FPCGExNumericRange::StaticStruct(), &PCGExPropertyMediator::DescribeRange,
	                                                   TEXT("Editor numeric range hints."));
}
