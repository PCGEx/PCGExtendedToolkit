// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "PCGExCollectionsCommon.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"

#include "PCGExLevelExportHandler.generated.h"

class AActor;
class UActorComponent;
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
	/** No collection: each item carries its own data, emitted as is on the pin (splines). */
	None = 2,
};

namespace PCGExLevelExport
{
	/** The slots one actor is classified into. More than one only for slots that take the actor apart. */
	using FSlotSet = TArray<FName, TInlineAllocator<2>>;
}

namespace PCGExLevelExport::Slots
{
	// Built-in slot ids double as the exported pin names and the external-asset suffixes.
	const FName Meshes = PCGExCollections::Labels::MeshesPin;
	const FName Actors = PCGExCollections::Labels::ActorsPin;
	const FName Levels = PCGExCollections::Labels::LevelsPin;
	const FName Splines = PCGExCollections::Labels::SplinesPin;
}

/** What a handler contributes: the pin its output lands on and the collection, if any, its entries live in. */
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

	/**
	 * The slot spawns its actors again (class + delta, level instance), components included: no other slot may
	 * harvest them. False = the slot takes the actor apart and keeps only what it harvests.
	 */
	bool bRecreatesSource = false;

	/** False for Scope None, which declares no collection class, entry struct or policy. */
	bool HasCollection() const
	{
		return Scope != EPCGExExportSlotScope::None;
	}

	bool IsValid() const
	{
		return !SlotId.IsNone() && !PinName.IsNone() && (HasCollection() ? (CollectionClass && EntryStruct) : (!CollectionClass && !EntryStruct));
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

	/**
	 * Whether Merged, which no previous shared entry Equals, succeeds Previous: it takes Previous' user-set
	 * Category and, claim-once, its EntryId. Override when identity includes source-derived fields a
	 * re-export can change (mesh tags).
	 */
	virtual bool IsSuccessor(const FPCGExAssetCollectionEntry& Previous, const FPCGExAssetCollectionEntry& Merged) const
	{
		return false;
	}
};

/**
 * One exported point, or one datum of a slot without a collection. Transform is already in the export frame; bounds
 * are point-local.
 */
struct PCGEXCOLLECTIONS_API FPCGExExportItem
{
	FTransform Transform = FTransform::Identity;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;

	/** Actor the point was harvested from -- generic per-point attributes (ActorName, value tags) read it. */
	AActor* SourceActor = nullptr;

	/** Index into the writer's TagSources; -1 = the point carries its actor's tags only. */
	int32 TagSource = INDEX_NONE;

	/** Index into the writer's entries; -1 = point without a pick. */
	int32 LocalEntryIndex = -1;

	/** Secondary pick (material variant); -1 = none. Ignored unless the slot supports secondaries. */
	int16 SecondaryIndex = -1;

	/** Component the item was harvested from, when its handler records one (meshes, splines). */
	const UActorComponent* SourceComponent = nullptr;

	/** Instance of an instanced SourceComponent; INDEX_NONE for a plain static mesh component. */
	int32 SourceInstanceIndex = INDEX_NONE;

	/**
	 * Slots without a collection: what the item emits, held alive by the item. The exporter emits its own copy and never
	 * edits this one, which a registry function may share.
	 */
	TStrongObjectPtr<const UPCGData> Data;
};

/**
 * One actor offered to the harvest pass. A root -- the root of a subtree export, or any IPCGExAssemblyRoot
 * implementer met inside the export (a module cage in a level, a nested root actor) -- is never an entry
 * itself; every other candidate carries the slots that claimed it: its classification tags, else a handler's
 * Claim, else the exporter's built-in classification.
 */
struct PCGEXCOLLECTIONS_API FPCGExExportCandidate
{
	AActor* Actor = nullptr;

	/** Never an entry: only its components are harvested. */
	bool bIsRoot = false;

	/** Empty for a root without classification tags. */
	PCGExLevelExport::FSlotSet ClaimedSlots;

	/** A claiming slot spawns the actor again (FPCGExExportSlotDesc::bRecreatesSource). Never set on a root. */
	bool bRecreated = false;

	/** Classified by its StaticClassifyMesh / StaticClassifySpline tags, not by a classificator or a handler. */
	bool bForcedByTag = false;

	bool IsClaimedBy(const FName SlotId) const
	{
		return ClaimedSlots.Contains(SlotId);
	}

	/** Whether a handler that harvests from any actor may read its components: false once they travel with the actor. */
	bool IsHarvestable() const
	{
		return !bRecreated;
	}
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
	 * Tags items carry on top of their actor's (FPCGExExportItem::TagSource) -- a mesh component's, say. The
	 * handler supplies authored tags only. Parsed as value tags ahead of every actor's and written after
	 * them, so a source wins over an actor on a shared name whatever the two types are.
	 */
	struct FTagSource
	{
		/** Named in type-conflict warnings. */
		FString Name;
		TArray<FName> Tags;
	};

	TArray<FTagSource> TagSources;

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
 *   1. Claim pass: an actor's StaticClassifyMesh / StaticClassifySpline tags classify it outright; else
 *      handlers by priority may claim it; unclaimed actors fall to the exporter's built-in classification
 *      (Mesh and/or Spline, Actor, Level).
 *   2. Harvest pass: every handler sees every candidate (roots included, flagged: the subtree root and
 *      any IPCGExAssemblyRoot implementer inside the export) with its claimed slots, and appends items
 *      to its own writer.
 *   3. Per slot: points (or, without a collection, each item's data) emitted on PinName, entries handed
 *      off -- captured for shared compaction, or built into a per-entry embedded collection.
 * A slot either re-creates the actors it claims (bRecreatesSource: Actors, Levels), whose components then
 * travel with them, or takes them apart and keeps only what it harvests (Meshes, Splines). A handler with
 * its own classification harvests its own claims (IsClaimedBy); one that harvests from any actor reads only
 * IsHarvestable() candidates.
 */
UCLASS(Abstract)
class PCGEXCOLLECTIONS_API UPCGExLevelExportHandler : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const PURE_VIRTUAL(UPCGExLevelExportHandler::GetSlotDesc, return FPCGExExportSlotDesc(););

	/** Required by a slot with a collection: registration refuses one without a policy. */
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const
	{
		return nullptr;
	}

	/** Lower runs first -- claim order and pin emission order. Built-ins use 10/20/30/40. */
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

	/**
	 * Slots without a collection: finishes Data, the exporter's own copy of Writer.Items[ItemIndex].Data, already
	 * outered to the exported asset. Runs after the generic ActorName and before value tags, data tags and the
	 * exporter's hooks; a value tag never overwrites a @Data attribute written here.
	 */
	virtual void FinalizeItemData(UPCGData* Data, int32 ItemIndex, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
	{
	}

	/** No picks are written when collections are not generated; write whatever has a raw form (asset paths). */
	virtual void WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
	{
	}

	/**
	 * Names of the attributes WriteItemAttributes writes, plus WriteRawAttributes' when bRaw. A source tag of the
	 * same name is skipped with a warning; unlisted, it would take the name first and that write would fail.
	 */
	virtual void GetWrittenAttributeNames(const UPCGExLevelDataExporter* Exporter, bool bRaw, TArray<FName>& OutNames) const
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
