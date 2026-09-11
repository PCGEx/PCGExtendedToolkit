// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Components/PCGExDataCacheComponent.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadata.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/Package.h"

#include "PCGExLog.h"
#include "PCGExVersion.h"
#include "Helpers/PCGExDataCacheHelpers.h"

#if WITH_EDITOR
#include "PCGComponent.h"
#include "PCGWorldActor.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#endif

namespace PCGExDataCacheComponent
{
	// Caller holds the write lock. Moves the entry out of InMap; false when absent.
	bool Take(TMap<FName, FPCGExDataCacheEntry>& InMap, const FName InId, TArray<FPCGExDataCacheEntry>& OutTaken)
	{
		FPCGExDataCacheEntry Entry;
		if (!InMap.RemoveAndCopyValue(InId, Entry)) { return false; }
		OutTaken.Add(MoveTemp(Entry));
		return true;
	}

	// Caller holds the write lock. Moves every entry out of InMap.
	void TakeAll(TMap<FName, FPCGExDataCacheEntry>& InMap, TArray<FPCGExDataCacheEntry>& OutTaken)
	{
		OutTaken.Reserve(OutTaken.Num() + InMap.Num());
		for (TPair<FName, FPCGExDataCacheEntry>& Pair : InMap) { OutTaken.Add(MoveTemp(Pair.Value)); }
		InMap.Reset();
	}

	// Caller holds the write lock. Hides a persisted ID from preview reads without touching persisted data.
	void Tombstone(TMap<FName, FPCGExDataCacheEntry>& InPreviewMap, const FName InId, TArray<FPCGExDataCacheEntry>& OutTaken)
	{
		Take(InPreviewMap, InId, OutTaken);
		InPreviewMap.Add(InId).bTombstone = true;
	}
}

#pragma region UPCGExDataCacheComponent

UPCGExDataCacheComponent::UPCGExDataCacheComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UPCGExDataCacheComponent* UPCGExDataCacheComponent::Find(const AActor* InActor)
{
	if (!IsValid(InActor)) { return nullptr; }
	if (UPCGExDataCacheComponent* Component = InActor->FindComponentByClass<UPCGExDataCacheComponent>()) { return Component; }

	// Level copy-paste can leave an instance component in InstanceComponents without registering it.
	for (UActorComponent* InstanceComponent : InActor->GetInstanceComponents())
	{
		if (UPCGExDataCacheComponent* Component = Cast<UPCGExDataCacheComponent>(InstanceComponent)) { return Component; }
	}

	return nullptr;
}

UPCGExDataCacheComponent* UPCGExDataCacheComponent::FindOrCreate(AActor* InActor, const bool bTransient)
{
	check(IsInGameThread());

	if (!IsValid(InActor)) { return nullptr; }

	if (UPCGExDataCacheComponent* Existing = Find(InActor))
	{
		// Born in preview, promoted by the first persistent write. Never demoted.
		if (!bTransient && Existing->HasAnyFlags(RF_Transient))
		{
			Existing->ClearFlags(RF_Transient);
			Existing->SetFlags(RF_Transactional);
			Existing->MarkPackageDirty();
		}
		return Existing;
	}

	// Instance components survive construction-script reruns and are not touched by PCG cleanup; same path as
	// the engine's Add Component node, including RF_Transient for preview-mode writers.
	UPCGExDataCacheComponent* Component = NewObject<UPCGExDataCacheComponent>(InActor, NAME_None, bTransient ? RF_Transient : RF_Transactional);
	Component->RegisterComponent();
	InActor->AddInstanceComponent(Component);
	return Component;
}

bool UPCGExDataCacheComponent::Read(const FName InId, TArray<FPCGTaggedData>& OutData) const
{
	UE::TReadScopeLock ScopedReadLock(Lock);

	const FPCGExDataCacheEntry* Entry = PreviewEntries.Find(InId);
	if (Entry && Entry->bTombstone) { return false; }
	if (!Entry) { Entry = Entries.Find(InId); }
	if (!Entry) { return false; }

	OutData = Entry->Data.TaggedData;
	return true;
}

void UPCGExDataCacheComponent::ReadAll(TArray<TPair<FName, TArray<FPCGTaggedData>>>& OutEntries) const
{
	UE::TReadScopeLock ScopedReadLock(Lock);

	OutEntries.Reserve(OutEntries.Num() + Entries.Num() + PreviewEntries.Num());
	for (const TPair<FName, FPCGExDataCacheEntry>& Pair : PreviewEntries)
	{
		if (!Pair.Value.bTombstone) { OutEntries.Emplace(Pair.Key, Pair.Value.Data.TaggedData); }
	}
	for (const TPair<FName, FPCGExDataCacheEntry>& Pair : Entries)
	{
		if (!PreviewEntries.Contains(Pair.Key)) { OutEntries.Emplace(Pair.Key, Pair.Value.Data.TaggedData); }
	}
}

void UPCGExDataCacheComponent::Write(const FName InId, const bool bAppend, TArray<FPCGTaggedData>&& InData, UObject* InWriter, const bool bPreview, const bool bNotify)
{
	check(IsInGameThread());

	// Rename/Flatten happen outside the lock; the lock only brackets the map swap.
	TArray<FPCGTaggedData> Adopted = AdoptData(MoveTemp(InData), bPreview);
	if (Adopted.IsEmpty())
	{
		UE_LOG(LogPCGEx, Verbose, TEXT("[Data Cache] Nothing cacheable for '%s'; entry left untouched."), *InId.ToString());
		return;
	}

	TArray<FPCGExDataCacheEntry> Released;
	{
		UE::TWriteScopeLock ScopedWriteLock(Lock);

		FPCGExDataCacheEntry& Entry = (bPreview ? PreviewEntries : Entries).FindOrAdd(InId);
		if (!bAppend || Entry.bTombstone) { Released.Add(MoveTemp(Entry)); Entry = FPCGExDataCacheEntry(); }

		Entry.Data.TaggedData.Append(MoveTemp(Adopted));
		Entry.Writer = FSoftObjectPath(InWriter);

		// A persistent write supersedes the preview shadow (or tombstone) of the same ID.
		if (!bPreview) { PCGExDataCacheComponent::Take(PreviewEntries, InId, Released); }
	}

	ReleaseData(Released);

	if (!bPreview) { MarkPackageDirty(); }
	if (bNotify) { NotifyChanged(InWriter); }
}

void UPCGExDataCacheComponent::Clear(const FName InId, UObject* InWriter, const bool bPreview, const bool bNotify)
{
	check(IsInGameThread());

	TArray<FPCGExDataCacheEntry> Released;
	bool bChanged = false;
	bool bDirty = false;
	{
		UE::TWriteScopeLock ScopedWriteLock(Lock);
		if (bPreview)
		{
			// A preview generation never touches persisted data: it hides the ID instead.
			const FPCGExDataCacheEntry* Preview = PreviewEntries.Find(InId);
			const bool bVisible = Preview ? !Preview->bTombstone : Entries.Contains(InId);
			if (bVisible)
			{
				bChanged = true;
				if (Entries.Contains(InId)) { PCGExDataCacheComponent::Tombstone(PreviewEntries, InId, Released); }
				else { PCGExDataCacheComponent::Take(PreviewEntries, InId, Released); }
			}
		}
		else
		{
			bDirty = PCGExDataCacheComponent::Take(Entries, InId, Released);
			bChanged = PCGExDataCacheComponent::Take(PreviewEntries, InId, Released) || bDirty;
		}
	}

	ReleaseData(Released);

	if (bDirty) { MarkPackageDirty(); }
	if (bNotify && bChanged) { NotifyChanged(InWriter); }
}

void UPCGExDataCacheComponent::ClearAll(UObject* InWriter, const bool bPreview, const bool bNotify)
{
	check(IsInGameThread());

	TArray<FPCGExDataCacheEntry> Released;
	bool bChanged = false;
	bool bDirty = false;
	{
		UE::TWriteScopeLock ScopedWriteLock(Lock);
		if (bPreview)
		{
			// Hide every persisted ID and drop the preview shadows.
			for (const TPair<FName, FPCGExDataCacheEntry>& Pair : PreviewEntries) { bChanged |= !Pair.Value.bTombstone; }
			PCGExDataCacheComponent::TakeAll(PreviewEntries, Released);
			for (const TPair<FName, FPCGExDataCacheEntry>& Pair : Entries)
			{
				PreviewEntries.Add(Pair.Key).bTombstone = true;
				bChanged = true;
			}
		}
		else
		{
			bDirty = !Entries.IsEmpty();
			bChanged = bDirty || !PreviewEntries.IsEmpty();
			PCGExDataCacheComponent::TakeAll(Entries, Released);
			PCGExDataCacheComponent::TakeAll(PreviewEntries, Released);
		}
	}

	ReleaseData(Released);

	if (bDirty) { MarkPackageDirty(); }
	if (bNotify && bChanged) { NotifyChanged(InWriter); }
}

void UPCGExDataCacheComponent::NotifyChanged(UObject* InWriter) const
{
#if WITH_EDITOR
	check(IsInGameThread());

	AActor* Owner = GetOwner();
	if (!Owner) { return; }

	// PCGActorTracker::ShouldIgnoreActor (FPCGActorTracker::OnObjectPropertyChanged) drops the PCG World Actor.
	if (Owner->IsA<APCGWorldActor>())
	{
		UE_LOG(LogPCGEx, Verbose, TEXT("[Data Cache] Change notifications on the PCG World Actor are ignored by PCG tracking."));
		return;
	}

	// FPCGActorTracker::ShouldDelayActor defers actors still registering components to its Tick; that dispatch would
	// land after the scope below closed and re-trigger the writer. Skip rather than loop. (An unloaded level-instance
	// hierarchy defers the same way and is not detected here.)
	if (!Owner->HasActorRegisteredAllComponents())
	{
		UE_LOG(LogPCGEx, Verbose, TEXT("[Data Cache] '%s' is still registering components; change notification skipped."), *Owner->GetName());
		return;
	}

	UPCGExDataCacheComponent* MutableThis = const_cast<UPCGExDataCacheComponent*>(this);
	auto Broadcast = [MutableThis]() { PCGExEditor::NotifyObjectChanged(MutableThis); };

	// The actor tracker maps this component to its owner, then FPCGComponentChangeHandler::ShouldDiscardComponent
	// consults the tracked component's ORIGINAL component ignore list against {owner actor, this}. Dispatch is
	// synchronous from here, so the engine's scope (Start / ON_SCOPE_EXIT Stop on the original) is enough.
	if (UPCGComponent* WriterComponent = Cast<UPCGComponent>(InWriter))
	{
		WriterComponent->IgnoreChangeOriginDuringGenerationWithScope(Owner, Broadcast);
	}
	else
	{
		Broadcast();
	}
#else
	(void)InWriter;
#endif
}

#if WITH_EDITOR
void UPCGExDataCacheComponent::EDITOR_ClearCache()
{
	// Not transacted: the data release renames non-transactionally, so an undo would restore dangling references.
	ClearAll(/*InWriter=*/nullptr, /*bPreview=*/false, /*bNotify=*/true);
}
#endif

void UPCGExDataCacheComponent::ReleaseData(const TArray<FPCGExDataCacheEntry>& InEntries) const
{
	// Mirrors UPCGComponent::ClearGraphGeneratedOutput: only objects we own go back to the transient package. Any
	// context still holding the data keeps it alive; GC reclaims it once the last reference drops.
	for (const FPCGExDataCacheEntry& Entry : InEntries)
	{
		for (const FPCGTaggedData& TaggedData : Entry.Data.TaggedData)
		{
			if (TaggedData.Data && TaggedData.Data->GetOuter() == this)
			{
				const_cast<UPCGData*>(TaggedData.Data.Get())->Rename(nullptr, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}
}

TArray<FPCGTaggedData> UPCGExDataCacheComponent::AdoptData(TArray<FPCGTaggedData>&& InData, const bool bPreview) const
{
	TArray<FPCGTaggedData> Adopted;
	Adopted.Reserve(InData.Num());

	UPCGExDataCacheComponent* NewOuter = const_cast<UPCGExDataCacheComponent*>(this);

	for (FPCGTaggedData& TaggedData : InData)
	{
		if (!TaggedData.Data) { continue; }
		UPCGData* Data = const_cast<UPCGData*>(TaggedData.Data.Get());

		// Proxies (render targets, ...) hold non-serializable resources; the engine refuses them on component output too.
		if (!Data->CanBeSerialized())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Data Cache] '%s' cannot be serialized and was not cached."), *Data->GetName());
			continue;
		}

		// A fresh duplicate may still be a lazy copy of its source; only then is the (per-entry) flatten worth paying.
		const UPCGMetadata* Metadata = Data->ConstMetadata();
#if PCGEX_ENGINE_VERSION >= 508
		const UPCGSpatialData* Parented = Cast<UPCGSpatialData>(Data);
#else
		// 5.7 declares HasSpatialDataParent on point data only.
		const UPCGBasePointData* Parented = Cast<UPCGBasePointData>(Data);
#endif
		if ((Metadata && Metadata->GetParent()) || (Parented && Parented->HasSpatialDataParent())) { Data->Flatten(); }

		if (!PCGExDataCache::IsSelfContained(Data))
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Data Cache] '%s' references other data (union, intersection, projection...) and was not cached; convert it to points first."), *Data->GetName());
			continue;
		}

		if (bPreview) { Data->SetFlags(RF_Transient); }
		else { Data->ClearFlags(RF_Transient); }
		// Never dirtying here: Write marks the package once. Non-transactional like the release path.
		Data->Rename(nullptr, NewOuter, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);

		Adopted.Add(MoveTemp(TaggedData));
	}

	return Adopted;
}

#pragma endregion
