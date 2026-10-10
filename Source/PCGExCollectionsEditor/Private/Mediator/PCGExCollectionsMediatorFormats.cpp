// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorFormats.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorValues.h"
#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

namespace PCGExCollectionsMediatorFormats
{
	using namespace PCGExMediator;

	namespace Keys
	{
		const TCHAR* Type = TEXT("type");
		const TCHAR* EntryId = TEXT("entryId");
		const TCHAR* Sockets = TEXT("sockets");
		const TCHAR* BoundsModifier = TEXT("boundsModifier");
		const TCHAR* Path = TEXT("path");
		const TCHAR* Bounds = TEXT("bounds");
	}

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

	bool IsAuthoredProperty(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated | CPF_Transient);
	}

	// Hard references that are not instanced subobjects travel as path strings.
	const FObjectProperty* AsPlainObjectRef(const FProperty* Property)
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		if (!ObjectProperty || Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance)) { return nullptr; }
		return ObjectProperty;
	}

	TSharedPtr<FJsonValue> EncodeProperty(FProperty* Property, const void* ValuePtr)
	{
		if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : FString());
		}
		if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
		{
			return MakeShared<FJsonValueString>(SoftProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath().ToString());
		}
		return FJsonObjectConverter::UPropertyToJsonValue(Property, ValuePtr);
	}

	bool DecodeProperty(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
	{
		if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
		{
			if (!Json.IsValid() || Json->Type != EJson::String)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected an object path string"));
				return false;
			}
			const FString Text = Json->AsString();
			UObject* Object = nullptr;
			if (!Text.IsEmpty())
			{
				Object = PCGExPropertyMediator::ResolveObject(FPackageName::ExportTextPathToObjectPath(Text));
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

		FText FailReason;
		if (!FJsonObjectConverter::JsonValueToUProperty(Json, Property, ValuePtr, 0, 0, false, &FailReason))
		{
			Report(EPCGExMediatorSeverity::Error, FailReason.IsEmpty() ? TEXT("value rejected") : FailReason.ToString());
			return false;
		}
		return true;
	}

	void EncodeEntry(const UScriptStruct* Struct, const FPCGExAssetCollectionEntry& Entry, FJsonObject& Out)
	{
		Out.SetStringField(Keys::Type, Entry.GetTypeId().ToString());
		if (Entry.EntryId != 0) { Out.SetNumberField(Keys::EntryId, Entry.EntryId); }

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			if (!IsAuthoredProperty(Property)) { continue; }
			FPathScope P(Property->GetName());
			const TSharedPtr<FJsonValue> Value = EncodeProperty(Property, Property->ContainerPtrToValuePtr<void>(&Entry));
			if (!Value.IsValid())
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("no JSON representation; omitted"));
				continue;
			}
			Out.SetField(Property->GetName(), Value);
		}
	}

	bool DecodeEntry(const UScriptStruct* Struct, FPCGExAssetCollectionEntry& Entry, const FJsonObject& In)
	{
		bool bOk = true;

		FString TypeText;
		if (In.TryGetStringField(Keys::Type, TypeText))
		{
			PCGExAssetCollection::FTypeInfo Info;
			const bool bKnown = PCGExAssetCollection::FTypeRegistry::Get().GetInfoByEntryStruct(Struct, Info);
			const FName Given(*TypeText);
			if (bKnown && Given != Info.Id && Given != PCGExAssetCollection::TypeIds::Base && !PCGExAssetCollection::FTypeRegistry::Get().IsA(Given, Info.Id))
			{
				Report(EPCGExMediatorSeverity::Error, Keys::Type, FString::Printf(TEXT("'%s' is not the entry type of this collection (%s)"), *TypeText, *Info.Id.ToString()));
				return false;
			}
		}

		if (const TSharedPtr<FJsonValue> Id = In.TryGetField(Keys::EntryId))
		{
			FPathScope P(Keys::EntryId);
			int32 EntryId = 0;
			if (!Values::Decode<int32>(Id, EntryId)) { return false; }
			Entry.EntryId = EntryId;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : In.Values)
		{
			if (Pair.Key == Keys::Type || Pair.Key == Keys::EntryId) { continue; }
			FPathScope P(Pair.Key);
			FProperty* Property = FindFProperty<FProperty>(Struct, *Pair.Key);
			if (!Property || !IsAuthoredProperty(Property))
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("not an authored entry field; ignored"));
				continue;
			}
			if (!DecodeProperty(Property, Property->ContainerPtrToValuePtr<void>(&Entry), Pair.Value)) { bOk = false; }
		}
		return bOk;
	}

	TSharedPtr<FJsonObject> DescribeEntry(const UScriptStruct* Struct)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		{
			TSharedRef<FJsonObject> TypeSchema = MakeShared<FJsonObject>();
			TypeSchema->SetStringField(TEXT("type"), TEXT("string"));
			PCGExAssetCollection::FTypeInfo Info;
			if (PCGExAssetCollection::FTypeRegistry::Get().GetInfoByEntryStruct(Struct, Info)) { TypeSchema->SetStringField(TEXT("const"), Info.Id.ToString()); }
			TypeSchema->SetStringField(TEXT("description"), TEXT("registered collection type id"));
			Props->SetObjectField(Keys::Type, TypeSchema);

			TSharedRef<FJsonObject> IdSchema = MakeShared<FJsonObject>();
			IdSchema->SetStringField(TEXT("type"), TEXT("integer"));
			IdSchema->SetStringField(TEXT("description"), TEXT("stable entry identity; omit for a new entry, the collection mints it"));
			Props->SetObjectField(Keys::EntryId, IdSchema);
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsAuthoredProperty(Property)) { continue; }
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			FString Description = Property->GetCPPType();
			if (AsPlainObjectRef(Property) || Property->IsA<FSoftObjectProperty>()) { Description += TEXT(" as an object path string"); }
			S->SetStringField(TEXT("description"), Description);
			Props->SetObjectField(Property->GetName(), S);
		}
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		S->SetStringField(TEXT("description"), FString::Printf(TEXT("%s: authored fields by name; an absent key leaves the field unchanged"), *Struct->GetName()));
		return S;
	}

	TSharedPtr<FJsonObject> DescribeStaging()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		auto Add = [&Props](const TCHAR* Key, const TCHAR* Description)
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("description"), Description);
			Props->SetObjectField(Key, S);
		};
		Add(Keys::Sockets, TEXT("authored sockets (array)"));
		Add(Keys::BoundsModifier, TEXT("authored bounds modifier (instanced struct with _structType)"));
		Add(Keys::Path, TEXT("read-only: staged asset path"));
		Add(Keys::Bounds, TEXT("read-only: staged bounds { min, max }"));
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		return S;
	}

	struct FState
	{
		TMap<const UScriptStruct*, TUniquePtr<FPCGExCollectionEntryJsonConverter>> EntryConverters;
		TArray<FName> EntryFormatIds;
		FDelegateHandle ModulesChangedHandle;
		bool bRegistered = false;
	};

	FState& State()
	{
		static FState Instance;
		return Instance;
	}

	void RegisterEntryFormats()
	{
		FState& S = State();

		// Collect under the registry lock, register outside it: RegisterFormat must not re-enter.
		TArray<TPair<FName, UScriptStruct*>> Pending;
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([&](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.EntryStruct && !S.EntryConverters.Contains(Info.EntryStruct)) { Pending.Emplace(Info.Id, Info.EntryStruct); }
		});

		for (const TPair<FName, UScriptStruct*>& Item : Pending)
		{
			const UScriptStruct* Struct = Item.Value;
			TUniquePtr<FPCGExCollectionEntryJsonConverter>& Converter = S.EntryConverters.Add(Struct, MakeUnique<FPCGExCollectionEntryJsonConverter>(Struct));

			FPCGExMediatorFormat F;
			F.Id = EntryFormatId(Item.Key);
			F.Version = 1;
			F.Struct = Struct;
			F.Converter = Converter.Get();
			F.Describe = [Struct]() { return DescribeEntry(Struct); };
			F.Summary = FString::Printf(TEXT("%s collection entry."), *Item.Key.ToString());
			FPCGExMediatorRegistry::RegisterFormat(F);
			S.EntryFormatIds.Add(F.Id);
		}
	}

	void OnModulesChanged(FName, const EModuleChangeReason Reason)
	{
		if (Reason == EModuleChangeReason::ModuleLoaded && State().bRegistered) { RegisterEntryFormats(); }
	}
}

FName PCGExCollectionsMediatorFormats::EntryFormatId(const FName TypeId)
{
	return FName(*FString::Printf(TEXT("pcgex.collection-entry/%s"), *TypeId.ToString()));
}

#pragma region FPCGExCollectionEntryJsonConverter

FPCGExCollectionEntryJsonConverter::FPCGExCollectionEntryJsonConverter(const UScriptStruct* InStruct)
	: Struct(InStruct)
{
}

EJsonObjectConvertResult FPCGExCollectionEntryJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	return PCGExCollectionsMediatorFormats::Guarded([&]()
	{
		PCGExCollectionsMediatorFormats::EncodeEntry(Struct, *static_cast<const FPCGExAssetCollectionEntry*>(StructMemory), *OutJsonObject);
		return true;
	});
}

EJsonObjectConvertResult FPCGExCollectionEntryJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	return PCGExCollectionsMediatorFormats::Guarded([&]()
	{
		FInstancedStruct Temp;
		Temp.InitializeAs(Struct, static_cast<const uint8*>(StructMemory));
		if (!PCGExCollectionsMediatorFormats::DecodeEntry(Struct, *Temp.GetMutablePtr<FPCGExAssetCollectionEntry>(), *InJsonObject)) { return false; }
		Struct->CopyScriptStruct(StructMemory, Temp.GetMemory());
		return true;
	});
}

#pragma endregion

#pragma region FPCGExOmniCollectionEntryJsonConverter

EJsonObjectConvertResult FPCGExOmniCollectionEntryJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	const FPCGExOmniCollectionEntry& Row = *static_cast<const FPCGExOmniCollectionEntry*>(StructMemory);
	if (!Row.Entry.IsValid()) { return EJsonObjectConvertResult::Converted; }
	return PCGExCollectionsMediatorFormats::Guarded([&]()
	{
		PCGExCollectionsMediatorFormats::EncodeEntry(Row.Entry.GetScriptStruct(), *Row.GetPayload(), *OutJsonObject);
		return true;
	});
}

EJsonObjectConvertResult FPCGExOmniCollectionEntryJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	using namespace PCGExCollectionsMediatorFormats;

	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	FPCGExOmniCollectionEntry& Live = *static_cast<FPCGExOmniCollectionEntry*>(StructMemory);
	return Guarded([&]()
	{
		FPCGExOmniCollectionEntry Temp = Live;

		FString TypeText;
		if (InJsonObject->TryGetStringField(Keys::Type, TypeText))
		{
			FPathScope P(Keys::Type);
			PCGExAssetCollection::FTypeInfo Info;
			if (!PCGExAssetCollection::FTypeRegistry::Get().GetInfo(FName(*TypeText), Info) || !Info.EntryStruct)
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a collection type with an entry struct"), *TypeText));
				return false;
			}
			if (Temp.Entry.GetScriptStruct() != Info.EntryStruct) { Temp.Entry.InitializeAs(Info.EntryStruct); }
		}
		if (!Temp.Entry.IsValid())
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Type, TEXT("required for a new Omni row"));
			return false;
		}
		if (!DecodeEntry(Temp.Entry.GetScriptStruct(), *Temp.GetPayload(), *InJsonObject)) { return false; }
		Live = MoveTemp(Temp);
		return true;
	});
}

#pragma endregion

#pragma region FPCGExAssetStagingDataJsonConverter

EJsonObjectConvertResult FPCGExAssetStagingDataJsonConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	using namespace PCGExCollectionsMediatorFormats;

	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	const FPCGExAssetStagingData& Staging = *static_cast<const FPCGExAssetStagingData*>(StructMemory);
	const UScriptStruct* Struct = FPCGExAssetStagingData::StaticStruct();

	if (!Staging.Sockets.IsEmpty())
	{
		if (FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets)))
		{
			OutJsonObject->SetField(Keys::Sockets, FJsonObjectConverter::UPropertyToJsonValue(P, &Staging.Sockets));
		}
	}
	if (Staging.BoundsStagingModifier.IsValid())
	{
		if (FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier)))
		{
			OutJsonObject->SetField(Keys::BoundsModifier, FJsonObjectConverter::UPropertyToJsonValue(P, &Staging.BoundsStagingModifier));
		}
	}
	if (!Staging.Path.IsNull()) { OutJsonObject->SetStringField(Keys::Path, Staging.Path.ToString()); }
	if (Staging.Bounds.IsValid)
	{
		TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
		Bounds->SetField(TEXT("min"), Values::Encode<FVector>(Staging.Bounds.Min));
		Bounds->SetField(TEXT("max"), Values::Encode<FVector>(Staging.Bounds.Max));
		OutJsonObject->SetObjectField(Keys::Bounds, Bounds);
	}
	return EJsonObjectConvertResult::Converted;
}

EJsonObjectConvertResult FPCGExAssetStagingDataJsonConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	using namespace PCGExCollectionsMediatorFormats;

	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	FPCGExAssetStagingData& Live = *static_cast<FPCGExAssetStagingData*>(StructMemory);
	return Guarded([&]()
	{
		FPCGExAssetStagingData Temp = Live;
		const UScriptStruct* Struct = FPCGExAssetStagingData::StaticStruct();
		auto Apply = [&](const TCHAR* Key, const FName Member) -> bool
		{
			const TSharedPtr<FJsonValue> Json = InJsonObject->TryGetField(Key);
			if (!Json.IsValid()) { return true; }
			FPathScope P(Key);
			FProperty* Property = FindFProperty<FProperty>(Struct, Member);
			return Property && DecodeProperty(Property, Property->ContainerPtrToValuePtr<void>(&Temp), Json);
		};
		if (!Apply(Keys::Sockets, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets))) { return false; }
		if (!Apply(Keys::BoundsModifier, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier))) { return false; }
		// "path" and "bounds" are staging output; the rebuild after import recomputes them.
		Live = MoveTemp(Temp);
		return true;
	});
}

#pragma endregion

void PCGExCollectionsMediatorFormats::Register()
{
	static FPCGExOmniCollectionEntryJsonConverter OmniConverter;
	static FPCGExAssetStagingDataJsonConverter StagingConverter;

	FState& S = State();
	S.bRegistered = true;

	{
		FPCGExMediatorFormat F;
		F.Id = OmniEntryFormatId;
		F.Version = 1;
		F.Struct = FPCGExOmniCollectionEntry::StaticStruct();
		F.Converter = &OmniConverter;
		F.Describe = []() { return DescribeEntry(FPCGExAssetCollectionEntry::StaticStruct()); };
		F.Summary = TEXT("Omni collection row: any registered entry type, chosen by \"type\".");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}
	{
		FPCGExMediatorFormat F;
		F.Id = StagingFormatId;
		F.Version = 1;
		F.Struct = FPCGExAssetStagingData::StaticStruct();
		F.Converter = &StagingConverter;
		F.Describe = &DescribeStaging;
		F.Summary = TEXT("Entry staging data: authored sockets and bounds modifier.");
		FPCGExMediatorRegistry::RegisterFormat(F);
	}

	RegisterEntryFormats();
	if (!S.ModulesChangedHandle.IsValid())
	{
		S.ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&OnModulesChanged);
	}
}

void PCGExCollectionsMediatorFormats::Unregister()
{
	FState& S = State();
	S.bRegistered = false;
	if (S.ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(S.ModulesChangedHandle);
		S.ModulesChangedHandle.Reset();
	}
	for (const FName& Id : S.EntryFormatIds) { FPCGExMediatorRegistry::UnregisterFormat(Id); }
	S.EntryFormatIds.Reset();
	S.EntryConverters.Reset();
	FPCGExMediatorRegistry::UnregisterFormat(OmniEntryFormatId);
	FPCGExMediatorRegistry::UnregisterFormat(StagingFormatId);
}
