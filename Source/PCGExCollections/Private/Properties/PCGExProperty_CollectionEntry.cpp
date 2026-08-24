// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Properties/PCGExProperty_CollectionEntry.h"

#include "PCGExLog.h"
#include "PCGExCollectionsCommon.h"
#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Metadata/PCGMetadata.h"
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

uint64 FPCGExProperty_CollectionEntry::ResolveOwnHash() const
{
	UPCGExAssetCollection* Collection = nullptr;
	const uint64 Hash = ResolveHash(Value, Collection);

	if (Collection)
	{
		return Hash;
	}

	if (!Value.Collection.IsNull())
	{
		bool bAlreadyWarned = false;
		WarnedCollections.Add(Value.Collection.ToSoftObjectPath(), &bAlreadyWarned);
		if (!bAlreadyWarned)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Collection Entry property '%s': %s '%s' -- writing 0."),
			       *PropertyName.ToString(),
			       Value.Collection.Get() ? TEXT("no entry with the stored EntryId in") : TEXT("collection not loaded:"),
			       *Value.Collection.ToString());
		}
	}

	return 0;
}

bool FPCGExProperty_CollectionEntry::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, const FName OutputName)
{
	OutputBuffer = OutputFacade->GetWritable<int64>(OutputName, 0, true, PCGExData::EBufferInit::Inherit);
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
	OutputBuffer->SetValue(PointIndex, static_cast<int64>(ResolveHash(static_cast<const FPCGExProperty_CollectionEntry*>(Source)->Value, Collection)));
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
	return Metadata->CreateAttribute<int64>(AttributeName, 0, true, true);
}

void FPCGExProperty_CollectionEntry::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, const int64 EntryKey) const
{
	// Callable on SHARED source instances (Tuple) -- resolve without touching any state.
	UPCGExAssetCollection* Collection = nullptr;
	static_cast<FPCGMetadataAttribute<int64>*>(Attribute)->SetValue(EntryKey, static_cast<int64>(ResolveHash(Value, Collection)));
}

bool FPCGExProperty_CollectionEntry::TryWriteValue(const EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	// Source-instance path (@Data writes): resolve without touching clone bookkeeping.
	UPCGExAssetCollection* Collection = nullptr;
	const int64 Projected = static_cast<int64>(ResolveHash(Value, Collection));
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
