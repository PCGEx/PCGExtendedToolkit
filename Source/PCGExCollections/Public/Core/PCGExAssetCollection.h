// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PCGExAssetCollectionTypes.h"
#include "PCGExAssetGrammar.h"
#include "PCGExProperty.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExSocket.h"
#include "Details/PCGExStagingDetails.h"
#include "Fitting/PCGExFittingVariations.h"
#include "Helpers/PCGExStreamingHelpers.h"

#include "PCGExAssetCollection.generated.h"

#if WITH_EDITOR
struct FAssetData;
#endif

class UPCGExAssetCollection;

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

	FORCEINLINE operator bool() const { return Entry != nullptr; }
	FORCEINLINE bool IsValid() const { return Entry != nullptr; }

	template <typename T>
	FORCEINLINE const T* As() const { return static_cast<const T*>(Entry); }

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

	template <typename T>
	T* LoadSync(FPCGExContext* InContext = nullptr) const
	{
		TSoftObjectPtr<T> SoftObjectPtr = TSoftObjectPtr<T>(Path);
		PCGExHelpers::LoadBlocking_AnyThreadTpl<T>(SoftObjectPtr, InContext);
		return SoftObjectPtr.Get();
	}

	template <typename T>
	T* TryGet() const { return TSoftObjectPtr<T>(Path).Get(); }

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
 * 3. Override GetSubCollectionPtr() / ClearSubCollection() if you have a typed SubCollection
 * 4. Override Validate() to reject invalid entries (call Super)
 * 5. Override UpdateStaging() to populate Staging.Bounds and Staging.Path from your asset
 * 6. Override SetAssetPath() to update your TSoftObjectPtr from a path
 * 7. Override EDITOR_Sanitize() to sync InternalSubCollection from your typed SubCollection
 * 8. Optionally override BuildMicroCache() for per-entry sub-selections (e.g. material variants)
 *
 * Key properties inherited:
 * - Weight: pick probability (0 = excluded from cache)
 * - Category: named group for category-based picking
 * - Tags: arbitrary FName set, inheritable through subcollection hierarchy
 * - Variations: per-entry fitting transforms (scale/rotation randomization)
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

	// Core Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, ClampMin=0, UIMin=0))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, Category = Settings)
	FName Category = NAME_None;

	UPROPERTY(EditAnywhere, Category = Settings)
	bool bIsSubCollection = false;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExEntryVariationMode VariationMode = EPCGExEntryVariationMode::Local;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variations", EditCondition="!bIsSubCollection && VariationMode == EPCGExEntryVariationMode::Local", EditConditionHides, ShowOnlyInnerProperties))
	FPCGExFittingVariations Variations;

	UPROPERTY(EditAnywhere, Category = Settings)
	TSet<FName> Tags;

	/**
	 * Property overrides for this entry.
	 * Values here take precedence over collection-level defaults.
	 * Only include properties you want to override.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExPropertyOverrides PropertyOverrides;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExEntryVariationMode GrammarSource = EPCGExEntryVariationMode::Local;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	FPCGExAssetGrammarDetails AssetGrammar;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection", EditConditionHides))
	EPCGExGrammarSubCollectionMode SubGrammarMode = EPCGExGrammarSubCollectionMode::Inherit;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection && SubGrammarMode == EPCGExGrammarSubCollectionMode::Override", EditConditionHides))
	FPCGExCollectionGrammarDetails CollectionGrammar;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	FPCGExAssetStagingData Staging;

	/** Internal subcollection reference - set via EDITOR_Sanitize from typed SubCollection property */
	UPROPERTY()
	TObjectPtr<UPCGExAssetCollection> InternalSubCollection;


	// Subcollection Access (Virtual - Override in derived types)

	/** Get subcollection as base type. Override in derived classes. */
	virtual const UPCGExAssetCollection* GetSubCollectionPtr() const { return InternalSubCollection; }

	/** Clear subcollection references. Override to also clear typed pointer. */
	virtual void ClearSubCollection() { InternalSubCollection = nullptr; }

	/** Check if this is a valid subcollection entry */
	bool HasValidSubCollection() const { return bIsSubCollection && GetSubCollectionPtr() != nullptr; }


	// Typed Subcollection Access (Templates for convenience)

	template <typename T>
	T* GetSubCollection() { return Cast<T>(InternalSubCollection); }

	template <typename T>
	const T* GetSubCollection() const { return Cast<T>(InternalSubCollection); }


	// Variations & Grammar

	const FPCGExFittingVariations& GetVariations(const UPCGExAssetCollection* ParentCollection) const;
	double GetGrammarSize(const UPCGExAssetCollection* Host) const;
	double GetGrammarSize(const UPCGExAssetCollection* Host, TMap<const FPCGExAssetCollectionEntry*, double>* SizeCache) const;
	bool FixModuleInfos(const UPCGExAssetCollection* Host, FPCGSubdivisionSubmodule& OutModule, TMap<const FPCGExAssetCollectionEntry*, double>* SizeCache = nullptr) const;


	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection);
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive);
	virtual void SetAssetPath(const FSoftObjectPath& InPath);
	virtual void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize();
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

		virtual FTypeId GetTypeId() const { return TypeIds::None; }

		bool IsEmpty() const { return Order.IsEmpty(); }
		int32 Num() const { return Order.Num(); }

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

		FORCEINLINE bool IsEmpty() const { return Order.IsEmpty(); }
		FORCEINLINE int32 Num() const { return Order.Num(); }

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
		TMap<FName, TSharedPtr<FCategory>> Categories;

		FCache();
		~FCache() = default;

		FORCEINLINE bool IsEmpty() const { return Main ? Main->IsEmpty() : true; }

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
class PCGEXCOLLECTIONS_API UPCGExAssetCollection : public UDataAsset
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
	virtual PCGExAssetCollection::FTypeId GetTypeId() const { return PCGExAssetCollection::TypeIds::Base; }

	/** Check if this collection is of a specific type */
	bool IsType(PCGExAssetCollection::FTypeId TypeId) const
	{
		return PCGExAssetCollection::FTypeRegistry::Get().IsA(GetTypeId(), TypeId);
	}

#pragma endregion

#pragma region Cache

	PCGExAssetCollection::FCache* LoadCache();
	virtual void InvalidateCache();
	virtual void BuildCache();

#pragma endregion

#pragma region API

	/** Get entry at cache-adjusted index (0 = first valid entry, 1 = second, etc.) */
	FPCGExEntryAccessResult GetEntryAt(int32 Index) const;

	/** Get entry by raw Entries array index (bypasses cache). Use for indices from FCategory, packed hashes, etc. */
	FPCGExEntryAccessResult GetEntryRaw(int32 RawIndex) const;

#if WITH_EDITOR
	/** Editor-only mutable access to entry at raw array index. For editor UI direct writes. */
	FPCGExAssetCollectionEntry* EDITOR_GetMutableEntry(int32 Index) { return GetMutableEntryAtRawIndex(Index); }
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
	virtual bool IsValidIndex(int32 InIndex) const { return false; }

	/** Get total number of entries */
	virtual int32 NumEntries() const { return 0; }

	/** Get number of valid (non-zero weight) entries */
	virtual int32 GetValidEntryNum() { return LoadCache()->Main->Indices.Num(); }

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

#pragma region Lifecycle

	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void BeginDestroy() override;

	void RebuildStagingData(bool bRecursive);
	void EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const;

	/** Rebuild property registry from CollectionProperties. Called automatically during cache build. */
	void RebuildPropertyRegistry()
	{
		TArray<FInstancedStruct> Schema = CollectionProperties.BuildSchema();
		PCGExProperties::BuildRegistry(Schema, PropertyRegistry);
	}

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
#endif

	static uint32 GenerateNewGUID() { return GetTypeHash(FGuid::NewGuid()); }

#pragma endregion

public:
	// Properties

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, MultiLine))
	FString Notes;
#endif

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1))
	TSet<FName> CollectionTags;

	/** Persistent unique identifier for this collection. Used by FPickPacker to produce
	 *  deterministic, mergeable hashes across different staging nodes and sessions.
	 *  Generated once in the constructor; new value assigned on duplication/import. */
	UPROPERTY(VisibleAnywhere, Category = Settings, AdvancedDisplay, meta=(IgnoreForMemberInitializationTest))
	uint32 CollectionGUID = 0;

	uint32 GetCollectionGUID() const { return CollectionGUID; }

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	bool bAutoRebuildStaging = true;
#endif

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExGlobalVariationRule GlobalVariationMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExFittingVariations GlobalVariations;

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExGlobalVariationRule GlobalGrammarMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExAssetGrammarDetails GlobalAssetGrammar = FPCGExAssetGrammarDetails(FName("N/A"));

	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExCollectionGrammarDetails CollectionGrammar;

	UPROPERTY(EditAnywhere, Category = Settings)
	bool bDoNotIgnoreInvalidEntries = false;

	/**
	 * Collection-level properties with default values.
	 * Entries inherit these unless they provide overrides.
	 */
	UPROPERTY(EditAnywhere, Category = "Properties")
	FPCGExPropertySchemaCollection CollectionProperties;

	/**
	 * Read-only registry of available properties (built from CollectionProperties).
	 * Used for UI display and validation.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Properties")
	TArray<FPCGExPropertyRegistryEntry> PropertyRegistry;

protected:
	// Internal - Override in derived classes

	/** Get entry at raw array index (not cache-adjusted). Must override. */
	virtual const FPCGExAssetCollectionEntry* GetEntryAtRawIndex(int32 Index) const { return nullptr; }

	/** Get mutable entry at raw array index. Must override. */
	virtual FPCGExAssetCollectionEntry* GetMutableEntryAtRawIndex(int32 Index) { return nullptr; }

	/** Build cache from entries. Call with your Entries array. */
	template <typename T>
	bool BuildCacheFromEntries(TArray<T>& InEntries);

	UPROPERTY()
	bool bCacheNeedsRebuild = true;

	TSharedPtr<PCGExAssetCollection::FCache> Cache;
};

// Validates each entry, registers valid ones to the cache (Main + named categories),
// triggers MicroCache builds, and compiles weight-sorted indices.
template <typename T>
bool UPCGExAssetCollection::BuildCacheFromEntries(TArray<T>& InEntries)
{
	FWriteScopeLock WriteScopeLock(CacheLock);

	if (Cache) { return true; }

	// Rebuild property registry from collection properties
	RebuildPropertyRegistry();

	Cache = MakeShared<PCGExAssetCollection::FCache>();
	bCacheNeedsRebuild = false;

	const int32 NumEntriesCount = InEntries.Num();
	Cache->Main->Reserve(NumEntriesCount);

	for (int32 i = 0; i < NumEntriesCount; i++)
	{
		T& Entry = InEntries[i];
		if (!Entry.Validate(this)) { continue; }

		Cache->RegisterEntry(i, static_cast<const FPCGExAssetCollectionEntry*>(&Entry));
	}

	Cache->Compile();

	return true;
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
	{ return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr; } \
public:

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
