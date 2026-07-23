// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "Core/PCGExAssetGrammar.h"
#include "Fitting/PCGExFittingVariations.h"

#include "PCGExCollectionEntryBlueprintLibrary.generated.h"

class UPCGExAssetCollection;

/**
 * Blueprint access to asset-collection entries by raw index: typed property override
 * read/write plus plain entry-field helpers (weight, category, tags, staging reads).
 *
 * Primary consumer is UPCGExCollectionStagingPipeline hooks (which receive Collection +
 * EntryIndex), but everything here also works from editor utility blueprints/widgets.
 * Setters snapshot the collection for undo (Modify) and mark its package dirty; weight /
 * category / tag setters additionally invalidate the pick cache.
 *
 * The wildcard CustomThunk pair and the Object/Class variants are BlueprintInternalUseOnly:
 * UK2Node_GetPCGExEntryProperty / UK2Node_SetPCGExEntryProperty are the user-facing entries
 * and expand to calls to these functions at compile time (same split as
 * UPCGExPropertyBlueprintLibrary, and for the same marshalling reasons).
 */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExCollectionEntryBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Try to read the resolved value of a named property for an entry: the entry's enabled
	 * override first, falling back to the collection's schema default. OutValue is a wildcard
	 * whose concrete pin type at compile time drives the EPCGMetadataTypes conversion.
	 */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "OutValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Property Value"))
	static bool TryGetEntryPropertyValue(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		int32& OutValue);

	DECLARE_FUNCTION(execTryGetEntryPropertyValue);

	/**
	 * Try to write a value to an entry's property override slot. NewValue is a wildcard input
	 * whose concrete type at compile time drives the EPCGMetadataTypes conversion.
	 *
	 * The override slot must already exist in the entry (slots are kept parallel to the
	 * collection's schema by SyncPropertyOverridesToEntries); a name that isn't part of the
	 * schema fails with a Blueprint runtime warning. On success the override is enabled and
	 * the collection is dirtied.
	 *
	 * The Set K2 node's readback chains a separate TryGetEntryPropertyValue call after this
	 * one -- each thunk keeps a single wildcard (see UPCGExPropertyBlueprintLibrary).
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "NewValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Property Override"))
	static bool TrySetEntryPropertyOverride(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		const int32& NewValue);

	DECLARE_FUNCTION(execTrySetEntryPropertyOverride);

	/**
	 * Read an entry's resolved property as an object reference. The underlying property is
	 * treated as a soft object path; the resolved object is filtered against ExpectedClass.
	 * Separate from the wildcard thunk because Object pins don't round-trip cleanly through
	 * CustomStructureParam (see UPCGExPropertyBlueprintLibrary for the gory details).
	 */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Property Object"))
	static UObject* TryGetEntryPropertyObject(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Property Object"))
	static bool TrySetEntryPropertyObject(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		UObject* NewObject);

	/** Class-pin variants of the Object accessors (soft class path under the hood). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Property Class"))
	static TSubclassOf<UObject> TryGetEntryPropertyClass(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Property Class"))
	static bool TrySetEntryPropertyClass(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		UClass* NewClass);

	// Generic reflected member access -- BlueprintInternalUseOnly backing for the
	// Get/Set Entry Member and Get/Set Collection Member K2 nodes. MemberPath is a
	// dot-separated path into the entry struct (resolved via the collection type registry)
	// or the collection object itself, e.g. "Weight", "AssetGrammar.SizingX.FixedSize",
	// "ISMDescriptor.InstanceStartCullDistance". Exact-type semantics: the wildcard pin's
	// type must match the member's reflected type (the K2 node stamps it from the picked
	// member); mismatches and unknown paths fail with a Blueprint runtime warning.

	UFUNCTION(BlueprintPure, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "OutValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Member Value"))
	static bool TryGetEntryMemberValue(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		int32& OutValue);

	DECLARE_FUNCTION(execTryGetEntryMemberValue);

	/** Writes an entry member. Commits Modify + dirty + cache invalidation on success. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "NewValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Member Value"))
	static bool TrySetEntryMemberValue(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		const int32& NewValue);

	DECLARE_FUNCTION(execTrySetEntryMemberValue);

	UFUNCTION(BlueprintPure, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "OutValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Collection Member Value"))
	static bool TryGetCollectionMemberValue(
		const UPCGExAssetCollection* Collection,
		FName MemberPath,
		int32& OutValue);

	DECLARE_FUNCTION(execTryGetCollectionMemberValue);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "PCGEx|Collection",
		meta = (CustomStructureParam = "NewValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Collection Member Value"))
	static bool TrySetCollectionMemberValue(
		UPCGExAssetCollection* Collection,
		FName MemberPath,
		const int32& NewValue);

	DECLARE_FUNCTION(execTrySetCollectionMemberValue);

	// Typed soft-reference member accessors. Soft pins do not round-trip through the
	// CustomStructureParam wildcard (same marshalling hazard as Object pins); the member
	// K2 nodes dispatch PC_SoftObject / PC_SoftClass members here instead. bSuccess
	// reflects member resolution + flavor/class compatibility -- the returned soft
	// reference itself may legitimately be null.

	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Member (Soft Object)"))
	static TSoftObjectPtr<UObject> TryGetEntryMemberSoftObject(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Member (Soft Object)"))
	static bool TrySetEntryMemberSoftObject(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		TSoftObjectPtr<UObject> NewValue);

	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Entry Member (Soft Class)"))
	static TSoftClassPtr<UObject> TryGetEntryMemberSoftClass(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Entry Member (Soft Class)"))
	static bool TrySetEntryMemberSoftClass(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName MemberPath,
		TSoftClassPtr<UObject> NewValue);

	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Collection Member (Soft Object)"))
	static TSoftObjectPtr<UObject> TryGetCollectionMemberSoftObject(
		const UPCGExAssetCollection* Collection,
		FName MemberPath,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Collection Member (Soft Object)"))
	static bool TrySetCollectionMemberSoftObject(
		UPCGExAssetCollection* Collection,
		FName MemberPath,
		TSoftObjectPtr<UObject> NewValue);

	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get Collection Member (Soft Class)"))
	static TSoftClassPtr<UObject> TryGetCollectionMemberSoftClass(
		const UPCGExAssetCollection* Collection,
		FName MemberPath,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set Collection Member (Soft Class)"))
	static bool TrySetCollectionMemberSoftClass(
		UPCGExAssetCollection* Collection,
		FName MemberPath,
		TSoftClassPtr<UObject> NewValue);

	// Plain entry helpers -- user-facing, usable anywhere a collection reference exists.

	/** Number of entries in the collection's raw entries array (includes invalid entries). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static int32 GetNumEntries(const UPCGExAssetCollection* Collection);

	/** True if EntryIndex is a valid raw index into the collection's entries array. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static bool IsValidEntryIndex(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** True if the entry at EntryIndex is a subcollection reference rather than an asset. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static bool IsSubCollectionEntry(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** Entry weight, or 0 when the entry doesn't exist. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static int32 GetEntryWeight(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** Set the entry weight (clamped to >= 0). Returns false when the entry doesn't exist. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetEntryWeight(UPCGExAssetCollection* Collection, int32 EntryIndex, int32 NewWeight);

	/** Entry category, or None when the entry doesn't exist. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FName GetEntryCategory(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** Set the entry category. Returns false when the entry doesn't exist. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetEntryCategory(UPCGExAssetCollection* Collection, int32 EntryIndex, FName NewCategory);

	/** The entry's tags as an array (empty when the entry doesn't exist). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static TArray<FName> GetEntryTags(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** Add a tag to the entry. Returns true only when the tag was newly added. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool AddEntryTag(UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag);

	/** Remove a tag from the entry. Returns true only when the tag was present. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool RemoveEntryTag(UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag);

	/** True if the entry carries the given tag. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static bool EntryHasTag(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag);

	/** True if the entry has an ENABLED override for the named property. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static bool HasEntryPropertyOverride(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName PropertyName);

	/**
	 * Enable or disable an entry's override slot for the named property without touching its
	 * value. Disabling makes resolution fall back to the collection default; the authored
	 * value is preserved. Returns false when the entry or the schema property doesn't exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetEntryPropertyOverrideEnabled(UPCGExAssetCollection* Collection, int32 EntryIndex, FName PropertyName, bool bEnabled);

	/** The entry's staged asset path (computed by the last staging rebuild). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FSoftObjectPath GetEntryStagingPath(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** The entry's staged local bounds (computed by the last staging rebuild). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FBox GetEntryStagingBounds(const UPCGExAssetCollection* Collection, int32 EntryIndex);

	// Whole-struct pairs -- copy out, edit with standard struct nodes, write back.

	/** The entry's AUTHORED grammar (AssetGrammar member, regardless of GrammarSource).
	 *  Use GetEntryEffectiveGrammar for the grammar that staging/export actually resolves. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExAssetGrammarDetails GetEntryGrammar(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess);

	/** Write the entry's authored grammar wholesale. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetEntryGrammar(UPCGExAssetCollection* Collection, int32 EntryIndex, const FPCGExAssetGrammarDetails& NewGrammar);

	/** The grammar the entry actually resolves to (routes through GrammarSource / collection
	 *  global / subcollection grammar) -- read-only by nature. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExAssetGrammarDetails GetEntryEffectiveGrammar(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess);

	/** The entry's authored fitting variations. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExFittingVariations GetEntryVariations(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess);

	/** Write the entry's fitting variations wholesale. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetEntryVariations(UPCGExAssetCollection* Collection, int32 EntryIndex, const FPCGExFittingVariations& NewVariations);

	/** The collection's shared Global grammar (used by entries with GrammarSource == Global). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExAssetGrammarDetails GetCollectionGlobalGrammar(const UPCGExAssetCollection* Collection, bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetCollectionGlobalGrammar(UPCGExAssetCollection* Collection, const FPCGExAssetGrammarDetails& NewGrammar);

	/** The grammar this collection exposes when nested as a subcollection entry. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExAssetGrammarDetails GetCollectionSubCollectionGrammar(const UPCGExAssetCollection* Collection, bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetCollectionSubCollectionGrammar(UPCGExAssetCollection* Collection, const FPCGExAssetGrammarDetails& NewGrammar);

	/** The collection's shared Global fitting variations. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FPCGExFittingVariations GetCollectionGlobalVariations(const UPCGExAssetCollection* Collection, bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool SetCollectionGlobalVariations(UPCGExAssetCollection* Collection, const FPCGExFittingVariations& NewVariations);

	// Computed helpers.

	/** The nested collection referenced by a subcollection entry (null for asset entries). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static UPCGExAssetCollection* GetEntrySubCollection(UPCGExAssetCollection* Collection, int32 EntryIndex);

	/**
	 * Synchronously load the entry's staged asset (Staging.Path). Callable (not pure) so the
	 * blocking load happens at an explicit point in the graph. Returns null and bSuccess=false
	 * when the path is unset, fails to load, or doesn't match ExpectedClass.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection", meta = (DeterminesOutputType = "ExpectedClass"))
	static UObject* LoadEntryAsset(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	/**
	 * Re-run staging for a single entry. Use after mutating staging-feeding members (e.g. a
	 * mesh entry's StaticMesh) from OnProcessEntry / OnPostRebuild; prefer mutating such
	 * members in OnPreRebuild instead, where the session stages them naturally. When called
	 * from inside a pipeline hook the restage is finalize-quiet (the owning session fires the
	 * post-rebuild work once at its own tail). Editor-only; returns false in cooked targets.
	 *
	 * True only when the restage actually changed the entry -- false means "nothing to do".
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Collection")
	static bool RestageEntry(UPCGExAssetCollection* Collection, int32 EntryIndex);

	/** The collection's registered type id (e.g. "Mesh", "Actor"); None when null/unregistered. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection")
	static FName GetCollectionTypeId(const UPCGExAssetCollection* Collection);
};
