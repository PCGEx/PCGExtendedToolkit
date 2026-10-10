// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorFormats.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorReflection.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

namespace PCGExCollectionsMediatorFormats
{
	using namespace PCGExMediator;

	namespace Keys
	{
		constexpr const TCHAR* const Type = TEXT("type");
		constexpr const TCHAR* const EntryId = TEXT("entryId");
		constexpr const TCHAR* const Sockets = TEXT("sockets");
		constexpr const TCHAR* const BoundsModifier = TEXT("boundsModifier");
		constexpr const TCHAR* const Path = TEXT("path");
		constexpr const TCHAR* const Bounds = TEXT("bounds");
	}

	const FString EntryOwnKeys[] = {TEXT("type"), TEXT("entryId")};

	bool IsAuthoredProperty(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated | CPF_Transient);
	}

	// Sparse: a field at its struct default is omitted, which keeps descriptor trees out unless authored.
	void EncodeEntry(const UScriptStruct* Struct, const FPCGExAssetCollectionEntry& Entry, FJsonObject& Out)
	{
		Out.SetStringField(Keys::Type, Entry.GetTypeId().ToString());
		if (Entry.EntryId != 0) { Out.SetNumberField(Keys::EntryId, Entry.EntryId); }

		FInstancedStruct Defaults;
		Defaults.InitializeAs(Struct);
		Reflect::EncodeStruct(Struct, &Entry, Defaults.GetMemory(), Out, &IsAuthoredProperty);
	}

	bool DecodeEntry(const UScriptStruct* Struct, FPCGExAssetCollectionEntry& Entry, const FJsonObject& In)
	{
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

		return Reflect::DecodeStruct(Struct, &Entry, In, &IsAuthoredProperty, EntryOwnKeys);
	}

	void AddEntryOwnSchemas(FJsonObject& Props, const TSharedRef<FJsonObject>& TypeSchema)
	{
		Props.SetObjectField(Keys::Type, TypeSchema);
		Props.SetObjectField(Keys::EntryId, Schema::Integer(TEXT("stable entry identity; omit for a new entry, the collection mints it on its next staging rebuild")));
	}

	TSharedPtr<FJsonObject> DescribeEntry(const UScriptStruct* Struct)
	{
		// Depth 0: every nested plain struct (descriptors, variations) collapses to its DescribeFormat path; a Mesh entry
		// schema otherwise carries two 20 KB descriptor trees.
		TSharedPtr<FJsonObject> S = Reflect::DescribeStruct(Struct, &IsAuthoredProperty, 0);
		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!S.IsValid() || !S->TryGetObjectField(TEXT("properties"), Props)) { return S; }

		TSharedRef<FJsonObject> TypeSchema = Schema::String(TEXT("registered collection type id"));
		PCGExAssetCollection::FTypeInfo Info;
		if (PCGExAssetCollection::FTypeRegistry::Get().GetInfoByEntryStruct(Struct, Info)) { TypeSchema->SetStringField(TEXT("const"), Info.Id.ToString()); }
		AddEntryOwnSchemas(**Props, TypeSchema);

		Schema::Describe(S.ToSharedRef(), FString::Printf(TEXT("%s: authored fields by name. Export is sparse (fields at their default are omitted); import merges (an absent key leaves the field unchanged on an existing row, at its default on a new one)."), *Struct->GetName()));
		return S;
	}

	TSharedPtr<FJsonObject> DescribeOmniRow()
	{
		TSharedPtr<FJsonObject> S = Reflect::DescribeStruct(FPCGExAssetCollectionEntry::StaticStruct(), &IsAuthoredProperty, 0);
		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!S.IsValid() || !S->TryGetObjectField(TEXT("properties"), Props)) { return S; }

		TArray<FString> TypeIds;
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([&TypeIds](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.EntryStruct) { TypeIds.Add(Info.Id.ToString()); }
		});
		AddEntryOwnSchemas(**Props, Schema::Enum(TypeIds, TEXT("the row's entry type; required for a new row, switches the payload struct on an existing one")));

		Schema::Describe(S.ToSharedRef(), TEXT("Omni row: the base entry fields listed here plus the type's own fields, described by pcgex.collection-entry/<type>. Sparse export, merging import."));
		return S;
	}

	TSharedPtr<FJsonObject> DescribeStaging()
	{
		const UScriptStruct* Struct = FPCGExAssetStagingData::StaticStruct();
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		if (const FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets)))
		{
			if (const TSharedPtr<FJsonObject> S = Reflect::DescribeProperty(P)) { Props->SetObjectField(Keys::Sockets, S.ToSharedRef()); }
		}
		if (const FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier)))
		{
			if (const TSharedPtr<FJsonObject> S = Reflect::DescribeProperty(P)) { Props->SetObjectField(Keys::BoundsModifier, S.ToSharedRef()); }
		}
		Props->SetObjectField(Keys::Path, Schema::String(TEXT("read-only: staged asset path")));
		Props->SetObjectField(Keys::Bounds, Schema::Typed(TEXT("object"), TEXT("read-only: staged bounds { min: [x, y, z], max: [x, y, z] }")));
		return Schema::Object(Props, TEXT("authored staging data; path and bounds are recomputed by the staging rebuild and ignored on import"));
	}

	struct FState
	{
		FPCGExMediatorDomain* Domain = nullptr;
		FDelegateHandle ModulesChangedHandle;
	};

	FState& State()
	{
		static FState Instance;
		return Instance;
	}

	void RegisterEntryFormats()
	{
		FPCGExMediatorDomain* Domain = State().Domain;
		if (!Domain) { return; }

		// Collect under the registry lock, register outside it: the registry must not re-enter.
		TArray<TPair<FName, UScriptStruct*>> Pending;
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([&](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.EntryStruct && !Domain->HasFormatForStruct(Info.EntryStruct)) { Pending.Emplace(Info.Id, Info.EntryStruct); }
		});

		for (const TPair<FName, UScriptStruct*>& Item : Pending)
		{
			const UScriptStruct* Struct = Item.Value;
			Domain->AddFormat<FPCGExCollectionEntryJsonConverter>(
				EntryFormatId(Item.Key), 1, Struct, [Struct]() { return DescribeEntry(Struct); },
				FString::Printf(TEXT("%s collection entry."), *Item.Key.ToString()), Struct);
		}
	}

	void OnModulesChanged(FName, const EModuleChangeReason Reason)
	{
		if (Reason == EModuleChangeReason::ModuleLoaded) { RegisterEntryFormats(); }
	}
}

FName PCGExCollectionsMediatorFormats::EntryFormatId(const FName TypeId)
{
	return FName(*FString::Printf(TEXT("pcgex.collection-entry/%s"), *TypeId.ToString()));
}

#pragma region FPCGExCollectionEntryJsonConverter

FPCGExCollectionEntryJsonConverter::FPCGExCollectionEntryJsonConverter(const UScriptStruct* InStruct)
	: FPCGExMediatorScriptStructConverter(InStruct)
{
}

bool FPCGExCollectionEntryJsonConverter::Encode(const void* Value, FJsonObject& Out) const
{
	PCGExCollectionsMediatorFormats::EncodeEntry(GetStruct(), *static_cast<const FPCGExAssetCollectionEntry*>(Value), Out);
	return true;
}

bool FPCGExCollectionEntryJsonConverter::Decode(const FJsonObject& In, void* Temp, const void*) const
{
	return PCGExCollectionsMediatorFormats::DecodeEntry(GetStruct(), *static_cast<FPCGExAssetCollectionEntry*>(Temp), In);
}

#pragma endregion

#pragma region FPCGExOmniCollectionEntryJsonConverter

bool FPCGExOmniCollectionEntryJsonConverter::Encode(const FPCGExOmniCollectionEntry& Value, FJsonObject& Out) const
{
	if (!Value.Entry.IsValid()) { return true; }
	PCGExCollectionsMediatorFormats::EncodeEntry(Value.Entry.GetScriptStruct(), *Value.GetPayload(), Out);
	return true;
}

bool FPCGExOmniCollectionEntryJsonConverter::Decode(const FJsonObject& In, FPCGExOmniCollectionEntry& Temp, const FPCGExOmniCollectionEntry&) const
{
	using namespace PCGExCollectionsMediatorFormats;

	FString TypeText;
	if (In.TryGetStringField(Keys::Type, TypeText))
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
	return DecodeEntry(Temp.Entry.GetScriptStruct(), *Temp.GetPayload(), In);
}

#pragma endregion

#pragma region FPCGExAssetStagingDataJsonConverter

bool FPCGExAssetStagingDataJsonConverter::Encode(const FPCGExAssetStagingData& Value, FJsonObject& Out) const
{
	using namespace PCGExCollectionsMediatorFormats;

	const UScriptStruct* Struct = FPCGExAssetStagingData::StaticStruct();
	auto Write = [&](const TCHAR* Key, const FName Member, const void* Field)
	{
		const FProperty* Property = FindFProperty<FProperty>(Struct, Member);
		if (!Property) { return; }
		FPathScope S(Key);
		if (const TSharedPtr<FJsonValue> Json = Reflect::EncodeProperty(Property, Field)) { Out.SetField(Key, Json); }
	};
	if (!Value.Sockets.IsEmpty()) { Write(Keys::Sockets, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets), &Value.Sockets); }
	if (Value.BoundsStagingModifier.IsValid()) { Write(Keys::BoundsModifier, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier), &Value.BoundsStagingModifier); }
	if (!Value.Path.IsNull()) { Out.SetStringField(Keys::Path, Value.Path.ToString()); }
	if (Value.Bounds.IsValid)
	{
		TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
		Bounds->SetField(TEXT("min"), Values::Encode<FVector>(Value.Bounds.Min));
		Bounds->SetField(TEXT("max"), Values::Encode<FVector>(Value.Bounds.Max));
		Out.SetObjectField(Keys::Bounds, Bounds);
	}
	return true;
}

bool FPCGExAssetStagingDataJsonConverter::Decode(const FJsonObject& In, FPCGExAssetStagingData& Temp, const FPCGExAssetStagingData&) const
{
	using namespace PCGExCollectionsMediatorFormats;

	const UScriptStruct* Struct = FPCGExAssetStagingData::StaticStruct();
	auto Apply = [&](const TCHAR* Key, const FName Member) -> bool
	{
		const TSharedPtr<FJsonValue> Json = In.TryGetField(Key);
		if (!Json.IsValid()) { return true; }
		FPathScope P(Key);
		const FProperty* Property = FindFProperty<FProperty>(Struct, Member);
		return Property && Reflect::DecodeProperty(Property, Property->ContainerPtrToValuePtr<void>(&Temp), Json);
	};
	if (!Apply(Keys::Sockets, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets))) { return false; }
	if (!Apply(Keys::BoundsModifier, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier))) { return false; }
	// "path" and "bounds" are staging output; the rebuild after import recomputes them.
	return true;
}

#pragma endregion

void PCGExCollectionsMediatorFormats::Register(FPCGExMediatorDomain& Domain)
{
	FState& S = State();
	S.Domain = &Domain;

	Domain.AddFormat<FPCGExOmniCollectionEntryJsonConverter>(OmniEntryFormatId, 1, FPCGExOmniCollectionEntry::StaticStruct(), &DescribeOmniRow,
	                                                          TEXT("Omni collection row: any registered entry type, chosen by \"type\"."));
	Domain.AddFormat<FPCGExAssetStagingDataJsonConverter>(StagingFormatId, 1, FPCGExAssetStagingData::StaticStruct(), &DescribeStaging,
	                                                       TEXT("Entry staging data: authored sockets and bounds modifier."));

	RegisterEntryFormats();
	if (!S.ModulesChangedHandle.IsValid())
	{
		S.ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&OnModulesChanged);
	}
}

void PCGExCollectionsMediatorFormats::Unregister()
{
	FState& S = State();
	if (S.ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(S.ModulesChangedHandle);
		S.ModulesChangedHandle.Reset();
	}
	S.Domain = nullptr;
}
