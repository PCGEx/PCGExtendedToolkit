// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorFormats.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorReflection.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorValues.h"
#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"
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
		const TCHAR* Type = TEXT("type");
		const TCHAR* EntryId = TEXT("entryId");
		const TCHAR* Sockets = TEXT("sockets");
		const TCHAR* BoundsModifier = TEXT("boundsModifier");
		const TCHAR* Path = TEXT("path");
		const TCHAR* Bounds = TEXT("bounds");
	}

	const FString EntryOwnKeys[] = {TEXT("type"), TEXT("entryId")};

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

	void AddEntryOwnSchemas(FJsonObject& Props, const TSharedPtr<FJsonObject>& TypeSchema)
	{
		Props.SetObjectField(Keys::Type, TypeSchema.ToSharedRef());
		TSharedRef<FJsonObject> IdSchema = MakeShared<FJsonObject>();
		IdSchema->SetStringField(TEXT("type"), TEXT("integer"));
		IdSchema->SetStringField(TEXT("description"), TEXT("stable entry identity; omit for a new entry, the collection mints it on its next staging rebuild"));
		Props.SetObjectField(Keys::EntryId, IdSchema);
	}

	TSharedPtr<FJsonObject> DescribeEntry(const UScriptStruct* Struct)
	{
		// Depth 0: every nested plain struct (descriptors, variations) collapses to its DescribeFormat path; a Mesh entry
		// schema otherwise carries two 20 KB descriptor trees.
		TSharedPtr<FJsonObject> S = Reflect::DescribeStruct(Struct, &IsAuthoredProperty, 0);
		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!S.IsValid() || !S->TryGetObjectField(TEXT("properties"), Props)) { return S; }

		TSharedRef<FJsonObject> TypeSchema = MakeShared<FJsonObject>();
		TypeSchema->SetStringField(TEXT("type"), TEXT("string"));
		PCGExAssetCollection::FTypeInfo Info;
		if (PCGExAssetCollection::FTypeRegistry::Get().GetInfoByEntryStruct(Struct, Info)) { TypeSchema->SetStringField(TEXT("const"), Info.Id.ToString()); }
		TypeSchema->SetStringField(TEXT("description"), TEXT("registered collection type id"));
		AddEntryOwnSchemas(**Props, TypeSchema);

		S->SetStringField(TEXT("description"), FString::Printf(TEXT("%s: authored fields by name. Export is sparse (fields at their default are omitted); import merges (an absent key leaves the field unchanged on an existing row, at its default on a new one)."), *Struct->GetName()));
		return S;
	}

	TSharedPtr<FJsonObject> DescribeOmniRow()
	{
		TSharedPtr<FJsonObject> S = Reflect::DescribeStruct(FPCGExAssetCollectionEntry::StaticStruct(), &IsAuthoredProperty, 0);
		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (!S.IsValid() || !S->TryGetObjectField(TEXT("properties"), Props)) { return S; }

		TArray<TSharedPtr<FJsonValue>> TypeIds;
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([&TypeIds](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.EntryStruct) { TypeIds.Add(MakeShared<FJsonValueString>(Info.Id.ToString())); }
		});
		TSharedRef<FJsonObject> TypeSchema = MakeShared<FJsonObject>();
		TypeSchema->SetStringField(TEXT("type"), TEXT("string"));
		TypeSchema->SetArrayField(TEXT("enum"), TypeIds);
		TypeSchema->SetStringField(TEXT("description"), TEXT("the row's entry type; required for a new row, switches the payload struct on an existing one"));
		AddEntryOwnSchemas(**Props, TypeSchema);

		S->SetStringField(TEXT("description"), TEXT("Omni row: the base entry fields listed here plus the type's own fields, described by pcgex.collection-entry/<type>. Sparse export, merging import."));
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
		TSharedRef<FJsonObject> PathSchema = MakeShared<FJsonObject>();
		PathSchema->SetStringField(TEXT("type"), TEXT("string"));
		PathSchema->SetStringField(TEXT("description"), TEXT("read-only: staged asset path"));
		Props->SetObjectField(Keys::Path, PathSchema);
		TSharedRef<FJsonObject> BoundsSchema = MakeShared<FJsonObject>();
		BoundsSchema->SetStringField(TEXT("type"), TEXT("object"));
		BoundsSchema->SetStringField(TEXT("description"), TEXT("read-only: staged bounds { min: [x, y, z], max: [x, y, z] }"));
		Props->SetObjectField(Keys::Bounds, BoundsSchema);

		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetObjectField(TEXT("properties"), Props);
		S->SetStringField(TEXT("description"), TEXT("authored staging data; path and bounds are recomputed by the staging rebuild and ignored on import"));
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
		if (const FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Sockets)))
		{
			FPathScope S(Keys::Sockets);
			OutJsonObject->SetField(Keys::Sockets, Reflect::EncodeProperty(P, &Staging.Sockets));
		}
	}
	if (Staging.BoundsStagingModifier.IsValid())
	{
		if (const FProperty* P = FindFProperty<FProperty>(Struct, GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, BoundsStagingModifier)))
		{
			FPathScope S(Keys::BoundsModifier);
			OutJsonObject->SetField(Keys::BoundsModifier, Reflect::EncodeProperty(P, &Staging.BoundsStagingModifier));
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
			const FProperty* Property = FindFProperty<FProperty>(Struct, Member);
			return Property && Reflect::DecodeProperty(Property, Property->ContainerPtrToValuePtr<void>(&Temp), Json);
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
		F.Describe = &DescribeOmniRow;
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
