// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorHooks.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Properties/PCGExProperty_CollectionEntry.h"
#include "Properties/PCGExProperty_Range.h"

namespace PCGExCollectionsMediatorHooks
{
	using namespace PCGExMediator;

	FPCGExPropertyMediatorHooks MakeCollectionEntryHooks()
	{
		FPCGExPropertyMediatorHooks H;
		H.EncodeValue = [](const FPCGExProperty& P) -> TSharedPtr<FJsonValue>
		{
			const FPCGExCollectionEntryRef& Ref = static_cast<const FPCGExProperty_CollectionEntry&>(P).Value;
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("collection"), Ref.Collection.ToSoftObjectPath().ToString());
			Obj->SetNumberField(TEXT("entryId"), Ref.EntryId);
			return MakeShared<FJsonValueObject>(Obj);
		};
		H.DecodeValue = [](FPCGExProperty& P, const TSharedPtr<FJsonValue>& Json)
		{
			if (!Json.IsValid() || Json->Type != EJson::Object)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected { \"collection\", \"entryId\" }"));
				return false;
			}
			FPCGExCollectionEntryRef Temp = static_cast<FPCGExProperty_CollectionEntry&>(P).Value;
			const TSharedPtr<FJsonObject>& Obj = Json->AsObject();
			if (const TSharedPtr<FJsonValue> C = Obj->TryGetField(TEXT("collection")))
			{
				FPathScope S(TEXT("collection"));
				FSoftObjectPath Path;
				if (!Values::Decode<FSoftObjectPath>(C, Path)) { return false; }
				Temp.Collection = TSoftObjectPtr<UPCGExAssetCollection>(Path);
			}
			if (const TSharedPtr<FJsonValue> Id = Obj->TryGetField(TEXT("entryId")))
			{
				FPathScope S(TEXT("entryId"));
				int32 EntryId = 0;
				if (!Values::Decode<int32>(Id, EntryId)) { return false; }
				Temp.EntryId = EntryId;
			}
			static_cast<FPCGExProperty_CollectionEntry&>(P).Value = Temp;
			return true;
		};
		H.EncodeStructural = [](const FPCGExProperty& P, FJsonObject& Entry)
		{
			Entry.SetBoolField(TEXT("lockCollection"), static_cast<const FPCGExProperty_CollectionEntry&>(P).Value.bLockCollection);
		};
		H.DecodeStructural = [](FPCGExProperty& P, const FJsonObject& Entry)
		{
			if (const TSharedPtr<FJsonValue> Lock = Entry.TryGetField(TEXT("lockCollection")))
			{
				FPathScope S(TEXT("lockCollection"));
				bool bLock = true;
				if (!Values::Decode<bool>(Lock, bLock)) { return false; }
				static_cast<FPCGExProperty_CollectionEntry&>(P).Value.bLockCollection = bLock;
			}
			return true;
		};
		H.DescribeValue = [](const FPCGExProperty*)
		{
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("collection"), Values::DescribeShape(EPCGMetadataTypes::SoftObjectPath).ToSharedRef());
			Props->SetObjectField(TEXT("entryId"), Schema::Integer(TEXT("the picked entry's EntryId; 0 = none")));
			return TSharedPtr<FJsonObject>(Schema::Object(Props));
		};
		H.DescribeStructural = [](FJsonObject& Props)
		{
			Props.SetObjectField(TEXT("lockCollection"), Schema::Boolean(TEXT("overrides may only pick entries within the schema's collection")));
		};
		return H;
	}

	FPCGExPropertyMediatorHooks MakeRangeHooks()
	{
		FPCGExPropertyMediatorHooks H = PCGExPropertyMediator::MakeOutputTypeHooks();
		H.EncodeStructural = [](const FPCGExProperty& P, FJsonObject& Entry)
		{
			const FPCGExProperty_Range& R = static_cast<const FPCGExProperty_Range&>(P);
			Entry.SetNumberField(TEXT("min"), R.Min);
			Entry.SetNumberField(TEXT("max"), R.Max);
		};
		H.DecodeStructural = [](FPCGExProperty& P, const FJsonObject& Entry)
		{
			FPCGExProperty_Range& R = static_cast<FPCGExProperty_Range&>(P);
			double Min = R.Min;
			double Max = R.Max;
			if (const TSharedPtr<FJsonValue> V = Entry.TryGetField(TEXT("min")))
			{
				FPathScope S(TEXT("min"));
				if (!Values::Decode<double>(V, Min)) { return false; }
			}
			if (const TSharedPtr<FJsonValue> V = Entry.TryGetField(TEXT("max")))
			{
				FPathScope S(TEXT("max"));
				if (!Values::Decode<double>(V, Max)) { return false; }
			}
			R.Min = Min;
			R.Max = Max;
			return true;
		};
		H.DescribeStructural = [](FJsonObject& Props)
		{
			Props.SetObjectField(TEXT("min"), Schema::Number(TEXT("output value at the range's left end")));
			Props.SetObjectField(TEXT("max"), Schema::Number(TEXT("output value at the range's right end")));
		};
		return H;
	}
}

void PCGExCollectionsMediator::RegisterHooks()
{
	PCGExPropertyMediator::RegisterHooks(FPCGExProperty_CollectionEntry::StaticStruct(), PCGExCollectionsMediatorHooks::MakeCollectionEntryHooks());
	PCGExPropertyMediator::RegisterHooks(FPCGExProperty_Range::StaticStruct(), PCGExCollectionsMediatorHooks::MakeRangeHooks());
}

void PCGExCollectionsMediator::UnregisterHooks()
{
	PCGExPropertyMediator::UnregisterHooks(FPCGExProperty_CollectionEntry::StaticStruct());
	PCGExPropertyMediator::UnregisterHooks(FPCGExProperty_Range::StaticStruct());
}
