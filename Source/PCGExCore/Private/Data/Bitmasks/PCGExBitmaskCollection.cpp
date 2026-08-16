// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Bitmasks/PCGExBitmaskCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif

#include "Core/PCGExContext.h"

void FPCGExBitmaskCollectionEntry::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	Bitmask.EDITOR_RegisterTrackingKeys(Context);
}

void FPCGExBitmaskCollectionEntry::RebuildCache()
{
	CachedBitmask.Identifier = Identifier;
	CachedBitmask.Bitmask = Bitmask.Get();
	GetDirection(CachedBitmask.Direction);
}

bool PCGExBitmaskCollection::FCache::TryGetBitmask(const FName Identifier, int64& OutBitmask) const
{
	const int32* Index = BitmaskMap.Find(Identifier);
	if (!Index)
	{
		if (!Identifier.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("Bitmask \"%s\" doesn't exists."), *Identifier.ToString())
		}
		return false;
	}
	OutBitmask = Bitmasks[*Index].Bitmask;
	return true;
}

bool PCGExBitmaskCollection::FCache::TryGetBitmask(const FName Identifier, PCGExBitmask::FCachedRef& OutCachedBitmask) const
{
	const int32* Index = BitmaskMap.Find(Identifier);
	if (!Index)
	{
		if (!Identifier.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("Bitmask \"%s\" doesn't exists."), *Identifier.ToString())
		}
		return false;
	}
	OutCachedBitmask = Bitmasks[*Index];
	return true;
}

// Thread-safe lazy cache initialization. Read-lock fast path returns only a fresh cache;
// stale drops happen under the write lock (resetting the shared ptr under a read lock races
// concurrent readers on the control block).
PCGExBitmaskCollection::FCache* UPCGExBitmaskCollection::LoadCache()
{
	{
		FReadScopeLock ReadScopeLock(CacheLock);
		if (!bCacheNeedsRebuild && Cache)
		{
			return Cache.Get();
		}
	}

	{
		FWriteScopeLock WriteScopeLock(CacheLock);
		if (bCacheNeedsRebuild)
		{
			// Clearing the flag claims the rebuild -- a peer must not drop the cache this thread
			// is about to build.
			Cache.Reset();
			bCacheNeedsRebuild = false;
		}
		if (Cache)
		{
			return Cache.Get();
		}
	}

	// Outside the lock -- BuildCache takes the write lock itself (FRWLock is non-reentrant).
	BuildCache();

	FReadScopeLock ReadScopeLock(CacheLock);
	return Cache.Get();
}

void UPCGExBitmaskCollection::InvalidateCache()
{
	// Takes the write lock -- never call while holding CacheLock.
	FWriteScopeLock WriteScopeLock(CacheLock);
	Cache.Reset();
	bCacheNeedsRebuild = true;
}

void UPCGExBitmaskCollection::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

void UPCGExBitmaskCollection::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

void UPCGExBitmaskCollection::PostEditImport()
{
	Super::PostEditImport();
#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

void UPCGExBitmaskCollection::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	Context->EDITOR_TrackPath(this);
	for (const FPCGExBitmaskCollectionEntry& Entry : Entries)
	{
		Entry.EDITOR_RegisterTrackingKeys(Context);
	}
}

void UPCGExBitmaskCollection::BuildCache()
{
	FWriteScopeLock WriteScopeLock(CacheLock);

	if (Cache)
	{
		return;
	} // Cache needs to be invalidated

	Cache = MakeShared<PCGExBitmaskCollection::FCache>();
	bCacheNeedsRebuild = false;

	const int32 NumEntries = Entries.Num();
	Cache->Bitmasks.Reserve(NumEntries);
	Cache->BitmaskMap.Reserve(NumEntries);
	Cache->Identifiers.Reserve(NumEntries);

	TSet<FName> UniqueIdentifiers;
	for (FPCGExBitmaskCollectionEntry& Entry : Entries)
	{
		Entry.RebuildCache();
		PCGExBitmask::FCachedRef EntryCache = Entry.CachedBitmask;
		bool bAlreadyInSet = false;
		UniqueIdentifiers.Add(Entry.Identifier, &bAlreadyInSet);
		Cache->BitmaskMap.Add(Entry.Identifier, Cache->Bitmasks.Add(EntryCache));
		if (!bAlreadyInSet)
		{
			Cache->Identifiers.Add(Entry.Identifier.ToString());
		}
	}
}

#if WITH_EDITOR

bool UPCGExBitmaskCollection::HasCircularDependency(const UPCGExBitmaskCollection* OtherCollection) const
{
	if (OtherCollection == this)
	{
		return true;
	}
	TSet<const UPCGExBitmaskCollection*> References;
	References.Add(this);
	return OtherCollection->HasCircularDependency(References);
}

bool UPCGExBitmaskCollection::HasCircularDependency(TSet<const UPCGExBitmaskCollection*>& InReferences) const
{
	bool bIsAlreadyInSet = false;
	InReferences.Add(this, &bIsAlreadyInSet);

	if (bIsAlreadyInSet)
	{
		return true;
	}

	for (const FPCGExBitmaskCollectionEntry& Entry : Entries)
	{
		for (const FPCGExBitmaskRef& Ref : Entry.Bitmask.Compositions)
		{
			if (Ref.Source == this)
			{
				return true;
			}
			if (Ref.Source && Ref.Source->HasCircularDependency(InReferences))
			{
				return true;
			}
		}
	}

	return false;
}

void UPCGExBitmaskCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (FPCGExBitmaskCollectionEntry& Entry : Entries)
	{
		for (FPCGExBitmaskRef& Ref : Entry.Bitmask.Compositions)
		{
			if (Ref.Source && HasCircularDependency(Ref.Source))
			{
				UE_LOG(LogTemp, Error, TEXT("Prevented circular dependency trying to nest \"%s\" inside \"%s\""), *GetNameSafe(Ref.Source), *GetNameSafe(this))
				Ref.Source = nullptr;
			}
		}
	}

	if (PropertyChangedEvent.Property)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
	}

	EDITOR_SetDirty();
}

TArray<FName> UPCGExBitmaskCollection::EDITOR_GetIdentifierOptions() const
{
	TArray<FName> Options;
	Options.Reserve(const_cast<UPCGExBitmaskCollection*>(this)->LoadCache()->Bitmasks.Num());
	for (const PCGExBitmask::FCachedRef& StagedBitmask : Cache->Bitmasks)
	{
		Options.Add(StagedBitmask.Identifier);
	}
	return Options;
}
#endif
