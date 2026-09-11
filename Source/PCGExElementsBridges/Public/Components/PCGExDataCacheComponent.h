// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "Components/ActorComponent.h"
#include "Misc/TransactionallySafeRWLock.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExDataCacheComponent.generated.h"

UENUM()
enum class EPCGExDataCacheWriteMode : uint8
{
	Replace  = 0 UMETA(DisplayName = "Replace", Tooltip = "Replace whatever is stored under the cache ID with the input data. With no cacheable input, the entry is left untouched."),
	Append   = 1 UMETA(DisplayName = "Append", Tooltip = "Append the input data to whatever is already stored under the cache ID."),
	Clear    = 2 UMETA(DisplayName = "Clear", Tooltip = "Remove the entry stored under the cache ID."),
	ClearAll = 3 UMETA(DisplayName = "Clear All", Tooltip = "Remove every entry on the target cache. Cache ID is ignored."),
};

/** One cached collection. Data objects are outered to the owning cache component so they serialize with the actor. */
USTRUCT()
struct PCGEXELEMENTSBRIDGES_API FPCGExDataCacheEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Data Cache")
	FPCGDataCollection Data;

	/** Diagnostic: execution source (usually a PCG component) that last wrote this entry. */
	UPROPERTY(VisibleAnywhere, Category = "Data Cache")
	FSoftObjectPath Writer;

	/** Preview map only: a preview-mode Clear of a persisted ID hides that ID without touching persisted data. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Data Cache")
	bool bTombstone = false;
};

/**
 * Holds PCG data collections under named IDs, persisted with the owning actor the same way a PCG component
 * persists its generated output. One per actor (see Find / FindOrCreate), created only by Set Cached Data.
 * Never a PCG managed resource: cleanup and regeneration leave it alone, so a refresh can read back what a
 * previous pass stored.
 *
 * Only self-contained data is cached (point, spline, volume, attribute sets...). Data referencing other data
 * objects (unions, intersections, projections, collision wrappers) is refused: it would either steal live
 * upstream objects mid-execution or serialize with null operands.
 *
 * Preview-mode writers get a transient shadow map that never saves, plus tombstones so a preview Clear hides a
 * persisted ID. Unlike a PCG component's output, nothing is promoted when the writer's editing mode changes:
 * the next persistent write does that.
 *
 * Reads are safe from any thread. Writes are game-thread only: adopting data re-outers it (Rename) and
 * flattens it (Modify), neither of which is thread-safe.
 */
UCLASS(ClassGroup = (Procedural), meta = (DisplayName = "PCGEx Data Cache"))
class PCGEXELEMENTSBRIDGES_API UPCGExDataCacheComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCGExDataCacheComponent();

	/** The actor's cache component, or null. Also finds instance components a level copy-paste left unregistered. */
	static UPCGExDataCacheComponent* Find(const AActor* InActor);

	/**
	 * The actor's cache component, created as an instance component if missing. Game thread only.
	 * bTransient (preview-mode writers) creates an RF_Transient component; a later persistent write promotes it.
	 */
	static UPCGExDataCacheComponent* FindOrCreate(AActor* InActor, const bool bTransient);

	/** Copies the entry stored under InId (preview entries shadow persisted ones). Any thread. */
	bool Read(const FName InId, TArray<FPCGTaggedData>& OutData) const;

	/** Copies every entry, preview entries shadowing persisted ones with the same ID. Any thread. */
	void ReadAll(TArray<TPair<FName, TArray<FPCGTaggedData>>>& OutEntries) const;

	/**
	 * Stores InData under InId (replacing or appending). Game thread only. Every data object must be a private
	 * duplicate outered to the transient package: it is flattened and re-outered to this component (data that is
	 * not self-contained is refused, see class comment). When nothing is adoptable the entry is left untouched.
	 * bPreview confines the write to the shadow map; a persistent write evicts the shadow of the same ID.
	 * bNotify: see NotifyChanged.
	 */
	void Write(const FName InId, const bool bAppend, TArray<FPCGTaggedData>&& InData, UObject* InWriter, const bool bPreview, const bool bNotify);

	/** Drops the entry under InId; a preview Clear only tombstones it. Game thread only. Notifies only on change. */
	void Clear(const FName InId, UObject* InWriter, const bool bPreview, const bool bNotify);

	/** Drops every entry; a preview Clear All only tombstones them. Game thread only. Notifies only on change. */
	void ClearAll(UObject* InWriter, const bool bPreview, const bool bNotify);

	/**
	 * Editor only, no-op otherwise. Broadcasts the standard object-changed pair so PCG components tracking the
	 * owner actor dirty and refresh. InWriter's original PCG component (if any) is bracketed with the engine's
	 * ignore-change-origin scope for the synchronous dispatch, so a writer never refreshes itself. Two writers
	 * on one actor both notifying will refresh each other on every generation.
	 */
	void NotifyChanged(UObject* InWriter) const;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Data Cache", meta = (DisplayName = "Clear Cache", ShortToolTip = "Remove every cached entry from this component. Not undoable."))
	void EDITOR_ClearCache();
#endif

protected:
	/** Persisted entries. */
	UPROPERTY(VisibleAnywhere, Category = "Data Cache")
	TMap<FName, FPCGExDataCacheEntry> Entries;

	/** Entries written by preview-mode generations. Never saved; shadow persisted entries on read. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Data Cache")
	TMap<FName, FPCGExDataCacheEntry> PreviewEntries;

	/** Guards both maps. Held only around map access, never around Rename/Flatten. */
	mutable FTransactionallySafeRWLock Lock;

	/** Re-outers every data object this component owns in InEntries back to the transient package so GC can reclaim it. */
	void ReleaseData(const TArray<FPCGExDataCacheEntry>& InEntries) const;

	/** Flattens and re-outers each adoptable data object to this component. Returns the data that was adopted. */
	TArray<FPCGTaggedData> AdoptData(TArray<FPCGTaggedData>&& InData, const bool bPreview) const;
};
