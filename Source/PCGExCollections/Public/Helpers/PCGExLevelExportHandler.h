// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"

#include "PCGExLevelExportHandler.generated.h"

class AActor;
class UPCGExAssetCollection;
class UPCGExLevelDataExporter;
class UPCGMetadata;
struct FPCGExAssetCollectionEntry;
struct FPCGExLevelExportSource;

UENUM()
enum class EPCGExExportSlotScope : uint8
{
	/** One collection per host, compacted across every PCGDataAsset entry (meshes, levels, sketches). */
	Shared = 0,
	/** One collection per PCGDataAsset entry, rebuilt from that entry's export alone (actors). */
	PerEntry = 1,
};

namespace PCGExLevelExport::Slots
{
	// Built-in slot ids double as the exported pin names and the external-asset suffixes.
	const FName Meshes = PCGExCollections::Labels::MeshesPin;
	const FName Actors = PCGExCollections::Labels::ActorsPin;
	const FName Levels = PCGExCollections::Labels::LevelsPin;
}

/** What a handler contributes: the pin its points land on and the collection its entries live in. */
struct PCGEXCOLLECTIONS_API FPCGExExportSlotDesc
{
	/** Storage key on entries/state and external-asset suffix (<GUID>_<SlotId>). Stable forever. */
	FName SlotId = NAME_None;

	/** Pin on the exported PCGDataAsset. */
	FName PinName = NAME_None;

	/** A UPCGExAssetCollection subclass (checked at registration; a plain pointer keeps this header light). */
	UClass* CollectionClass = nullptr;
	UScriptStruct* EntryStruct = nullptr;
	EPCGExExportSlotScope Scope = EPCGExExportSlotScope::Shared;

	/** Local picks carry a secondary index (mesh material variants). */
	bool bSupportsSecondary = false;

	bool IsValid() const
	{
		return !SlotId.IsNone() && !PinName.IsNone() && CollectionClass && EntryStruct;
	}
};

#if WITH_EDITOR

/**
 * Compaction identity for a slot's entries. Hash/Equals decide which contributions merge into one
 * shared entry; SortKey orders the merged result (process-stable, fully discriminating -- see
 * PCGExCollectionSortKeys.h); PrimaryPath is the loose key that lets an EntryId survive a content
 * tweak. Entries are guaranteed to be the slot's EntryStruct.
 */
class PCGEXCOLLECTIONS_API IPCGExExportSlotPolicy
{
public:
	virtual ~IPCGExExportSlotPolicy() = default;

	virtual uint32 Hash(const FPCGExAssetCollectionEntry& Entry) const = 0;
	virtual bool Equals(const FPCGExAssetCollectionEntry& A, const FPCGExAssetCollectionEntry& B) const = 0;
	virtual FString SortKey(const FPCGExAssetCollectionEntry& Entry) const = 0;
	virtual FSoftObjectPath PrimaryPath(const FPCGExAssetCollectionEntry& Entry) const = 0;
};

/** One exported point. Transform is already in the export frame; bounds are point-local. */
struct PCGEXCOLLECTIONS_API FPCGExExportItem
{
	FTransform Transform = FTransform::Identity;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;

	/** Actor the point was harvested from -- generic per-point attributes (ActorName, value tags) read it. */
	AActor* SourceActor = nullptr;

	/** Index into the writer's entries; -1 = point without a pick. */
	int32 LocalEntryIndex = -1;

	/** Secondary pick (material variant); -1 = none. Ignored unless the slot supports secondaries. */
	int16 SecondaryIndex = -1;
};

/**
 * One actor offered to the harvest pass. A root -- the root of a subtree export, or any IPCGExAssemblyRoot
 * implementer met inside the export (a module cage in a level, a nested root actor) -- is never an entry
 * itself; every other candidate carries the slot that claimed it (a handler's Claim, else the exporter's
 * built-in classification).
 */
struct PCGEXCOLLECTIONS_API FPCGExExportCandidate
{
	AActor* Actor = nullptr;

	/** Never an entry: only its components are harvested. */
	bool bIsRoot = false;

	/** None for a root. */
	FName ClaimedSlot = NAME_None;
};

/** Per-export handler scratch; the handler defines the concrete type, the writer owns the instance. */
struct PCGEXCOLLECTIONS_API FPCGExExportScratch
{
	virtual ~FPCGExExportScratch() = default;
};

/**
 * Per-export, per-slot accumulation a handler writes into: identity-deduplicated entries plus the
 * points that pick them. The exporter turns it into point data on the slot's pin and either a capture
 * (Shared) or an embedded collection (PerEntry).
 */
class PCGEXCOLLECTIONS_API FPCGExExportSlotWriter
{
public:
	explicit FPCGExExportSlotWriter(const FPCGExExportSlotDesc& InDesc);

	const FPCGExExportSlotDesc Desc;

	/** Entries of Desc.EntryStruct, in local-index order. Weight = number of items that picked them. */
	TArray<FInstancedStruct> Entries;
	TArray<FPCGExExportItem> Items;

	/** Optional per-property inherited-defaults view (see FPCGExExportSlotCapture::InheritedDefaults). */
	TArray<FInstancedStruct> InheritedDefaults;

	/**
	 * Local index of the entry matching IdentityHash + Equals, adding one (Init runs on a fresh
	 * Desc.EntryStruct, whose local index is Entries.Num() at that moment) when none does. Weight is
	 * bumped by WeightIncrement either way. Equals receives the candidate's local index so a handler
	 * can compare against identity data it keeps parallel to Entries in its scratch.
	 */
	int32 FindOrAddEntry(
		uint32 IdentityHash,
		TFunctionRef<bool(int32 LocalIndex, const FPCGExAssetCollectionEntry&)> Equals,
		TFunctionRef<void(FPCGExAssetCollectionEntry&)> Init,
		int32 WeightIncrement = 1);

	FPCGExAssetCollectionEntry& GetEntry(int32 LocalIndex);
	const FPCGExAssetCollectionEntry& GetEntry(int32 LocalIndex) const;

	template <typename T>
	T& GetEntry(const int32 LocalIndex)
	{
		return *Entries[LocalIndex].GetMutablePtr<T>();
	}

	template <typename T>
	const T& GetEntry(const int32 LocalIndex) const
	{
		return *Entries[LocalIndex].GetPtr<T>();
	}

	FPCGExExportItem& AddItem(const FTransform& InTransform, const FVector& InBoundsMin, const FVector& InBoundsMax, AActor* InSourceActor, int32 InLocalEntryIndex, int16 InSecondaryIndex = -1);

	template <typename T>
	T& GetOrCreateScratch()
	{
		if (!Scratch)
		{
			Scratch = MakeShared<T>();
		}
		return static_cast<T&>(*Scratch);
	}

	/** The scratch Collect created, or null when it never did. */
	template <typename T>
	const T* GetScratch() const
	{
		return static_cast<const T*>(Scratch.Get());
	}

	bool IsEmpty() const
	{
		return Items.IsEmpty();
	}

private:
	TMap<uint32, TArray<int32>> Buckets;
	TSharedPtr<FPCGExExportScratch> Scratch;
};

#endif // WITH_EDITOR

/**
 * One kind of level-export content: what claims an actor, what is harvested from it, which pin the
 * points go to, and which collection type the entries belong to. Registered by CLASS from a module's
 * StartupModule (PCGExLevelExport::FHandlerRegistry) and run as the class default object -- handlers
 * are stateless, const-only policies; per-export state lives in the writer's scratch.
 *
 * Harvest order per export (UPCGExDefaultLevelDataExporter):
 *   1. Claim pass: handlers by priority may claim a filtered actor; unclaimed actors fall to the
 *      exporter's built-in classification (Mesh / Actor / Level).
 *   2. Harvest pass: every handler sees every candidate (roots included, flagged: the subtree root and
 *      any IPCGExAssemblyRoot implementer inside the export) with its claimed slot, and appends items
 *      to its own writer.
 *   3. Per slot: points emitted on PinName, entries handed off -- captured for shared compaction, or
 *      built into a per-entry embedded collection.
 * A handler must only parse candidates that will not be spawned as actors: the root, its own claims,
 * and content-bearing claims of other slots (meshes). Actor-claimed candidates travel as a class +
 * delta and already carry their components.
 */
UCLASS(Abstract)
class PCGEXCOLLECTIONS_API UPCGExLevelExportHandler : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const PURE_VIRTUAL(UPCGExLevelExportHandler::GetSlotDesc, return FPCGExExportSlotDesc(););
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const PURE_VIRTUAL(UPCGExLevelExportHandler::MakePolicy, return nullptr;);

	/** Lower runs first -- claim order and pin emission order. Built-ins use 10/20/30. */
	virtual int32 GetPriority() const
	{
		return 100;
	}

	/** Take Actor out of the built-in classification. Return true to make it this slot's candidate. */
	virtual bool Claim(AActor* Actor, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter) const
	{
		return false;
	}

	virtual void Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const PURE_VIRTUAL(UPCGExLevelExportHandler::Collect,);

	/** Last touch on the writer before points are emitted and entries handed off. */
	virtual void FinalizeSlot(FPCGExExportSlotWriter& Writer, UObject* Outer, const UPCGExLevelDataExporter* Exporter) const
	{
	}

	/** Per-point attributes beyond the generic ActorName / value tags. MetaEntries is parallel to Writer.Items. */
	virtual void WriteItemAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
	{
	}

	/** No picks are written when collections are not generated; write whatever has a raw form (asset paths). */
	virtual void WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
	{
	}

	/** PerEntry slots only: the embedded collection holds the writer's entries with their EntryIds
	 *  claimed; runs before its staging rebuild. */
	virtual void FinalizeEmbeddedCollection(UPCGExAssetCollection* Collection, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
	{
	}
#endif
};

#if WITH_EDITOR

namespace PCGExLevelExport
{
	struct PCGEXCOLLECTIONS_API FHandlerRegistration
	{
		FPCGExExportSlotDesc Desc;
		TSharedPtr<const IPCGExExportSlotPolicy> Policy;
		TSubclassOf<UPCGExLevelExportHandler> HandlerClass;
		int32 Priority = 100;
	};

	/**
	 * Handler classes keyed by slot id. Register from StartupModule only -- never from a static
	 * initializer: the handler CDO is read at registration (slot descriptor, policy), and a class
	 * default object cannot be resolved during static init. A module that registers MUST unregister
	 * from its ShutdownModule: the registry outlives it and holds a policy compiled into its DLL.
	 */
	class PCGEXCOLLECTIONS_API FHandlerRegistry
	{
	public:
		static FHandlerRegistry& Get();

		/** Registers HandlerClass under its CDO's slot id. A second class on the same slot is refused. */
		bool Register(TSubclassOf<UPCGExLevelExportHandler> HandlerClass);
		void Unregister(FName SlotId);

		bool FindSlot(FName SlotId, FPCGExExportSlotDesc& OutDesc) const;
		TSharedPtr<const IPCGExExportSlotPolicy> GetPolicy(FName SlotId) const;

		/** Every registration, priority ascending. Copies out; safe to hold. */
		void GetRegistrations(TArray<FHandlerRegistration>& OutRegistrations) const;
		void GetSlots(TArray<FPCGExExportSlotDesc>& OutSlots) const;

	private:
		FHandlerRegistry() = default;

		mutable FRWLock Lock;
		TMap<FName, FHandlerRegistration> Registrations;
	};
}

#endif // WITH_EDITOR
