// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Properties/PCGExProperty_CollectionEntry.h"

#include "PCGExLog.h"
#include "PCGExCollectionsCommon.h"
#include "Core/PCGExAssetCollection.h"
#include "HAL/CriticalSection.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/ScopeLock.h"
#include "Types/PCGExTypeOps.h"

#pragma region FPCGExProperty_CollectionEntry

UPCGExAssetCollection* FPCGExProperty_CollectionEntry::FindCollection(const FPCGExCollectionEntryRef& InRef) const
{
	if (InRef.Collection.IsNull())
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<UPCGExAssetCollection>* Found = ResolvedCollections.Find(InRef.Collection.ToSoftObjectPath()))
	{
		return Found->Get();
	}

	// Only resolves what is already resident (same contract as FPickUnpacker::UnpackDataset).
	return PCGExHelpers::ResolveObject_AnyThreadTpl(InRef.Collection);
}

uint64 FPCGExProperty_CollectionEntry::ResolveHash(const FPCGExCollectionEntryRef& InRef, UPCGExAssetCollection* InCollection, const FName InPropertyName)
{
	const int32 RawIndex = InCollection ? InCollection->FindRawIndexByEntryId(InRef.EntryId) : INDEX_NONE;
	if (RawIndex != INDEX_NONE && RawIndex <= MAX_uint16)
	{
		return PCGExCollections::PickHash::Pack(InCollection->GetCollectionGUID(), static_cast<uint16>(RawIndex), -1);
	}

	// EntryId 0 is the documented "no pick" state (the picker shows "None") -- only a STALE
	// pick (a stored id that no longer resolves) is worth a warning.
	if (!InRef.Collection.IsNull() && InRef.EntryId != 0)
	{
		// Global dedup: shared source instances must stay stateless and clones are per-execution,
		// so per-instance bookkeeping either races or re-warns every run.
		static FCriticalSection WarnLock;
		static TSet<uint32> Warned;

		const uint32 Key = HashCombine(GetTypeHash(InRef.Collection.ToSoftObjectPath()), GetTypeHash(InPropertyName));
		bool bAlreadyWarned = false;
		{
			FScopeLock Lock(&WarnLock);
			Warned.Add(Key, &bAlreadyWarned);
		}
		if (!bAlreadyWarned)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Collection Entry property '%s': %s '%s' -- writing 0."),
			       *InPropertyName.ToString(),
			       InCollection ? TEXT("no entry with the stored EntryId in") : TEXT("collection not loaded:"),
			       *InRef.Collection.ToString());
		}
	}

	return 0;
}

uint64 FPCGExProperty_CollectionEntry::ResolveOwnHash() const
{
	return ResolveHash(Value, FindCollection(Value), PropertyName);
}

bool FPCGExProperty_CollectionEntry::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, const FName OutputName)
{
	// Default seeds from the authored pick, like every PCGEX_PROPERTY_IMPL type seeds from Value --
	// a disabled row cell falls back to the column's authored entry, not to unresolvable 0.
	OutputBuffer = OutputFacade->GetWritable<int64>(OutputName, static_cast<int64>(ResolveOwnHash()), true, PCGExData::EBufferInit::Inherit);
	return OutputBuffer.IsValid();
}

void FPCGExProperty_CollectionEntry::WriteOutput(const int32 PointIndex) const
{
	check(OutputBuffer);
	OutputBuffer->SetValue(PointIndex, static_cast<int64>(ResolveOwnHash()));
}

void FPCGExProperty_CollectionEntry::WriteOutputFrom(const int32 PointIndex, const FPCGExProperty* Source) const
{
	check(OutputBuffer);
	const FPCGExProperty_CollectionEntry* Typed = static_cast<const FPCGExProperty_CollectionEntry*>(Source);
	OutputBuffer->SetValue(PointIndex, static_cast<int64>(ResolveHash(Typed->Value, FindCollection(Typed->Value), Typed->PropertyName)));
}

void FPCGExProperty_CollectionEntry::BindWriteSources(TConstArrayView<const FPCGExProperty*> InSources)
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(InSources.Num() + 1);
	Paths.Add(Value.Collection.ToSoftObjectPath());
	for (const FPCGExProperty* Source : InSources)
	{
		if (Source && Source->GetTypeName() == GetTypeName())
		{
			Paths.Add(static_cast<const FPCGExProperty_CollectionEntry*>(Source)->Value.Collection.ToSoftObjectPath());
		}
	}

	TArray<UObject*> Collections;
	PCGExHelpers::ResolveObjects_AnyThread(Paths, Collections);
	for (int32 i = 0; i < Paths.Num(); i++)
	{
		if (!Paths[i].IsNull())
		{
			ResolvedCollections.Add(Paths[i], Cast<UPCGExAssetCollection>(Collections[i]));
		}
	}
}

void FPCGExProperty_CollectionEntry::CopyValueFrom(const FPCGExProperty* Source)
{
	Value = static_cast<const FPCGExProperty_CollectionEntry*>(Source)->Value;

	// Clone-only and single-threaded by contract: each new path resolves once, later writes read ResolvedCollections.
	const FSoftObjectPath& Path = Value.Collection.ToSoftObjectPath();
	if (Path.IsNull())
	{
		return;
	}

	UPCGExAssetCollection* Collection = nullptr;
	if (const TWeakObjectPtr<UPCGExAssetCollection>* Found = ResolvedCollections.Find(Path))
	{
		Collection = Found->Get();
	}
	else
	{
		Collection = PCGExHelpers::ResolveObject_AnyThreadTpl(Value.Collection);
		ResolvedCollections.Add(Path, Collection);
	}

	// The natural place to accumulate Map contributions.
	if (Collection)
	{
		TouchedCollections.AddUnique(Collection);
	}
}

bool FPCGExProperty_CollectionEntry::SyncStructuralFromSchema(const FPCGExProperty& Schema)
{
	// The lock is structural; the collection is structural only while locked (a pick is meaningless
	// against another collection, so a locked retarget resets it). Unlocked, the override's own
	// collection IS its value and survives schema edits.
	const FPCGExProperty_CollectionEntry& Typed = static_cast<const FPCGExProperty_CollectionEntry&>(Schema);
	bool bChanged = false;

	if (Value.bLockCollection != Typed.Value.bLockCollection)
	{
		Value.bLockCollection = Typed.Value.bLockCollection;
		bChanged = true;
	}

	if (Value.bLockCollection && Value.Collection != Typed.Value.Collection)
	{
		Value.Collection = Typed.Value.Collection;
		Value.EntryId = 0;
		bChanged = true;
	}

	return bChanged;
}

FName FPCGExProperty_CollectionEntry::GetDisplayTypeName() const
{
	return Value.Collection.IsNull() ? GetTypeName() : FName(*Value.Collection.GetAssetName());
}

FName FPCGExProperty_CollectionEntry::ResolveOutputAttributeName(const FName InEffectiveName) const
{
	return PCGExCollections::Labels::EntryIdxName(InEffectiveName);
}

FName FPCGExProperty_CollectionEntry::GetOutputSidecarPin() const
{
	return PCGExProperties::Labels::OutputMapLabel;
}

void FPCGExProperty_CollectionEntry::WriteOutputSidecar(UPCGMetadata* InSidecar) const
{
	// Touched collections (clone) plus the authored one (source instance path), each expanded to its
	// flat host set, in ONE AppendMapRows call so the dedupe scan runs once.
	TArray<const UPCGExAssetCollection*> Collections;
	auto Expand = [&Collections](UPCGExAssetCollection* Collection)
	{
		if (!Collection)
		{
			return;
		}
		for (const TObjectPtr<UPCGExAssetCollection>& Host : Collection->GetFlatHosts())
		{
			Collections.AddUnique(Host.Get());
		}
	};

	for (const TObjectPtr<UPCGExAssetCollection>& Touched : TouchedCollections)
	{
		Expand(Touched.Get());
	}
	Expand(FindCollection(Value));

	PCGExCollections::FPickPacker::AppendMapRows(InSidecar, Collections);
}

FPCGMetadataAttributeBase* FPCGExProperty_CollectionEntry::CreateMetadataAttribute(UPCGMetadata* Metadata, const FName AttributeName) const
{
	// Default seeds from the authored pick, like every PCGEX_PROPERTY_IMPL type seeds from Value.
	return Metadata->CreateAttribute<int64>(AttributeName, static_cast<int64>(ResolveOwnHash()), true, true);
}

void FPCGExProperty_CollectionEntry::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, const int64 EntryKey) const
{
	// Callable on SHARED source instances (Tuple): only reads, and their ResolvedCollections is never written.
	static_cast<FPCGMetadataAttribute<int64>*>(Attribute)->SetValue(EntryKey, static_cast<int64>(ResolveOwnHash()));
}

bool FPCGExProperty_CollectionEntry::TryWriteValue(const EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	// Source-instance path (@Data writes): only reads, as in WriteMetadataValue.
	const int64 Projected = static_cast<int64>(ResolveOwnHash());
	PCGExTypeOps::FConversionTable::Convert(EPCGMetadataTypes::Integer64, &Projected, TargetType, OutBuffer);
	return true;
}

bool FPCGExProperty_CollectionEntry::TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer)
{
	// A pick hash is not invertible into (collection, EntryId).
	return false;
}

void FPCGExProperty_CollectionEntry::GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	if (!Value.Collection.IsNull())
	{
		OutPaths.Add(Value.Collection.ToSoftObjectPath());
	}
}

void FPCGExProperty_CollectionEntry::GatherOutputDependencies(TSet<FSoftObjectPath>& OutPaths) const
{
	GatherSoftObjectPaths(OutPaths);
}

#pragma endregion
