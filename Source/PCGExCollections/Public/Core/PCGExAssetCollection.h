// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExAssetCollectionTypes.h"
#include "PCGExAssetGrammar.h"
#include "PCGExCollectionGlobals.h"
#include "PCGExProperty.h"
#include "PCGExSchemaMerging.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExSocket.h"
#include "Details/PCGExStagingDetails.h"
#include "Fitting/PCGExFittingOverrides.h"
#include "Fitting/PCGExFittingVariations.h"
#include "Helpers/PCGExCookDependencyProvider.h"
#include "Helpers/PCGExStreamingHelpers.h"

#include "PCGExAssetCollection.generated.h"

#if WITH_EDITOR
struct FAssetData;
#endif

class UPCGExAssetCollection;
class UPCGExCollectionStagingPipeline;
class UPCGExCollectionTypeState;

namespace PCGExAssetCollection
{
	class FCache;
	class FCategory;
	class FMicroCache;

	enum class ELoadingFlags : uint8
	{
		Default = 0,
		Recursive,
		RecursiveCollectionsOnly,
	};
}

struct FPCGExAssetCollectionEntry;

/**
 * Result of a collection entry lookup. Bundles the entry pointer with the host collection
 * that owns it (important when subcollections are involved, since the entry may come from
 * a nested collection). Use As<T>() to downcast to a concrete entry type.
 */
struct PCGEXCOLLECTIONS_API FPCGExEntryAccessResult
{
	const FPCGExAssetCollectionEntry* Entry = nullptr;
	const UPCGExAssetCollection* Host = nullptr;

	FORCEINLINE operator bool() const
	{
		return Entry != nullptr;
	}

	FORCEINLINE bool IsValid() const
	{
		return Entry != nullptr;
	}

	template <typename T>
	FORCEINLINE const T* As() const
	{
		return static_cast<const T*>(Entry);
	}

	// Check if entry is of a specific type
	bool IsType(PCGExAssetCollection::FTypeId TypeId) const;
};

/**
 * Pre-computed data shared across all entry types. Populated during UpdateStaging()
 * (editor-time or on demand). Stores the soft path for async loading, cached bounds
 * for spatial queries, and sockets extracted from the underlying asset (e.g. mesh sockets).
 * Use LoadSync<T>() for thread-safe loading or TryGet<T>() for already-loaded assets.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Asset Staging Data")
struct PCGEXCOLLECTIONS_API FPCGExAssetStagingData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 InternalIndex = -1;

	UPROPERTY()
	FSoftObjectPath Path;

	/** Sockets attached to this entry. Maintained automatically, supports user-defined entries. */
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGExSocket> Sockets;

	/** Cached bounds. Computed automatically. */
	UPROPERTY(VisibleAnywhere, Category = Settings)
	FBox Bounds = FBox(ForceInit);

	/**
	 * Staging content (bounds, sockets) was authored by an external system (e.g. a generated
	 * collection whose entries carry authoritative bounds) instead of being derived from the
	 * asset. Entry types honor this in UpdateStaging() by refreshing identity fields only
	 * (InternalIndex, Path) and leaving content untouched, so no rebuild path -- editor
	 * toolbar, grid, stale-entry batch, pipelines -- can clobber authored staging.
	 * Cooked (not editor-only): runtime staging refreshes must preserve it too.
	 */
	UPROPERTY(VisibleAnywhere, Category = Settings)
	bool bAuthored = false;

	/**
	 * Optional modifier that derives AlteredBounds (the bounds used for fitting, spacing and
	 * best-fit selection) from the original asset-derived Bounds. Null = AlteredBounds mirrors
	 * Bounds. The bounds applied to the mesh itself always remain the original ones.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExCollections.PCGExStagingBoundsModifier", ExcludeBaseStruct))
	FInstancedStruct BoundsStagingModifier;

	FBox AlteredBounds = FBox(ForceInit);

	template <typename T>
	T* LoadSync(FPCGExContext* InContext = nullptr) const
	{
		TSoftObjectPtr<T> SoftObjectPtr = TSoftObjectPtr<T>(Path);
		PCGExHelpers::LoadBlocking_AnyThreadTpl<T>(SoftObjectPtr, InContext);
		return SoftObjectPtr.Get();
	}

	template <typename T>
	T* TryGet() const
	{
		return TSoftObjectPtr<T>(Path).Get();
	}

	bool FindSocket(FName InName, const FPCGExSocket*& OutSocket) const;
	bool FindSocket(FName InName, const FString& Tag, const FPCGExSocket*& OutSocket) const;
};

/**
 * Base entry in an asset collection. Each entry is either a direct asset reference
 * or a subcollection pointer (controlled by bIsSubCollection).
 *
 * Creating a custom collection type:
 * 1. Subclass this struct -- add your asset-specific UPROPERTY (e.g. TSoftObjectPtr<UMyAsset>)
 * 2. Override GetTypeId() to return your registered FTypeId
 * 3. Override Validate() to reject invalid entries (call Super)
 * 4. Override UpdateStaging() to populate Staging.Bounds and Staging.Path from your asset
 * 5. Override SetAssetPath() to update your TSoftObjectPtr from a path
 * 6. Optionally override EDITOR_Sanitize() to clean up type-specific state (call Super)
 * 7. Optionally override BuildMicroCache() for per-entry sub-selections (e.g. material variants)
 * Subcollection support comes for free: the base SubCollection property accepts any
 * collection type, no per-type storage or overrides needed.
 *
 * Key properties inherited:
 * - Weight: pick probability (0 = excluded from cache)
 * - Category: named group for category-based picking
 * - Tags: arbitrary FName set, inheritable through subcollection hierarchy
 * - Variations: per-entry fitting transforms (scale/rotation randomization), opt-in via VariationMode
 * - ScaleToFit / Justification: opt-in per-entry overrides of the staging nodes' fitting settings
 * - PropertyOverrides: per-entry override of collection-level custom properties
 * - Staging: pre-computed bounds, path, and sockets
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Asset Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	virtual ~FPCGExAssetCollectionEntry() = default;
	FPCGExAssetCollectionEntry() = default;

	/** Get the type ID of this entry */
	virtual PCGExAssetCollection::FTypeId GetTypeId() const
	{
		return bIsSubCollection ? PCGExAssetCollection::TypeIds::Base : PCGExAssetCollection::TypeIds::None;
	}

	/** Check if this entry is of a specific type (or derives from it) */
	bool IsType(PCGExAssetCollection::FTypeId TypeId) const
	{
		return PCGExAssetCollection::FTypeRegistry::Get().IsA(GetTypeId(), TypeId);
	}

	/**
	 * Bake collection-level ("Global") inheritance channels into local values, reading the
	 * source's globals through GetTypeGlobals. Call when an entry is copied OUT of its
	 * collection and the destination won't carry an equivalent globals block -- otherwise
	 * global-sourced values are silently lost. Base is a no-op.
	 */
	virtual void ResolveGlobalsToLocal(const UPCGExAssetCollection* InSourceCollection)
	{
	}

#pragma region Core
	
	/**
	 * Stable identity for external entry references (e.g. variant collections) across source
	 * edits -- reorder, rename, duplicate. 0 = unassigned. NEVER assign in the ctor: it would
	 * defeat UE's CDO->instance propagation for arrays-of-structs (see FPCGExProperty::HeaderId).
	 * Assigned and deduplicated by UPCGExAssetCollection::SyncEntryIds (RebuildStagingData).
	 * Cooked deliberately (not editor-only) so packaged builds can match entries by identity.
	 */
	UPROPERTY()
	int32 EntryId = 0;

	// Core Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, ClampMin=0, UIMin=0))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, Category = Settings)
	FName Category = NAME_None;

	UPROPERTY(EditAnywhere, Category = Settings)
	bool bIsSubCollection = false;
	
#pragma endregion
	
#pragma region Grammar
	
	/**
	 * Property overrides for this entry.
	 * Values here take precedence over collection-level defaults.
	 * Only include properties you want to override.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExPropertyOverrides PropertyOverrides;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides, InvalidEnumValues="None"))
	EPCGExEntryVariationMode GrammarSource = EPCGExEntryVariationMode::Local;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName="Grammar Mode", EditCondition="bIsSubCollection", EditConditionHides))
	EPCGExGrammarSubCollectionMode SubGrammarMode = EPCGExGrammarSubCollectionMode::Inherit;

	/**
	 * Per-entry grammar configuration. Shared between leaf and subcollection entries; for
	 * subcollections this slot is populated only when SubGrammarMode == Override (it then
	 * replaces the subcollection's own SubCollectionGrammar wholesale). Customization
	 * gates EPCGExGrammarAxisSize values based on bIsSubCollection.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName="Grammar", EditCondition="!bIsSubCollection || SubGrammarMode == EPCGExGrammarSubCollectionMode::Override", EditConditionHides))
	FPCGExAssetGrammarDetails AssetGrammar;

#pragma region DEPRECATED

	/** LEGACY (schema v0). Migrated into AssetGrammar by PostLoad when SubGrammarMode==Override. */
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGExCollectionGrammarDetails CollectionGrammar_DEPRECATED;

#pragma endregion
	
#pragma endregion

#pragma region Fitting
	
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExEntryVariationMode VariationMode = EPCGExEntryVariationMode::None;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variations", EditCondition="!bIsSubCollection && VariationMode == EPCGExEntryVariationMode::Local", EditConditionHides, ShowOnlyInnerProperties))
	FPCGExFittingVariations Variations;

	/**
	 * Where this entry's Scale to Fit comes from when a staging node considers entry overrides.
	 * None = the node's settings apply; Local = this entry's; Global = the collection's.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExEntryVariationMode ScaleToFitSource = EPCGExEntryVariationMode::None;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Scale to Fit", EditCondition="!bIsSubCollection && ScaleToFitSource == EPCGExEntryVariationMode::Local", EditConditionHides))
	FPCGExLeanScaleToFitDetails ScaleToFit;

	/**
	 * Where this entry's Justification comes from when a staging node considers entry overrides.
	 * None = the node's settings apply; Local = this entry's; Global = the collection's.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExEntryVariationMode JustificationSource = EPCGExEntryVariationMode::None;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Justification", EditCondition="!bIsSubCollection && JustificationSource == EPCGExEntryVariationMode::Local", EditConditionHides))
	FPCGExLeanJustificationDetails Justification;

	UPROPERTY(EditAnywhere, Category = Settings)
	TSet<FName> Tags;


	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	FPCGExAssetStagingData Staging;

#pragma endregion
	
	/**
	 * Subcollection to draw picks from when bIsSubCollection is enabled. Accepts any collection type;
	 * consuming nodes type-check resolved entries, so mixed-type nesting must be routed through type
	 * filters downstream. Kept populated when bIsSubCollection is toggled off so the ref survives iteration.
	 * Do not rename: legacy per-type entry members were also named "SubCollection", so their on-disk
	 * tags deserialize straight into this property.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection", EditConditionHides, DisplayAfter="bIsSubCollection"))
	TObjectPtr<UPCGExAssetCollection> SubCollection;


	// Subcollection Access

	virtual const UPCGExAssetCollection* GetSubCollectionPtr() const
	{
		return SubCollection;
	}

	virtual void ClearSubCollection()
	{
		SubCollection = nullptr;
	}

	/** Check if this is a valid subcollection entry */
	bool HasValidSubCollection() const
	{
		return bIsSubCollection && GetSubCollectionPtr() != nullptr;
	}


	// Typed Subcollection Access (Templates for convenience)

	template <typename T>
	T* GetSubCollection()
	{
		return Cast<T>(SubCollection);
	}

	template <typename T>
	const T* GetSubCollection() const
	{
		return Cast<T>(SubCollection);
	}


	// Variations & Grammar

	const FPCGExFittingVariations& GetVariations(const UPCGExAssetCollection* ParentCollection) const;

	/**
	 * Resolve this entry's Scale to Fit override. Collection-level Overrule wins first, then
	 * ScaleToFitSource (Local/Global). Returns nullptr when the consuming node's settings
	 * should be used (None).
	 */
	const FPCGExLeanScaleToFitDetails* GetScaleToFitOverride(const UPCGExAssetCollection* ParentCollection) const;

	/** Same contract as GetScaleToFitOverride, for justification. */
	const FPCGExLeanJustificationDetails* GetJustificationOverride(const UPCGExAssetCollection* ParentCollection) const;

	/**
	 * Return the grammar struct that applies to this entry given GrammarSource / SubGrammarMode /
	 * Overrule. Returns nullptr for Flatten or when a subcollection mode requires a missing
	 * subcollection. Cheap struct lookup -- callers needing only Symbol / DebugColor / Axes
	 * should prefer this over FixModuleInfos.
	 */
	const FPCGExAssetGrammarDetails* GetEffectiveGrammar(const UPCGExAssetCollection* Host) const;

	/**
	 * Resolve this entry's grammar module for the given axis. Routes to AssetGrammar / Host's
	 * GlobalAssetGrammar / the subcollection's SubCollectionGrammar based on GrammarSource and
	 * SubGrammarMode. Returns 0 when the resolved struct doesn't enable Axis, when the subcollection
	 * is in Flatten mode, or when no subcollection is bound. Caller may pass an optional SizeCache
	 * to deduplicate per-entry queries across recursive aggregation.
	 */
	double GetGrammarSize(
		const UPCGExAssetCollection* Host,
		EPCGExGrammarAxes Axis = EPCGExGrammarAxes::X,
		FPCGExGrammarSizeCache* SizeCache = nullptr) const;

	/**
	 * Resolve and populate OutModule for the given axis. Returns true when the resolved grammar
	 * struct enables Axis (Symbol/DebugColor/Size/bScalable written); false otherwise (OutModule
	 * untouched).
	 */
	bool FixModuleInfos(
		const UPCGExAssetCollection* Host,
		FPCGSubdivisionSubmodule& OutModule,
		EPCGExGrammarAxes Axis = EPCGExGrammarAxes::X,
		FPCGExGrammarSizeCache* SizeCache = nullptr) const;


	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection);
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive);
	virtual void PostUpdateStaging();
	virtual void SetAssetPath(const FSoftObjectPath& InPath);
	virtual void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize();

	/**
	 * Editor-only: paths whose on-disk updates should trigger a rebuild of this entry.
	 * Base returns Staging.Path. Override when the entry is driven by a *source* asset
	 * that differs from Staging.Path -- e.g. a level that gets exported into an embedded
	 * UPCGDataAsset living inside the collection package.
	 */
	virtual void EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const;

	/**
	 * Editor-only: the asset path the collection grid should use for this entry's
	 * thumbnail and for double-click "open asset" actions. Defaults to Staging.Path
	 * for leaves and to the live SubCollection reference for subcollection entries.
	 * Override when the user-facing source asset differs from Staging.Path -- e.g.
	 * level-sourced PCGDataAsset entries whose Staging.Path points at the embedded
	 * exported data asset rather than the authored UWorld.
	 */
	virtual FSoftObjectPath EDITOR_GetThumbnailAssetPath() const;
#endif


	// MicroCache (Per-entry cached data, e.g., material variants)

	TSharedPtr<PCGExAssetCollection::FMicroCache> MicroCache;
	virtual void BuildMicroCache();


	// Property Resolution

	/**
	 * Get resolved property by type: checks entry overrides first, then collection defaults.
	 * @param OwningCollection The collection this entry belongs to
	 * @param PropertyName Optional name filter (NAME_None matches first of type)
	 * @return Pointer to property if found, nullptr otherwise
	 */
	template <typename T>
	const T* GetResolvedProperty(const UPCGExAssetCollection* OwningCollection, FName PropertyName = NAME_None) const;

	/**
	 * Type-erased resolve: returns the FPCGExProperty base pointer for PropertyName,
	 * preferring enabled overrides on this entry, then falling back to collection defaults.
	 * Returns nullptr if the property isn't defined.
	 *
	 * Use this when you don't know (or don't care about) the concrete property type --
	 * typically in combination with TryGetPropertyValue<T> for type-erased value reads.
	 */
	const FPCGExProperty* GetResolvedPropertyBase(const UPCGExAssetCollection* OwningCollection, FName PropertyName) const;

	/**
	 * Read a property's effective value converted to T, regardless of the property's
	 * concrete type. Checks entry overrides first, then collection defaults, then
	 * dispatches through FPCGExProperty::TryGetValue (backed by FConversionTable).
	 *
	 * T must be a PCG-supported metadata type (see PCGExTypes::TTraits).
	 * Returns false if the property isn't defined for this name.
	 *
	 * Example:
	 *   double Out = 0.0;
	 *   if (Entry->TryGetPropertyValue<double>(Collection, TEXT("Weight"), Out)) { ... }
	 */
	template <typename T>
	bool TryGetPropertyValue(const UPCGExAssetCollection* OwningCollection, FName PropertyName, T& Out) const
	{
		if (const FPCGExProperty* Base = GetResolvedPropertyBase(OwningCollection, PropertyName))
		{
			return Base->TryGetValue(Out);
		}
		return false;
	}

	/**
	 * Check if this entry has an override for a specific property name.
	 */
	bool HasPropertyOverride(FName PropertyName) const
	{
		return PropertyOverrides.HasOverride(PropertyName);
	}

protected:
	void ClearManagedSockets();
};

namespace PCGExAssetCollection
{
	/**
	 * Claim-once bank of persistent EntryIds for GENERATED collections that rebuild their
	 * Entries from scratch. External references (variant collections) bind entries by
	 * EntryId; a rebuild that mints all-new ids silently unbinds them.
	 *
	 * Deposit every previous entry's id under caller-defined identity keys before
	 * regenerating, then claim while writing the new entries. Two tiers: exact (full
	 * content/definition identity -- claim FIRST, for every entry) and loose (coarser
	 * identity, e.g. primary asset path, so a binding survives a content tweak -- claim only
	 * for entries the exact pass left at 0; ordering ensure-enforced). Pass 0 for either key
	 * to skip that tier. Each deposited id is handed out at most once, in deposit order per
	 * key. Unclaimed entries keep EntryId 0 and receive fresh ids from the post-rebuild
	 * SyncEntryIds pass. Keys only need to be stable within the rebuild call.
	 */
	class PCGEXCOLLECTIONS_API FEntryIdBank
	{
		struct FDeposit
		{
			int32 EntryId = 0;
			bool bClaimed = false;
		};

		TArray<FDeposit> Deposits;
		TMap<uint32, TArray<int32>> ExactToDeposits;
		TMap<uint32, TArray<int32>> LooseToDeposits;
		bool bLooseClaimStarted = false;

	public:
		/** Deposit one id under the two identity tiers. Zero id is ignored; a zero key skips that tier. */
		void Deposit(uint32 InExactKey, uint32 InLooseKey, int32 InEntryId);

		/** Deposit every non-zero EntryId of InCollection; InKeyFunc returns the exact key per entry (0 = skip). No loose tier. */
		void Deposit(const UPCGExAssetCollection* InCollection, TFunctionRef<uint32(const FPCGExAssetCollectionEntry&, int32 Index)> InKeyFunc);

		/** Pop the oldest unclaimed id deposited under the exact key. 0 when exhausted or unknown. */
		int32 ClaimExact(uint32 InExactKey);

		/** Pop the oldest unclaimed id deposited under the loose key. 0 when exhausted or unknown. */
		int32 ClaimLoose(uint32 InLooseKey);
	};

	/**
	 * Per-entry cache for weighted sub-selections within a single entry.
	 * Used when an entry has multiple variants (e.g. material overrides on a mesh,
	 * point weights on a data asset). Provides the same pick modes as FCategory
	 * (ascending, descending, random, weighted random).
	 *
	 * To create a custom MicroCache:
	 * 1. Subclass FMicroCache, override GetTypeId()
	 * 2. Add a Process*() method that calls BuildFromWeights() with your weight array
	 * 3. Override BuildMicroCache() in your entry struct to create and populate it
	 * 4. Add a typed accessor (e.g. GetMyMicroCache()) on your entry struct
	 */
	class PCGEXCOLLECTIONS_API FMicroCache : public TSharedFromThis<FMicroCache>
	{
	protected:
		double WeightSum = 0;
		TArray<int32> Weights;
		TArray<int32> Order;

	public:
		FMicroCache() = default;
		virtual ~FMicroCache() = default;

		virtual FTypeId GetTypeId() const
		{
			return TypeIds::None;
		}

		bool IsEmpty() const
		{
			return Order.IsEmpty();
		}

		int32 Num() const
		{
			return Order.Num();
		}

		int32 GetPick(int32 Index, EPCGExIndexPickMode PickMode) const;
		int32 GetPickAscending(int32 Index) const;
		int32 GetPickDescending(int32 Index) const;
		int32 GetPickWeightAscending(int32 Index) const;
		int32 GetPickWeightDescending(int32 Index) const;
		int32 GetPickRandom(int32 Seed) const;
		int32 GetPickRandomWeighted(int32 Seed) const;

	protected:
		/** Initialize from weight array. Call from derived class. */
		void BuildFromWeights(TConstArrayView<int32> InWeights);
	};

	/**
	 * Groups entries sharing the same Category FName. Maintains its own weight-sorted
	 * index array for efficient pick operations. The "Main" category in FCache contains
	 * all valid entries regardless of name. Named categories enable filtered picking
	 * (e.g. "Rocks", "Trees") without building separate collections.
	 */
	class PCGEXCOLLECTIONS_API FCategory : public TSharedFromThis<FCategory>
	{
	public:
		FName Name = NAME_None;
		double WeightSum = 0;
		TArray<int32> Indices;
		TArray<int32> Weights;
		TArray<int32> Order;
		TArray<const FPCGExAssetCollectionEntry*> Entries;

		FCategory() = default;

		explicit FCategory(FName InName)
			: Name(InName)
		{
		}

		~FCategory() = default;

		FORCEINLINE bool IsEmpty() const
		{
			return Order.IsEmpty();
		}

		FORCEINLINE int32 Num() const
		{
			return Order.Num();
		}

		int32 GetPick(int32 Index, EPCGExIndexPickMode PickMode) const;
		int32 GetPickAscending(int32 Index) const;
		int32 GetPickDescending(int32 Index) const;
		int32 GetPickWeightAscending(int32 Index) const;
		int32 GetPickWeightDescending(int32 Index) const;
		int32 GetPickRandom(int32 Seed) const;
		int32 GetPickRandomWeighted(int32 Seed) const;

		void Reserve(int32 InNum);
		void Shrink();
		void RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry);
		void Compile();
	};

	/**
	 * Top-level cache built from the collection's Entries array. Contains one "Main"
	 * category (all valid entries) plus named sub-categories. Built lazily on first
	 * access via LoadCache(). Thread-safe (guarded by FRWLock on the collection).
	 */
	class PCGEXCOLLECTIONS_API FCache : public TSharedFromThis<FCache>
	{
	public:
		int32 WeightSum = 0;
		TSharedPtr<FCategory> Main;

		// Dense array of named sub-categories, in registration order. Indexed by the value side
		// of CategoryNameToIndex, which is the canonical name -> slot lookup.
		TArray<TSharedPtr<FCategory>> Categories;

		// Name -> index into Categories. Populated incrementally by RegisterEntry; stable for
		// the cache's lifetime. Consumers can maintain parallel TArrays keyed by the same index
		// to avoid FName hash lookups on the hot path.
		TMap<FName, int32> CategoryNameToIndex;

		// Flattened set of all collections transitively reachable from this one (self + every
		// subcollection returnable as a Host from GetEntry). Built during BuildCacheFromEntries
		// via a cycle-safe tree walk. Consumed by FPickPacker bulk registration to precompute
		// collection→GUID mappings without per-point lock contention.
		TArray<TObjectPtr<UPCGExAssetCollection>> FlatHosts;

		FCache();
		~FCache() = default;

		FORCEINLINE bool IsEmpty() const
		{
			return Main ? Main->IsEmpty() : true;
		}

		void Compile();
		void RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry);
	};
}

/**
 * Abstract base for all PCGEx asset collections. A collection is a UDataAsset containing
 * a typed array of entries, each pointing to an asset (mesh, actor, data asset, etc.)
 * or recursively to another subcollection of the same type.
 *
 * Architecture overview:
 *   UPCGExAssetCollection (UDataAsset)
 *     └─ TArray<FMyEntry> Entries          -- the authored list
 *     └─ FCache (built lazily)
 *         ├─ FCategory "Main"              -- all valid entries, weight-sorted
 *         └─ FCategory per unique name     -- entries grouped by Category FName
 *             └─ per entry: FMicroCache    -- optional sub-selections (material variants, etc.)
 *
 * Creating a custom collection type:
 * 1. Create your entry struct (see FPCGExAssetCollectionEntry doc)
 * 2. Subclass UPCGExAssetCollection
 * 3. Add PCGEX_ASSET_COLLECTION_BODY(FMyEntry) in the class body -- this implements
 *    all required virtual functions (IsValidIndex, NumEntries, BuildCache, ForEachEntry, etc.)
 * 4. Override GetTypeId() to return your registered FTypeId
 * 5. Add your TArray<FMyEntry> Entries UPROPERTY
 * 6. Register your type with PCGEX_REGISTER_COLLECTION_TYPE in your .cpp file
 * 7. Optionally override EDITOR_AddBrowserSelectionInternal for drag-drop support
 *
 * Picking API (all methods handle subcollection recursion automatically):
 * - GetEntryAt(Index)           -- direct index access
 * - GetEntry(Index, Seed, Mode) -- pick by mode (ascending/descending/weight-sorted)
 * - GetEntryRandom(Seed)        -- uniform random
 * - GetEntryWeightedRandom(Seed)-- weighted random
 * All return FPCGExEntryAccessResult with entry + host collection.
 */
UCLASS(Abstract, BlueprintType, DisplayName="[PCGEx] Asset Collection")
class PCGEXCOLLECTIONS_API UPCGExAssetCollection : public UDataAsset, public IPCGExCookDependencyProvider
{
	mutable FRWLock CacheLock;

	GENERATED_BODY()

	friend struct FPCGExAssetCollectionEntry;

public:
	UPCGExAssetCollection()
	{
		CollectionGUID = GenerateNewGUID();
	}

#pragma region Type

	/** Get the type ID of this collection */
	virtual PCGExAssetCollection::FTypeId GetTypeId() const
	{
		return PCGExAssetCollection::TypeIds::Base;
	}

	/** Check if this collection is of a specific type */
	bool IsType(PCGExAssetCollection::FTypeId TypeId) const
	{
		return PCGExAssetCollection::FTypeRegistry::Get().IsA(GetTypeId(), TypeId);
	}

	/**
	 * True when this host runs TypeId's cross-entry collection machinery (post-rebuild
	 * passes, save-time lifecycle -- e.g. the PCGDataAsset shared-collection compaction).
	 * Entry code gating on machinery must use this, never a concrete host cast.
	 * Base: only the native type lineage runs its own machinery. Heterogeneous hosts
	 * override to answer for every type with a registered state class.
	 */
	virtual bool SupportsTypeMachinery(PCGExAssetCollection::FTypeId TypeId) const
	{
		return IsType(TypeId);
	}

	/**
	 * First machinery state of (or derived from) StateClass this host owns; null when it
	 * has none. Accessor semantics (one-directional IsChildOf on purpose): the result IS-A
	 * StateClass, so the typed template below stays cast-safe. Hosts with per-type
	 * machinery override this (Omni: TypeStates array; typed collections: their owned
	 * state) -- the generic way to reach a host's machinery state without concrete casts.
	 */
	virtual UPCGExCollectionTypeState* FindTypeState(const UClass* StateClass) const
	{
		return nullptr;
	}

	template <typename T>
	T* FindTypeState() const
	{
		return static_cast<T*>(FindTypeState(T::StaticClass()));
	}

#pragma endregion

#pragma region Type Globals

	/**
	 * Copy this host's globals block matching T into OutGlobals; false when the host has no
	 * such block (callers fall back to entry-local values). This is how entries read
	 * collection-level settings -- NEVER cast a host collection to a concrete class for them.
	 * Copy-out by design: no lifetime coupling, any-thread safe. Called per resolved entry;
	 * do not introduce per-point calls.
	 */
	template <typename T>
	bool GetTypeGlobals(T& OutGlobals) const
	{
		static_assert(std::is_base_of_v<FPCGExCollectionTypeGlobals, T>, "T must derive from FPCGExCollectionTypeGlobals");
		return GetTypeGlobalsInternal(T::StaticStruct(), OutGlobals);
	}

	/** Type-erased flavor. OutGlobals' concrete type must match or derive from StructType. */
	bool GetTypeGlobals(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
	{
		return GetTypeGlobalsInternal(StructType, OutGlobals);
	}

	/**
	 * Globals-block struct types this host can answer GetTypeGlobals for. Base: the
	 * collection's registered GlobalsStruct; heterogeneous hosts: each stored block's
	 * concrete struct. Consumed by conversion/merge.
	 */
	virtual void GetTypeGlobalsStructs(TArray<const UScriptStruct*>& OutStructs) const;

protected:
	/**
	 * Fill OutGlobals if this collection provides a block StructType matches or derives from.
	 * Unhandled types must route to Super so provider chains compose. Base provides none.
	 */
	virtual bool GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
	{
		return false;
	}

public:
#pragma endregion

#pragma region Cache

	PCGExAssetCollection::FCache* LoadCache();
	virtual void InvalidateCache();
	virtual void BuildCache();

	/** Flattened set of all collections transitively reachable from this one (self + subcollection Hosts). */
	const TArray<TObjectPtr<UPCGExAssetCollection>>& GetFlatHosts()
	{
		return LoadCache()->FlatHosts;
	}

#pragma endregion

#pragma region API

	/** Get entry at cache-adjusted index (0 = first valid entry, 1 = second, etc.) */
	FPCGExEntryAccessResult GetEntryAt(int32 Index) const;

	/** Get entry by raw Entries array index (bypasses cache). Use for indices from FCategory, packed hashes, etc. */
	FPCGExEntryAccessResult GetEntryRaw(int32 RawIndex) const;

	/** Mutable access to entry at raw array index (bypasses cache). For programmatic mutation
	 *  (staging pipeline hooks, the entry Blueprint library). Caller owns Modify /
	 *  MarkPackageDirty / InvalidateCache as appropriate for what it mutates. */
	FPCGExAssetCollectionEntry* GetMutableEntryRaw(int32 RawIndex)
	{
		return GetMutableEntryAtRawIndex(RawIndex);
	}

#if WITH_EDITOR
	/** Editor-only mutable access to entry at raw array index. For editor UI direct writes. */
	FPCGExAssetCollectionEntry* EDITOR_GetMutableEntry(int32 Index)
	{
		return GetMutableEntryAtRawIndex(Index);
	}

	/**
	 * Concrete script struct of the entry payload at the given raw index (base: the Entries
	 * inner struct; heterogeneous collections: per row). Null when the row has no payload.
	 * Editor UI must use this instead of reflecting the Entries array inner type.
	 */
	virtual const UScriptStruct* EDITOR_GetEntryScriptStruct(int32 RawIndex) const;

	/**
	 * Payload types a grid "+ Add" affordance should offer. Base leaves it empty (untyped
	 * adds); heterogeneous collections fill it so the UI presents a type choice.
	 */
	virtual void EDITOR_GetAddableEntryTypes(TArray<const UScriptStruct*>& OutTypes) const
	{
	}

	/**
	 * Append one default-initialized entry; null EntryStruct = the native entry type. Base
	 * accepts the Entries inner struct or a base of it (element is created NATIVE, caller
	 * copies the requested portion); Omni accepts any entry-derived payload. Null on
	 * rejection -- doubles as the compatibility arbiter for cross-collection transfers.
	 * Caller owns transaction/Modify/PostEditChange.
	 */
	virtual FPCGExAssetCollectionEntry* EDITOR_AddEntry(const UScriptStruct* EntryStruct = nullptr);
#endif

	/** Get entry by index with pick mode */
	FPCGExEntryAccessResult GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode) const;

	/** Get random entry (uniform distribution) */
	FPCGExEntryAccessResult GetEntryRandom(int32 Seed) const;

	/** Get random entry (weighted by entry Weight property) */
	FPCGExEntryAccessResult GetEntryWeightedRandom(int32 Seed) const;

	// With tag inheritance
	FPCGExEntryAccessResult GetEntryAt(int32 Index, uint8 TagInheritance, TSet<FName>& OutTags) const;
	FPCGExEntryAccessResult GetEntryRaw(int32 RawIndex, uint8 TagInheritance, TSet<FName>& OutTags) const;
	FPCGExEntryAccessResult GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode, uint8 TagInheritance, TSet<FName>& OutTags) const;
	FPCGExEntryAccessResult GetEntryRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const;
	FPCGExEntryAccessResult GetEntryWeightedRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const;

#pragma endregion

#pragma region Enumeration

	/** Check if index is valid in the entries array */
	virtual bool IsValidIndex(int32 InIndex) const
	{
		return false;
	}

	/** Get total number of entries */
	virtual int32 NumEntries() const
	{
		return 0;
	}

	/** Get number of valid (non-zero weight) entries */
	virtual int32 GetValidEntryNum()
	{
		return LoadCache()->Main->Indices.Num();
	}

	/** Initialize entries array to given size */
	virtual void InitNumEntries(int32 Num) PCGEX_NOT_IMPLEMENTED(InitNumEntries)

	/** ForEach iteration (const) */
	using FForEachConstEntryFunc = TFunctionRef<void(const FPCGExAssetCollectionEntry*, int32)>;

	virtual void ForEachEntry(FForEachConstEntryFunc Iterator) const
	{
	}

	/** ForEach iteration (mutable) */
	using FForEachEntryFunc = TFunctionRef<void(FPCGExAssetCollectionEntry*, int32)>;

	virtual void ForEachEntry(FForEachEntryFunc Iterator)
	{
	}

	/** Sort */
	using FSortEntriesFunc = TFunctionRef<bool(const FPCGExAssetCollectionEntry* A, const FPCGExAssetCollectionEntry* B)>;

	virtual void Sort(FSortEntriesFunc Predicate)
	{
	}

#pragma endregion

	void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths, PCGExAssetCollection::ELoadingFlags Flags) const;

	/**
	 * Gather the soft object paths referenced by this collection's custom properties --
	 * collection-level CollectionProperties plus every entry's PropertyOverrides. Runtime-safe
	 * (not editor-gated) so preloading can warm property-referenced assets at runtime; the
	 * editor cook-dependency walk shares the same extraction.
	 */
	void GatherPropertySoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const;

#if WITH_EDITOR
	//~ Begin IPCGExCookDependencyProvider
	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	//~ End IPCGExCookDependencyProvider
#endif

#pragma region Lifecycle

	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

	/**
	 * Re-stage every entry (SyncEntryIds -> per-entry UpdateStaging/PostUpdateStaging ->
	 * cache invalidation). Entries whose Staging.bAuthored is set keep their staging content
	 * (their UpdateStaging only refreshes identity fields) -- id sync and cache invalidation
	 * still run collection-wide.
	 */
	void RebuildStagingData(bool bRecursive);
	void EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const;

	/** Rebuild property registry from CollectionProperties. Called automatically during cache build. */
	void RebuildPropertyRegistry()
	{
		TArray<FInstancedStruct> Schema = CollectionProperties.BuildSchema();
		PCGExProperties::BuildRegistry(Schema, PropertyRegistry);
	}

	/**
	 * Sync CollectionProperties' schemas and propagate any HeaderId remaps to every entry's
	 * PropertyOverrides. Returns true if a remap occurred (caller may want MarkPackageDirty).
	 */
	bool SyncPropertySchemaAndRemapEntries();

	/**
	 * Derive CollectionProperties from the union of every entry's currently-enabled
	 * PropertyOverrides, then re-sync each entry's overrides against the resulting
	 * canonical schema (preserves values via HeaderId).
	 *
	 * Use this when per-entry overrides are the source of truth and the collection-level
	 * schema needs to mirror them. Counterpart to UPCGExActorCollection's actor-component
	 * scan, which builds the schema from the component side. Callers:
	 *
	 *   - SharedMeshCollection after CompactSharedMesh has aggregated per-entry mesh
	 *     contributions (each contribution may carry its own PropertyOverrides extracted
	 *     from the source actor's UPCGExPropertyCollectionComponent).
	 *   - Anywhere else a collection's schema should be reconstructed from the data
	 *     authored on individual entries.
	 *
	 * Source ordering under FirstWins / StrictTypeMatch:
	 *   1. InheritedDefaults (if any)  -- highest priority. Caller-computed "common-ancestor"
	 *      view: the value contributing actors would inherit if they didn't author per-instance
	 *      overrides (typically the BP CDO's value when all actors share a class; the asset's
	 *      default when CDOs disagree).
	 *   2. Per-entry contributors (each entry's enabled override slots).
	 *   3. Existing CollectionProperties -- lowest priority, survives only when the entry above
	 *      does not contribute the property (manual-only schema entries).
	 *
	 * @param Policy            Conflict-resolution policy applied during the merge. Defaults to
	 *                          StrictTypeMatch: silent dedupe on same-name+same-type, conflict log
	 *                          on type mismatch.
	 * @param InheritedDefaults Optional caller-computed inherited-defaults view. When provided,
	 *                          becomes source #0 in the merge so the collection default snaps to
	 *                          the common-ancestor value across contributors rather than to
	 *                          whichever contributor was iterated first. Pass empty (default) to
	 *                          fall back to the old "first contributor wins" behavior.
	 */
	void RefreshCollectionPropertiesFromEntries(
		EPCGExSchemaMergePolicy Policy = EPCGExSchemaMergePolicy::StrictTypeMatch,
		TConstArrayView<FInstancedStruct> InheritedDefaults = {});

	/**
	 * Get property from collection defaults by type.
	 * @param PropertyName Optional name filter (NAME_None matches first of type)
	 * @return Pointer to property if found, nullptr otherwise
	 */
	template <typename T>
	const T* GetProperty(FName PropertyName = NAME_None) const
	{
		return CollectionProperties.GetProperty<T>(PropertyName);
	}

	/**
	 * Check if collection has a property with given name.
	 */
	bool HasProperty(FName PropertyName) const
	{
		return CollectionProperties.HasProperty(PropertyName);
	}

	bool HasCircularDependency(const UPCGExAssetCollection* OtherCollection) const;
	bool HasCircularDependency(TSet<const UPCGExAssetCollection*>& InReferences) const;

	/**
	 * Assigns a unique non-zero EntryId to every entry that has none, and re-assigns
	 * copy-paste-introduced duplicates (first-seen entry keeps its id, so external
	 * references keep resolving to the original). Runs at the top of RebuildStagingData
	 * so identity is settled before any derived data (e.g. variant bakes) reads it.
	 */
	void SyncEntryIds();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	virtual void EDITOR_RebuildStagingData();

	UFUNCTION()
	virtual void EDITOR_RebuildStagingData_Recursive();

	UFUNCTION()
	virtual void EDITOR_RebuildStagingData_Project();

	void EDITOR_SanitizeAndRebuildStagingData(bool bRecursive);
	void EDITOR_AddBrowserSelectionTyped(const TArray<FAssetData>& InAssetData);

	/**
	 * Append one entry per input subcollection. Each new entry has bIsSubCollection = true
	 * and its base SubCollection property pointed at the input collection -- any collection
	 * type is accepted. Skips inputs that would create a cycle, that point at self, or that
	 * are already referenced by an existing subcollection entry.
	 * Does NOT open a transaction or mark dirty -- caller is responsible (matches the
	 * EDITOR_AddBrowserSelectionTyped contract). Base reflects over a homogeneous `Entries`
	 * array; wrapper-row collections (Omni) override.
	 */
	virtual void EDITOR_AddSubCollectionEntries(const TArray<UPCGExAssetCollection*>& InSubCollections);

	/**
	 * Post-rebuild extension point. Called once at the tail of any user-triggered editor
	 * rebuild path (single entry, full, recursive, or stale-entry batch) AFTER all entries
	 * have had UpdateStaging applied and the cache has been invalidated.
	 *
	 * Subclasses override this when they need to run cross-entry work that depends on every
	 * entry's freshly-staged state -- typically post-processing that's too expensive to fold
	 * into per-entry UpdateStaging without N² blowup. Default implementation is a no-op.
	 *
	 * The hook is automatically suppressed inside batch loops (e.g. EDITOR_RebuildStaleEntries
	 * calling EDITOR_RebuildEntryStaging per stale index) and fires once at the batch end.
	 */
	virtual void EDITOR_OnPostStagingRebuild()
	{
	}

	/** Re-stage a single entry. Mirrors the dirty/broadcast behaviour of editing the entry's properties so UI refreshes. Returns true if the entry was rebuilt. */
	bool EDITOR_RebuildEntryStaging(int32 EntryIndex);

	/** Walks entries and re-stages any whose referenced asset's file mtime is newer than LastRebuiltUtc. Per-entry scope (not a full rebuild). No-op if LastRebuiltUtc is MinValue. Returns the number of entries re-staged. */
	int32 EDITOR_RebuildStaleEntries();

	/** Sync PropertyOverrides in all entries to match CollectionProperties schema */
	void SyncPropertyOverridesToEntries();

protected:
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData);

	void EDITOR_SetDirty()
	{
		Cache.Reset();
		bCacheNeedsRebuild = true;
		InvalidateCache();
	}

	/** Tail of every user-triggered rebuild session (depth 0 only): runs the native
	 *  EDITOR_OnPostStagingRebuild virtual first, then the staging pipelines' OnPostRebuild,
	 *  so the pipelines see post-merge/post-compaction state. Must stay outside the virtual --
	 *  overrides are not required to call Super. */
	void EDITOR_FinalizeStagingRebuild();

	/** Render the entry mosaic and cache it into the package thumbnail map so it survives editor
	 *  restarts -- the pool's live render is session-only and data assets get no engine save-time
	 *  bake. No-op outside the editor, mid asset-registry scan, or for empty/transient collections. */
	void EDITOR_BakeThumbnailToPackage();

	/** Staging pipeline hook dispatchers. Run every valid pipeline in array order; no-op when
	 *  none are assigned, when invoked re-entrantly from inside another hook, or while cooking. */
	void EDITOR_DispatchPipelinePreRebuild();
	void EDITOR_DispatchPipelineEntry(int32 EntryIndex, bool bIsSubCollection);
	void EDITOR_DispatchPipelinePostRebuild();

	/** True when at least one StagingPipelines slot holds a valid pipeline. */
	bool EDITOR_HasAnyStagingPipeline() const;
#endif

	static uint32 GenerateNewGUID()
	{
		return GetTypeHash(FGuid::NewGuid());
	}

#pragma endregion

public:
	// Properties

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, MultiLine))
	FString Notes;

	/** Optional post-process pipelines invoked around editor staging rebuilds (once before the
	 *  session, once per re-staged entry, once after the native post-rebuild work). Composable:
	 *  pipelines run in array order at every hook point, so later pipelines see earlier ones'
	 *  mutations; null entries are skipped. Use them to drive per-entry property overrides,
	 *  tags, weights etc. from Blueprint or C++.
	 *  Editor-only: never serialized into or executed by cooked targets. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TArray<TObjectPtr<UPCGExCollectionStagingPipeline>> StagingPipelines;

	/** LEGACY single-pipeline slot. Migrated into StagingPipelines by PostLoad. */
	UPROPERTY(Instanced, meta=(DeprecatedProperty, ScriptNoExport))
	TObjectPtr<UPCGExCollectionStagingPipeline> StagingPipeline_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1))
	TSet<FName> CollectionTags;

	/** Persistent unique identifier for this collection. Used by FPickPacker to produce
	 *  deterministic, mergeable hashes across different staging nodes and sessions.
	 *  Generated once in the constructor; new value assigned on duplication/import. */
	UPROPERTY(VisibleAnywhere, Category = Settings, AdvancedDisplay, meta=(IgnoreForMemberInitializationTest))
	uint32 CollectionGUID = 0;

	uint32 GetCollectionGUID() const
	{
		return CollectionGUID;
	}

#if WITH_EDITORONLY_DATA
	bool bSuppressStagingRebuild = false;

	/** Set at the end of every full rebuild (EDITOR_RebuildStagingData / _Recursive) to UtcNow.
	 *  Used by EDITOR_RebuildStaleEntries to detect entries whose referenced asset's file mtime
	 *  is newer than this -- i.e. modified since the last collection-wide rebuild.
	 *  MinValue means "no baseline yet" -- the stale check is skipped entirely until the user
	 *  triggers a manual rebuild once. Per-entry rebuilds do NOT update this field, otherwise
	 *  they'd mask staleness in unrelated entries that haven't been re-staged. */
	UPROPERTY()
	FDateTime LastRebuiltUtc = FDateTime::MinValue();
#endif

	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	EPCGExGlobalVariationRule GlobalVariationMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	FPCGExFittingVariations GlobalVariations;

	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	EPCGExGlobalVariationRule GlobalGrammarMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	FPCGExAssetGrammarDetails GlobalAssetGrammar = FPCGExAssetGrammarDetails(FName("N/A"));

	/** Collection-level rule for entry Scale to Fit overrides: let entries choose (PerEntry) or force GlobalScaleToFit on all of them (Overrule). */
	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	EPCGExGlobalVariationRule GlobalScaleToFitMode = EPCGExGlobalVariationRule::PerEntry;

	/** Collection-level Scale to Fit, consumed by entries whose ScaleToFitSource is Global (or by all entries when Overrule). */
	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	FPCGExLeanScaleToFitDetails GlobalScaleToFit;

	/** Collection-level rule for entry Justification overrides: let entries choose (PerEntry) or force GlobalJustification on all of them (Overrule). */
	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	EPCGExGlobalVariationRule GlobalJustificationMode = EPCGExGlobalVariationRule::PerEntry;

	/** Collection-level Justification, consumed by entries whose JustificationSource is Global (or by all entries when Overrule). */
	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	FPCGExLeanJustificationDetails GlobalJustification;

	/**
	 * This collection's identity as a grammar module when it is used as a subcollection entry
	 * elsewhere (resolved when the parent entry has SubGrammarMode == Inherit). Per-axis like
	 * any other grammar struct; customization gates EPCGExGrammarAxisSize values for the
	 * subcollection-aggregation set (Min/Max/Average + Fixed).
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExAssetGrammarDetails SubCollectionGrammar;

#pragma region DEPRECATED

	/** LEGACY (schema v0). Migrated into SubCollectionGrammar by PostLoad. */
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGExCollectionGrammarDetails CollectionGrammar_DEPRECATED;

#pragma endregion

	/** Versioned grammar schema. PostLoad migrates legacy data to the current version. 0 = pre-v1 layout. */
	UPROPERTY()
	int32 GrammarSchemaVersion = 0;

	/** Versioned fitting/variations schema. PostLoad migrates legacy data to the current version. 0 = pre-opt-in entry variations. */
	UPROPERTY()
	int32 FittingSchemaVersion = 0;

	UPROPERTY(EditAnywhere, Category = "Settings|Utils")
	bool bDoNotIgnoreInvalidEntries = false;

	/**
	 * How an entry that is itself a subcollection computes its aggregate Staging.Bounds
	 * (extents, centered at origin). Consumed by selectors that reason about entry size.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExSubcollectionBoundsMode SubcollectionBoundsMode = EPCGExSubcollectionBoundsMode::UnionAABB;

	/**
	 * Collection-level properties with default values.
	 * Entries inherit these unless they provide overrides.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExPropertySchemaCollection CollectionProperties;

	/**
	 * Read-only registry of available properties (built from CollectionProperties).
	 * Used for UI display and validation.
	 */
	UPROPERTY()
	TArray<FPCGExPropertyRegistryEntry> PropertyRegistry;

protected:
	// Internal - Override in derived classes

	/** Get entry at raw array index (not cache-adjusted). Must override. */
	virtual const FPCGExAssetCollectionEntry* GetEntryAtRawIndex(int32 Index) const
	{
		return nullptr;
	}

	/** Get mutable entry at raw array index. Must override. */
	virtual FPCGExAssetCollectionEntry* GetMutableEntryAtRawIndex(int32 Index)
	{
		return nullptr;
	}

	/** Build cache from entries. Call with your Entries array. */
	template <typename T>
	bool BuildCacheFromEntries(TArray<T>& InEntries);

	/**
	 * Non-template cache build over base-entry pointers -- the shared core the typed template
	 * delegates to. Null pointers are tolerated and skipped while still consuming their raw
	 * index (heterogeneous collections may carry unset rows).
	 */
	bool BuildCacheFromEntryPtrs(TConstArrayView<FPCGExAssetCollectionEntry*> InEntries);

	UPROPERTY()
	bool bCacheNeedsRebuild = true;

	TSharedPtr<PCGExAssetCollection::FCache> Cache;

#if WITH_EDITOR
	/** Reentrance depth for suppressing EDITOR_OnPostStagingRebuild mid-batch. Incremented
	 *  by one per nested batch scope (TGuardValue pattern); hook fires only when it returns
	 *  to zero. Kept as int32 rather than bool so future nested batch calls work without
	 *  API changes. */
	int32 EDITOR_PostStagingRebuildSuppressDepth = 0;

	/** True while a staging pipeline hook executes (TGuardValue pattern). Rebuilds triggered
	 *  from inside a hook (e.g. a scripted edit routing through PostEditChangeProperty ->
	 *  EDITOR_RebuildStagingData) run normally but without re-firing pipeline hooks,
	 *  preventing infinite recursion. Transient. */
	bool bEDITOR_PipelineDispatchGuard = false;
#endif
};

// Validates each entry, registers valid ones to the cache (Main + named categories),
// triggers MicroCache builds, and compiles weight-sorted indices.
// Thin typed adapter -- the actual build lives in BuildCacheFromEntryPtrs (all per-entry
// calls it makes are virtual on the base entry, so the pointer view loses nothing).
template <typename T>
bool UPCGExAssetCollection::BuildCacheFromEntries(TArray<T>& InEntries)
{
	TArray<FPCGExAssetCollectionEntry*> EntryPtrs;
	EntryPtrs.Reserve(InEntries.Num());
	for (T& Entry : InEntries)
	{
		EntryPtrs.Add(&Entry);
	}
	return BuildCacheFromEntryPtrs(EntryPtrs);
}

// Boilerplate Macro

/**
 * Implements required virtual functions for a collection class.
 * Place in the class body after GENERATED_BODY()
 * 
 * Usage:
 *   UCLASS()
 *   class UMyCollection : public UPCGExAssetCollection
 *   {
 *       GENERATED_BODY()
 *       PCGEX_ASSET_COLLECTION_BODY(FMyCollectionEntry)
 *       
 *       UPROPERTY(...)
 *       TArray<FMyCollectionEntry> Entries;
 *   };
 */
#define PCGEX_ASSET_COLLECTION_BODY(_ENTRY_TYPE) \
public: \
	virtual bool IsValidIndex(int32 InIndex) const override { return Entries.IsValidIndex(InIndex); } \
	virtual int32 NumEntries() const override { return Entries.Num(); } \
	virtual void InitNumEntries(int32 InNum) override { PCGExArrayHelpers::InitArray(Entries, InNum); } \
	virtual void BuildCache() override { BuildCacheFromEntries(Entries); } \
	virtual void ForEachEntry(FForEachConstEntryFunc Iterator) const override \
	{ for (int32 i = 0; i < Entries.Num(); i++) { Iterator(&Entries[i], i); } } \
	virtual void ForEachEntry(FForEachEntryFunc Iterator) override \
	{ for (int32 i = 0; i < Entries.Num(); i++) { Iterator(&Entries[i], i); } } \
	virtual void Sort(FSortEntriesFunc Predicate) override \
	{ Entries.Sort([&](const _ENTRY_TYPE& A, const _ENTRY_TYPE& B)\
	{ return Predicate(static_cast<const FPCGExAssetCollectionEntry*>(&A), static_cast<const FPCGExAssetCollectionEntry*>(&B)); }); }\
protected: \
	virtual const FPCGExAssetCollectionEntry* GetEntryAtRawIndex(int32 Index) const override \
	{ return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr; } \
	virtual FPCGExAssetCollectionEntry* GetMutableEntryAtRawIndex(int32 Index) override \
	{ return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr; }

// Entry Property Resolution Implementation
template <typename T>
const T* FPCGExAssetCollectionEntry::GetResolvedProperty(const UPCGExAssetCollection* OwningCollection, FName PropertyName) const
{
	// Check entry overrides first
	if (const T* Override = PropertyOverrides.GetProperty<T>(PropertyName))
	{
		return Override;
	}

	// Fall back to collection defaults
	if (OwningCollection)
	{
		return OwningCollection->GetProperty<T>(PropertyName);
	}

	return nullptr;
}
