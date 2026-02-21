// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExAssetCollection.h"

#include "PCGExProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Algo/RemoveIf.h"
#include "Engine/World.h"
#include "Helpers/PCGExArrayHelpers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "PropertyEditorModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

bool FPCGExEntryAccessResult::IsType(PCGExAssetCollection::FTypeId TypeId) const
{
	return Entry ? Entry->IsType(TypeId) : false;
}

#pragma region FPCGExAssetStagingData

bool FPCGExAssetStagingData::FindSocket(FName InName, const FPCGExSocket*& OutSocket) const
{
	for (const FPCGExSocket& Socket : Sockets)
	{
		if (Socket.SocketName == InName)
		{
			OutSocket = &Socket;
			return true;
		}
	}
	return false;
}

bool FPCGExAssetStagingData::FindSocket(FName InName, const FString& Tag, const FPCGExSocket*& OutSocket) const
{
	for (const FPCGExSocket& Socket : Sockets)
	{
		if (Socket.SocketName == InName && Socket.Tag == Tag)
		{
			OutSocket = &Socket;
			return true;
		}
	}
	return false;
}

#pragma endregion

namespace PCGExAssetCollection
{
#pragma region FMicroCache

	int32 FMicroCache::GetPick(int32 Index, EPCGExIndexPickMode PickMode) const
	{
		switch (PickMode)
		{
		default:
		case EPCGExIndexPickMode::Ascending: return GetPickAscending(Index);
		case EPCGExIndexPickMode::Descending: return GetPickDescending(Index);
		case EPCGExIndexPickMode::WeightAscending: return GetPickWeightAscending(Index);
		case EPCGExIndexPickMode::WeightDescending: return GetPickWeightDescending(Index);
		}
	}

	int32 FMicroCache::GetPickAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Index : -1;
	}

	int32 FMicroCache::GetPickDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? (Order.Num() - 1) - Index : -1;
	}

	int32 FMicroCache::GetPickWeightAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Order[Index] : -1;
	}

	int32 FMicroCache::GetPickWeightDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Order[(Order.Num() - 1) - Index] : -1;
	}

	int32 FMicroCache::GetPickRandom(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}
		return Order[FRandomStream(Seed).RandRange(0, Order.Num() - 1)];
	}

	int32 FMicroCache::GetPickRandomWeighted(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}

		const int32 Threshold = FRandomStream(Seed).RandRange(0, static_cast<int32>(WeightSum) - 1);
		int32 Pick = 0;
		while (Pick < Weights.Num() && Weights[Pick] <= Threshold) { Pick++; }
		return Order[FMath::Min(Pick, Order.Num() - 1)];
	}

	// Builds cumulative weight array for weighted random picking.
	// Weights are sorted ascending, then converted to cumulative sums.
	// GetPickRandomWeighted scans for the first cumulative weight > threshold.
	void FMicroCache::BuildFromWeights(TConstArrayView<int32> InWeights)
	{
		const int32 NumEntries = InWeights.Num();

		Weights.SetNumUninitialized(NumEntries);
		for (int32 i = 0; i < NumEntries; i++)
		{
			Weights[i] = InWeights[i] + 1; // +1 to ensure non-zero (Weight=0 entries are already excluded by Validate)
		}
		
		PCGExArrayHelpers::ArrayOfIndices(Order, NumEntries);

		Order.Sort([this](int32 A, int32 B) { return Weights[A] < Weights[B]; });
		Weights.Sort([](int32 A, int32 B) { return A < B; });

		WeightSum = 0;
		for (int32 i = 0; i < NumEntries; i++)
		{
			WeightSum += Weights[i];
			Weights[i] = static_cast<int32>(WeightSum);
		}
	}

#pragma endregion

#pragma region FCategory

	int32 FCategory::GetPick(int32 Index, EPCGExIndexPickMode PickMode) const
	{
		switch (PickMode)
		{
		default:
		case EPCGExIndexPickMode::Ascending: return GetPickAscending(Index);
		case EPCGExIndexPickMode::Descending: return GetPickDescending(Index);
		case EPCGExIndexPickMode::WeightAscending: return GetPickWeightAscending(Index);
		case EPCGExIndexPickMode::WeightDescending: return GetPickWeightDescending(Index);
		}
	}

	int32 FCategory::GetPickAscending(int32 Index) const
	{
		return Indices.IsValidIndex(Index) ? Indices[Index] : -1;
	}

	int32 FCategory::GetPickDescending(int32 Index) const
	{
		return Indices.IsValidIndex(Index) ? Indices[(Indices.Num() - 1) - Index] : -1;
	}

	int32 FCategory::GetPickWeightAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Indices[Order[Index]] : -1;
	}

	int32 FCategory::GetPickWeightDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Indices[Order[(Order.Num() - 1) - Index]] : -1;
	}

	int32 FCategory::GetPickRandom(int32 Seed) const
	{
		if (Order.IsEmpty()) { return -1; }
		return Indices[Order[FRandomStream(Seed).RandRange(0, Order.Num() - 1)]];
	}

	int32 FCategory::GetPickRandomWeighted(int32 Seed) const
	{
		if (Order.IsEmpty()) { return -1; }
		const int32 Threshold = FRandomStream(Seed).RandRange(0, static_cast<int32>(WeightSum) - 1);
		int32 Pick = 0;
		while (Pick < Weights.Num() && Weights[Pick] <= Threshold) { Pick++; }
		return Indices[Order[FMath::Min(Pick, Order.Num() - 1)]];
	}

	void FCategory::Reserve(int32 InNum)
	{
		Indices.Reserve(InNum);
		Weights.Reserve(InNum);
		Order.Reserve(InNum);
	}

	void FCategory::Shrink()
	{
		Indices.Shrink();
		Weights.Shrink();
		Order.Shrink();
	}

	void FCategory::RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry)
	{
		Entries.Add(InEntry);
		const_cast<FPCGExAssetCollectionEntry*>(InEntry)->BuildMicroCache();
		Indices.Add(Index);
		Weights.Add(InEntry->Weight + 1);
	}

	void FCategory::Compile()
	{
		Shrink();

		const int32 NumEntries = Indices.Num();
		PCGExArrayHelpers::ArrayOfIndices(Order, NumEntries);

		Order.Sort([this](int32 A, int32 B) { return Weights[A] < Weights[B]; });
		Weights.Sort([](int32 A, int32 B) { return A < B; });

		WeightSum = 0;
		for (int32 i = 0; i < NumEntries; i++)
		{
			WeightSum += Weights[i];
			Weights[i] = static_cast<int32>(WeightSum);
		}
	}

#pragma endregion

#pragma region FCache

	FCache::FCache()
	{
		Main = MakeShared<FCategory>(NAME_None);
	}

	// Every valid entry goes into Main. Additionally, entries with a non-None Category
	// are registered to a named sub-category (created on first encounter).
	void FCache::RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry)
	{
		Main->RegisterEntry(Index, InEntry);
		if (const TSharedPtr<FCategory>* CategoryPtr = Categories.Find(InEntry->Category); !CategoryPtr)
		{
			TSharedPtr<FCategory> Category = MakeShared<FCategory>(InEntry->Category);
			Categories.Add(InEntry->Category, Category);
			Category->RegisterEntry(Index, InEntry);
		}
		else
		{
			(*CategoryPtr)->RegisterEntry(Index, InEntry);
		}
	}

	void FCache::Compile()
	{
		Main->Compile();
		for (const auto& Pair : Categories) { Pair.Value->Compile(); }
	}
#pragma endregion
}

#pragma region FPCGExAssetCollectionEntry

const FPCGExFittingVariations& FPCGExAssetCollectionEntry::GetVariations(const UPCGExAssetCollection* ParentCollection) const
{
	if (VariationMode == EPCGExEntryVariationMode::Global || ParentCollection->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule)
	{
		return ParentCollection->GlobalVariations;
	}
	return Variations;
}

double FPCGExAssetCollectionEntry::GetGrammarSize(const UPCGExAssetCollection* Host) const
{
	if (!bIsSubCollection)
	{
		if (GrammarSource == EPCGExEntryVariationMode::Local) { return AssetGrammar.GetSize(Staging.Bounds); }
		return Host->GlobalAssetGrammar.GetSize(Staging.Bounds);
	}

	if (InternalSubCollection)
	{
		if (SubGrammarMode == EPCGExGrammarSubCollectionMode::Flatten) { return 0; }
		if (SubGrammarMode == EPCGExGrammarSubCollectionMode::Inherit) { return InternalSubCollection->CollectionGrammar.GetSize(InternalSubCollection); }
		if (SubGrammarMode == EPCGExGrammarSubCollectionMode::Override) { return CollectionGrammar.GetSize(InternalSubCollection); }
	}

	return 0;
}

double FPCGExAssetCollectionEntry::GetGrammarSize(const UPCGExAssetCollection* Host, TMap<const FPCGExAssetCollectionEntry*, double>* SizeCache) const
{
	if (!SizeCache) { return GetGrammarSize(Host); }
	if (double* CachedSize = SizeCache->Find(this)) { return *CachedSize; }
	return SizeCache->Add(this, GetGrammarSize(Host));
}

bool FPCGExAssetCollectionEntry::FixModuleInfos(const UPCGExAssetCollection* Host, FPCGSubdivisionSubmodule& OutModule, TMap<const FPCGExAssetCollectionEntry*, double>* SizeCache) const
{
	if (!bIsSubCollection)
	{
		if (GrammarSource == EPCGExEntryVariationMode::Local) { AssetGrammar.Fix(Staging.Bounds, OutModule); }
		else { Host->GlobalAssetGrammar.Fix(Staging.Bounds, OutModule); }
	}

	if (InternalSubCollection)
	{
		if (SubGrammarMode == EPCGExGrammarSubCollectionMode::Inherit) { InternalSubCollection->CollectionGrammar.Fix(InternalSubCollection, OutModule); }
		else if (SubGrammarMode == EPCGExGrammarSubCollectionMode::Override) { CollectionGrammar.Fix(InternalSubCollection, OutModule); }
		else { return false; }
	}

	return true;
}

#if WITH_EDITOR
void FPCGExAssetCollectionEntry::EDITOR_Sanitize()
{
	// Base implementation - override in derived classes
}
#endif

bool FPCGExAssetCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (Weight <= 0) { return false; }

	if (bIsSubCollection)
	{
		if (!InternalSubCollection) { return false; }
		InternalSubCollection->LoadCache();
	}
	return true;
}

void FPCGExAssetCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	Staging.InternalIndex = InInternalIndex;

	if (bIsSubCollection)
	{
		Staging.Bounds = FBox(ForceInit);
		if (InternalSubCollection)
		{
			Staging.Path = FSoftObjectPath(InternalSubCollection.GetPathName());
			if (bRecursive) { InternalSubCollection->RebuildStagingData(true); }
		}
		else
		{
			Staging.Path = FSoftObjectPath{};
		}
	}
}

void FPCGExAssetCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	Staging.Path = InPath;
}

void FPCGExAssetCollectionEntry::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	OutPaths.Emplace(Staging.Path);
}

void FPCGExAssetCollectionEntry::BuildMicroCache()
{
	MicroCache = nullptr;
}

void FPCGExAssetCollectionEntry::ClearManagedSockets()
{
	Staging.Sockets.SetNum(Algo::RemoveIf(Staging.Sockets, [](const FPCGExSocket& Socket) { return Socket.bManaged; }));
}

#pragma endregion

// All API methods follow the same pattern: pick from cache → if subcollection, recurse
// into it (using weighted random for the nested pick) → otherwise return entry + host.
// Tag-inheriting variants accumulate tags from the hierarchy as they recurse.
#pragma region API

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryAt(int32 Index) const
{
	FPCGExEntryAccessResult Result;

	const int32 Pick = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, EPCGExIndexPickMode::Ascending);
	if (const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(Pick))
	{
		Result.Entry = Entry;
		Result.Host = this;
	}
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRaw(int32 RawIndex) const
{
	FPCGExEntryAccessResult Result;

	if (const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(RawIndex))
	{
		Result.Entry = Entry;
		Result.Host = this;
	}
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, PickMode);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRandom(int32 Seed) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandom(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryRandom(Seed * 2);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryWeightedRandom(int32 Seed) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandomWeighted(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed * 2);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

// With tag inheritance

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryAt(int32 Index, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, EPCGExIndexPickMode::Ascending);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
	}
	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRaw(int32 RawIndex, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(RawIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
	}
	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, PickMode);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandom(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryRandom(Seed * 2, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryWeightedRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandomWeighted(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed * 2, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

#pragma endregion

#pragma region Cache

// Thread-safe lazy cache initialization. First checks under read lock (fast path),
// then builds under write lock (inside BuildCacheFromEntries) if needed.
PCGExAssetCollection::FCache* UPCGExAssetCollection::LoadCache()
{
	{
		FReadScopeLock ReadScopeLock(CacheLock);
		if (bCacheNeedsRebuild) { InvalidateCache(); }
		if (Cache) { return Cache.Get(); }
	}

	BuildCache();
	return Cache.Get();
}

void UPCGExAssetCollection::InvalidateCache()
{
	Cache.Reset();
	bCacheNeedsRebuild = true;
}

void UPCGExAssetCollection::BuildCache()
{
	bCacheNeedsRebuild = false;
	// Per-class implementation calls BuildCacheFromEntries(Entries)
}

#pragma endregion

void UPCGExAssetCollection::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// Duplicates get a new identity; PIE copies keep the same GUID
	if (!bDuplicateForPIE) { CollectionGUID = GenerateNewGUID(); }

#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

void UPCGExAssetCollection::PostEditImport()
{
	Super::PostEditImport();

	// Paste/import gets a new identity
	CollectionGUID = GenerateNewGUID();

#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

void UPCGExAssetCollection::BeginDestroy()
{
	InvalidateCache();
	Super::BeginDestroy();
}

void UPCGExAssetCollection::RebuildStagingData(bool bRecursive)
{
	ForEachEntry([this, bRecursive](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		InEntry->UpdateStaging(this, i, bRecursive);
	});
	InvalidateCache();
}

void UPCGExAssetCollection::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	Context->EDITOR_TrackPath(this);
	ForEachEntry([Context](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (!InEntry->bIsSubCollection) { return; }
		if (const UPCGExAssetCollection* SubCollection = InEntry->GetSubCollectionPtr())
		{
			SubCollection->EDITOR_RegisterTrackingKeys(Context);
		}
	});
}

bool UPCGExAssetCollection::HasCircularDependency(const UPCGExAssetCollection* OtherCollection) const
{
	if (!OtherCollection) { return false; }
	if (OtherCollection == this) { return true; }

	TSet<const UPCGExAssetCollection*> References;
	return OtherCollection->HasCircularDependency(References);
}

bool UPCGExAssetCollection::HasCircularDependency(TSet<const UPCGExAssetCollection*>& InReferences) const
{
	bool bCircularDependency = false;
	InReferences.Add(this, &bCircularDependency);

	if (bCircularDependency) { return true; }

	ForEachEntry([&](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (bCircularDependency) { return; }
		if (const UPCGExAssetCollection* Other = InEntry->GetSubCollectionPtr())
		{
			bCircularDependency = Other->HasCircularDependency(InReferences);
		}
	});

	return bCircularDependency;
}

void UPCGExAssetCollection::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths, PCGExAssetCollection::ELoadingFlags Flags) const
{
	const bool bCollectionOnly = Flags == PCGExAssetCollection::ELoadingFlags::RecursiveCollectionsOnly;
	const bool bRecursive = bCollectionOnly || Flags == PCGExAssetCollection::ELoadingFlags::Recursive;

	ForEachEntry([&](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (InEntry->bIsSubCollection)
		{
			if (bRecursive || bCollectionOnly)
			{
				if (InEntry->InternalSubCollection)
				{
					InEntry->InternalSubCollection->GetAssetPaths(OutPaths, Flags);
				}
			}
			return;
		}
		if (bCollectionOnly) { return; }

		InEntry->GetAssetPaths(OutPaths);
	});
}

#if WITH_EDITOR
void UPCGExAssetCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bNeedsSync = false;
	bool bNeedsUIRefresh = false;

	if (PropertyChangedEvent.MemberProperty)
	{
		FName PropName = PropertyChangedEvent.MemberProperty->GetFName();
		EPropertyChangeType::Type ChangeType = PropertyChangedEvent.ChangeType;

		// Check for ANY changes in CollectionProperties
		if (PropName == GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties))
		{
			bNeedsSync = true;
			bNeedsUIRefresh = true;
		}
		// Also catch changes to schema array elements (add/remove/reorder/rename/type change)
		else if (PropertyChangedEvent.MemberProperty->GetOwnerStruct() == FPCGExPropertySchema::StaticStruct() ||
		         PropertyChangedEvent.MemberProperty->GetOwnerStruct() == FPCGExPropertySchemaCollection::StaticStruct())
		{
			bNeedsSync = true;
			bNeedsUIRefresh = true;
		}
	}

	// Early return if no property-related changes needed
	if (!bNeedsSync && !bNeedsUIRefresh)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		ForEachEntry([this](FPCGExAssetCollectionEntry* InEntry, int32 i)
		{
			const UPCGExAssetCollection* Other = InEntry->GetSubCollectionPtr();
			if (Other && HasCircularDependency(Other))
			{
				UE_LOG(LogTemp, Error, TEXT("Prevented circular dependency trying to nest \"%s\" inside \"%s\""), *GetNameSafe(Other), *GetNameSafe(this));
				InEntry->ClearSubCollection();
				(void)MarkPackageDirty();
			}
		});

		EDITOR_SetDirty();

		if (bAutoRebuildStaging)
		{
			EDITOR_RebuildStagingData();
		}

		return;
	}

	// Sync and rebuild if needed
	if (bNeedsSync)
	{
		RebuildPropertyRegistry();
		SyncPropertyOverridesToEntries();
	}

	(void)MarkPackageDirty();

	Super::PostEditChangeProperty(PropertyChangedEvent);

	#if WITH_EDITOR
	// Force all details panels showing this object to rebuild
	// This ensures nested PropertyOverrides customizations detect the schema changes
	if (bNeedsSync)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}
	#endif

	ForEachEntry([this](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		const UPCGExAssetCollection* Other = InEntry->GetSubCollectionPtr();
		if (Other && HasCircularDependency(Other))
		{
			UE_LOG(LogTemp, Error, TEXT("Prevented circular dependency trying to nest \"%s\" inside \"%s\""), *GetNameSafe(Other), *GetNameSafe(this));
			InEntry->ClearSubCollection();
			(void)MarkPackageDirty();
		}
	});

	EDITOR_SetDirty();

	if (bAutoRebuildStaging)
	{
		EDITOR_RebuildStagingData();
	}
}

void UPCGExAssetCollection::SyncPropertyOverridesToEntries()
{
	// Sync schema to all entry overrides using shared utility
	CollectionProperties.SyncAllSchemas();
	TArray<FInstancedStruct> Schema = CollectionProperties.BuildSchema();
	ForEachEntry([&Schema](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		InEntry->PropertyOverrides.SyncToSchema(Schema);
	});
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData()
{
	Modify(true);
	InvalidateCache();
	EDITOR_SanitizeAndRebuildStagingData(false);
	(void)MarkPackageDirty();
	FCoreUObjectDelegates::BroadcastOnObjectModified(this);
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData_Recursive()
{
	Modify(true);
	InvalidateCache();
	EDITOR_SanitizeAndRebuildStagingData(true);
	(void)MarkPackageDirty();
	FCoreUObjectDelegates::BroadcastOnObjectModified(this);
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData_Project()
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(AssetData.GetAsset()))
		{
			Collection->EDITOR_RebuildStagingData();
		}
	}
}

void UPCGExAssetCollection::EDITOR_SanitizeAndRebuildStagingData(bool bRecursive)
{
	ForEachEntry([this, bRecursive](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		InEntry->EDITOR_Sanitize();
		InEntry->UpdateStaging(this, i, bRecursive);
	});
}

void UPCGExAssetCollection::EDITOR_AddBrowserSelectionTyped(const TArray<FAssetData>& InAssetData)
{
	Modify(true);
	EDITOR_AddBrowserSelectionInternal(InAssetData);
	(void)MarkPackageDirty();
	FCoreUObjectDelegates::BroadcastOnObjectModified(this);
}

void UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	// Override in derived classes
}
#endif
