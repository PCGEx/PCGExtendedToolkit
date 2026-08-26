// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Properties/PCGExProperty_CollectionEntry.h"

#include "PCGExLog.h"
#include "PCGExCollectionsCommon.h"
#include "Core/PCGExAssetCollection.h"
#include "HAL/CriticalSection.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/ScopeLock.h"
#include "Types/PCGExTypeOps.h"

#pragma region FPCGExProperty_CollectionEntry

uint64 FPCGExProperty_CollectionEntry::ResolveHash(const FPCGExCollectionEntryRef& InRef, UPCGExAssetCollection*& OutCollection)
{
	// Get() only dereferences an already-resident object (same contract as FPickUnpacker::UnpackDataset).
	UPCGExAssetCollection* Collection = InRef.Collection.Get();
	if (!Collection)
	{
		return 0;
	}

	const int32 RawIndex = Collection->FindRawIndexByEntryId(InRef.EntryId);
	if (RawIndex == INDEX_NONE || RawIndex > MAX_uint16)
	{
		return 0;
	}

	OutCollection = Collection;
	return PCGExCollections::PickHash::Pack(Collection->GetCollectionGUID(), static_cast<uint16>(RawIndex), -1);
}

uint64 FPCGExProperty_CollectionEntry::ResolveHashWarned(const FPCGExCollectionEntryRef& InRef, const FName InPropertyName, UPCGExAssetCollection*& OutCollection)
{
	const uint64 Hash = ResolveHash(InRef, OutCollection);

	if (!OutCollection && !InRef.Collection.IsNull())
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
			       InRef.Collection.Get() ? TEXT("no entry with the stored EntryId in") : TEXT("collection not loaded:"),
			       *InRef.Collection.ToString());
		}
	}

	return Hash;
}

uint64 FPCGExProperty_CollectionEntry::ResolveOwnHash() const
{
	UPCGExAssetCollection* Collection = nullptr;
	return ResolveHashWarned(Value, PropertyName, Collection);
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
	UPCGExAssetCollection* Collection = nullptr;
	const FPCGExProperty_CollectionEntry* Typed = static_cast<const FPCGExProperty_CollectionEntry*>(Source);
	OutputBuffer->SetValue(PointIndex, static_cast<int64>(ResolveHashWarned(Typed->Value, Typed->PropertyName, Collection)));
}

void FPCGExProperty_CollectionEntry::CopyValueFrom(const FPCGExProperty* Source)
{
	Value = static_cast<const FPCGExProperty_CollectionEntry*>(Source)->Value;

	// Clone-only path by contract -- the natural place to accumulate Map contributions.
	if (UPCGExAssetCollection* Collection = Value.Collection.Get())
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
	Expand(Value.Collection.Get());

	PCGExCollections::FPickPacker::AppendMapRows(InSidecar, Collections);
}

FPCGMetadataAttributeBase* FPCGExProperty_CollectionEntry::CreateMetadataAttribute(UPCGMetadata* Metadata, const FName AttributeName) const
{
	// Default seeds from the authored pick, like every PCGEX_PROPERTY_IMPL type seeds from Value.
	UPCGExAssetCollection* Collection = nullptr;
	return Metadata->CreateAttribute<int64>(AttributeName, static_cast<int64>(ResolveHashWarned(Value, PropertyName, Collection)), true, true);
}

void FPCGExProperty_CollectionEntry::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, const int64 EntryKey) const
{
	// Callable on SHARED source instances (Tuple) -- ResolveHashWarned touches no instance state.
	UPCGExAssetCollection* Collection = nullptr;
	static_cast<FPCGMetadataAttribute<int64>*>(Attribute)->SetValue(EntryKey, static_cast<int64>(ResolveHashWarned(Value, PropertyName, Collection)));
}

bool FPCGExProperty_CollectionEntry::TryWriteValue(const EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	// Source-instance path (@Data writes) -- ResolveHashWarned touches no instance state.
	UPCGExAssetCollection* Collection = nullptr;
	const int64 Projected = static_cast<int64>(ResolveHashWarned(Value, PropertyName, Collection));
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
