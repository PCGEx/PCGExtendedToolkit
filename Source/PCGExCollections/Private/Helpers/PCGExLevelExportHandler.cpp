// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExLevelExportHandler.h"

#include "PCGExLog.h"
#include "Core/PCGExAssetCollection.h"

#if WITH_EDITOR

#pragma region FPCGExExportSlotWriter

FPCGExExportSlotWriter::FPCGExExportSlotWriter(const FPCGExExportSlotDesc& InDesc)
	: Desc(InDesc)
{
}

int32 FPCGExExportSlotWriter::FindOrAddEntry(
	const uint32 IdentityHash,
	TFunctionRef<bool(int32 LocalIndex, const FPCGExAssetCollectionEntry&)> Equals,
	TFunctionRef<void(FPCGExAssetCollectionEntry&)> Init,
	const int32 WeightIncrement)
{
	TArray<int32>& Bucket = Buckets.FindOrAdd(IdentityHash);
	for (const int32 LocalIndex : Bucket)
	{
		if (Equals(LocalIndex, GetEntry(LocalIndex)))
		{
			GetEntry(LocalIndex).Weight += WeightIncrement;
			return LocalIndex;
		}
	}

	const int32 LocalIndex = Entries.Num();
	FInstancedStruct& Added = Entries.AddDefaulted_GetRef();
	Added.InitializeAs(Desc.EntryStruct);

	FPCGExAssetCollectionEntry& Entry = *Added.GetMutablePtr<FPCGExAssetCollectionEntry>();
	Init(Entry);
	// Weight is the pick count, never the struct default.
	Entry.Weight = WeightIncrement;

	Bucket.Add(LocalIndex);
	return LocalIndex;
}

FPCGExAssetCollectionEntry& FPCGExExportSlotWriter::GetEntry(const int32 LocalIndex)
{
	return *Entries[LocalIndex].GetMutablePtr<FPCGExAssetCollectionEntry>();
}

const FPCGExAssetCollectionEntry& FPCGExExportSlotWriter::GetEntry(const int32 LocalIndex) const
{
	return *Entries[LocalIndex].GetPtr<FPCGExAssetCollectionEntry>();
}

FPCGExExportItem& FPCGExExportSlotWriter::AddItem(const FTransform& InTransform, const FVector& InBoundsMin, const FVector& InBoundsMax, AActor* InSourceActor, const int32 InLocalEntryIndex, const int16 InSecondaryIndex)
{
	FPCGExExportItem& Item = Items.AddDefaulted_GetRef();
	Item.Transform = InTransform;
	Item.BoundsMin = InBoundsMin;
	Item.BoundsMax = InBoundsMax;
	Item.SourceActor = InSourceActor;
	Item.LocalEntryIndex = InLocalEntryIndex;
	Item.SecondaryIndex = InSecondaryIndex;
	return Item;
}

#pragma endregion

#pragma region FHandlerRegistry

namespace PCGExLevelExport
{
	FHandlerRegistry& FHandlerRegistry::Get()
	{
		static FHandlerRegistry Instance;
		return Instance;
	}

	bool FHandlerRegistry::Register(const TSubclassOf<UPCGExLevelExportHandler> HandlerClass)
	{
		if (!HandlerClass || HandlerClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Level export handler registration refused: null or abstract class."));
			return false;
		}

		const UPCGExLevelExportHandler* CDO = HandlerClass->GetDefaultObject<UPCGExLevelExportHandler>();
		FHandlerRegistration Registration;
		Registration.Desc = CDO->GetSlotDesc();
		Registration.Policy = CDO->MakePolicy();
		Registration.HandlerClass = HandlerClass;
		Registration.Priority = CDO->GetPriority();

		if (!Registration.Desc.IsValid() || !Registration.Policy)
		{
			UE_LOG(LogPCGEx, Error, TEXT("Level export handler '%s' refused: incomplete slot descriptor or null policy."), *HandlerClass->GetName());
			return false;
		}

		if (!Registration.Desc.EntryStruct->IsChildOf(FPCGExAssetCollectionEntry::StaticStruct()))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Level export handler '%s' refused: entry struct '%s' is not an asset collection entry."), *HandlerClass->GetName(), *Registration.Desc.EntryStruct->GetName());
			return false;
		}

		if (!Registration.Desc.CollectionClass->IsChildOf<UPCGExAssetCollection>() || Registration.Desc.CollectionClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Level export handler '%s' refused: '%s' is not a concrete asset collection class."), *HandlerClass->GetName(), *Registration.Desc.CollectionClass->GetName());
			return false;
		}

		FWriteScopeLock WriteLock(Lock);
		if (const FHandlerRegistration* Existing = Registrations.Find(Registration.Desc.SlotId))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Level export slot '%s' is already registered by '%s'; '%s' refused."),
			       *Registration.Desc.SlotId.ToString(), *GetNameSafe(Existing->HandlerClass), *HandlerClass->GetName());
			return false;
		}
		Registrations.Add(Registration.Desc.SlotId, MoveTemp(Registration));
		return true;
	}

	void FHandlerRegistry::Unregister(const FName SlotId)
	{
		FWriteScopeLock WriteLock(Lock);
		Registrations.Remove(SlotId);
	}

	bool FHandlerRegistry::FindSlot(const FName SlotId, FPCGExExportSlotDesc& OutDesc) const
	{
		FReadScopeLock ReadLock(Lock);
		if (const FHandlerRegistration* Found = Registrations.Find(SlotId))
		{
			OutDesc = Found->Desc;
			return true;
		}
		return false;
	}

	TSharedPtr<const IPCGExExportSlotPolicy> FHandlerRegistry::GetPolicy(const FName SlotId) const
	{
		FReadScopeLock ReadLock(Lock);
		const FHandlerRegistration* Found = Registrations.Find(SlotId);
		return Found ? Found->Policy : nullptr;
	}

	void FHandlerRegistry::GetRegistrations(TArray<FHandlerRegistration>& OutRegistrations) const
	{
		{
			FReadScopeLock ReadLock(Lock);
			OutRegistrations.Reserve(OutRegistrations.Num() + Registrations.Num());
			for (const TPair<FName, FHandlerRegistration>& Pair : Registrations)
			{
				OutRegistrations.Add(Pair.Value);
			}
		}
		// Priority, then slot id: a stable order across sessions regardless of registration order.
		OutRegistrations.Sort([](const FHandlerRegistration& A, const FHandlerRegistration& B)
		{
			if (A.Priority != B.Priority)
			{
				return A.Priority < B.Priority;
			}
			return A.Desc.SlotId.LexicalLess(B.Desc.SlotId);
		});
	}

	void FHandlerRegistry::GetSlots(TArray<FPCGExExportSlotDesc>& OutSlots) const
	{
		TArray<FHandlerRegistration> Copies;
		GetRegistrations(Copies);
		OutSlots.Reserve(OutSlots.Num() + Copies.Num());
		for (const FHandlerRegistration& Registration : Copies)
		{
			OutSlots.Add(Registration.Desc);
		}
	}
}

#pragma endregion

#endif // WITH_EDITOR
