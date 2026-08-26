// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExData.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/PCGExTypeTraits.h"

#include "PCGExPropertySchema.generated.h"

class UPCGExPropertySchemaAsset;

// FPCGExProperty stays out of this header by design: this is what the collection stack embeds by
// value, so pulling the property base in here re-couples every such translation unit to it.
struct FPCGExProperty;

/**
 * Entry in the property registry.
 * Built at compile time to provide a read-only view of available properties.
 *
 * The registry is used by:
 * - FPCGExPropertyOutputSettings::AutoPopulateFromRegistry() to auto-create output configs
 * - UI systems to display available property types and their capabilities
 *
 * Custom property types are automatically included when BuildRegistry() is called
 * on an FInstancedStruct array containing your type.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyRegistryEntry
{
	GENERATED_BODY()

	/** Property name */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Property")
	FName PropertyName;

	/** Property type name (e.g., "String", "Int32", "Vector") */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Property")
	FName TypeName;

	/** PCG metadata type for attribute output */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Property")
	EPCGMetadataTypes OutputType = EPCGMetadataTypes::Unknown;

	/** Whether this property supports attribute output */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Property")
	bool bSupportsOutput = false;

	/** Whether this property supports time-based sampling (see FPCGExProperty::SampleAt) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Property")
	bool bSupportsSampling = false;

	FPCGExPropertyRegistryEntry() = default;

	FPCGExPropertyRegistryEntry(FName InName, FName InTypeName, EPCGMetadataTypes InOutputType, bool bInSupportsOutput, bool bInSupportsSampling = false)
		: PropertyName(InName)
		  , TypeName(InTypeName)
		  , OutputType(InOutputType)
		  , bSupportsOutput(bInSupportsOutput)
		  , bSupportsSampling(bInSupportsSampling)
	{
	}
};

/**
 * Records a regenerated HeaderId. Name disambiguates which collided schema each remap
 * refers to -- OldId alone aliased multiple entries pre-regeneration.
 *
 * Defined unconditionally so signatures referencing it compile outside the editor; the
 * dedup pass that populates these only runs in WITH_EDITOR.
 */
struct PCGEXPROPERTIES_API FPCGExHeaderIdRemap
{
	int32 OldId = 0;
	int32 NewId = 0;
	FName Name = NAME_None;

	FPCGExHeaderIdRemap() = default;

	FPCGExHeaderIdRemap(int32 InOldId, int32 InNewId, FName InName)
		: OldId(InOldId), NewId(InNewId), Name(InName)
	{
	}
};

/**
 * Single property override entry.
 * Stores enabled state + typed value. PropertyName comes from the inner struct.
 *
 * Override entries are kept in parallel arrays with the schema:
 * - Schema[0] <-> Override[0], Schema[1] <-> Override[1], etc.
 * - This enables efficient per-column iteration and index-based access.
 * - SyncToSchema() maintains this parallel structure automatically.
 *
 * Custom properties work transparently here - the FInstancedStruct Value
 * holds any FPCGExProperty derivative polymorphically.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyOverrideEntry
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	// Outer identity cache. Lives outside Value (the FInstancedStruct) so it survives UE's
	// broken per-property propagation for FInstancedStruct-in-TArray: when propagation drops
	// the inner Value content on instances, the outer cache is the only signal SyncToSchema
	// has for matching existing entries to the new schema.
	UPROPERTY()
	int32 HeaderId = 0;

	UPROPERTY()
	FName PropertyName = NAME_None;
#endif

	/** Whether this override is active (false = use collection default) */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEnabled = false;

	// NoResetToDefault: the default reset on the outer FInstancedStruct would clear the
	// struct shape entirely. The inner property's per-UPROPERTY reset arrows provide the
	// right-level "reset to CDO value" gesture; chaining to the outer would also fight the
	// instance-data restore path (see FPCGExPropertyCollectionInstanceData).
	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExProperties.PCGExProperty", ExcludeBaseStruct, EditCondition="bEnabled", NoResetToDefault))
	FInstancedStruct Value;

	FPCGExPropertyOverrideEntry() = default;

	explicit FPCGExPropertyOverrideEntry(const FInstancedStruct& InValue, bool bInEnabled = false)
		: bEnabled(bInEnabled)
		  , Value(InValue)
	{
#if WITH_EDITORONLY_DATA
		SeedOuterIdentityFromInner();
#endif
	}

	// The four accessors below are implemented in .cpp: reaching through Value needs FPCGExProperty
	// complete, and this header stays free of it.

#if WITH_EDITORONLY_DATA
	// Copy inner FPCGExProperty identity (PropertyName, HeaderId) into the outer cache fields.
	// Idempotent; safe to call any time Value has been assigned a non-default content.
	void SeedOuterIdentityFromInner();
#endif

	FName GetPropertyName() const;

	/** Get the property from Value (may be nullptr) */
	const FPCGExProperty* GetProperty() const;

	FPCGExProperty* GetPropertyMutable();

	bool IsValid() const
	{
		return Value.IsValid() && !GetPropertyName().IsNone();
	}
};

/**
 * Wrapper struct for property overrides array.
 * Used by Collections (entry-level overrides) and Tuple (row values).
 *
 * The Overrides array is kept parallel with the schema array:
 * - Same size, same order as the schema that created it
 * - Each entry has bEnabled flag to toggle that column for this row
 * - Disabled entries use collection/schema defaults
 *
 * USAGE PATTERN (for nodes that use properties):
 *
 *   // In your settings class:
 *   FPCGExPropertySchemaCollection MySchema;           // Define columns
 *   TArray<FPCGExPropertyOverrides> MyRows;            // Row values
 *
 *   // In PostEditChangeProperty (structural change to schema collection):
 *   MySchema.SyncAllSchemas();                          // canonicalize identity
 *   MySchema.ReconcileImportOverrides();                // align imports
 *   MySchema.ApplyToOverrides(MyRows);                  // apply to external rows
 *
 *   // At runtime, read values:
 *   for (int Col = 0; Col < MySchema.Num(); ++Col) {
 *       if (MyRows[RowIdx].IsOverrideEnabled(Col)) {
 *           const FPCGExProperty* Prop = MyRows[RowIdx].Overrides[Col].GetProperty();
 *           // Use Prop->Value...
 *       }
 *   }
 *
 * The customization needs no schema of its own: Overrides is kept parallel to it by SyncToSchema,
 * so the holder can live on any object, not just one exposing a schema property.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyOverrides
{
	GENERATED_BODY()

	/** Overrides array - parallel with schema (same size, same order) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(NoResetToDefault))
	TArray<FPCGExPropertyOverrideEntry> Overrides;

	/**
	 * Sync overrides to match schema - ensures parallel array structure.
	 *
	 * This is the core mechanism that keeps overrides aligned with their schema.
	 * In the editor, it uses HeaderId for stable matching:
	 * - Existing overrides matched by HeaderId preserve their bEnabled state
	 * - Same-type matches also preserve the override value
	 * - Type changes preserve bEnabled but reset the value to schema default
	 * - New properties (no HeaderId match) are added as disabled
	 *
	 * At runtime (no editor data), overrides are rebuilt from schema defaults.
	 *
	 * @param Schema The schema to sync to (use FPCGExPropertySchemaCollection::BuildSchema())
	 * @return       True if anything changed. Slow path (structural drift) returns true
	 *               unconditionally; fast path returns true only if a PropertyName or
	 *               structural field actually differed. Drives conditional MarkPackageDirty.
	 */
	bool SyncToSchema(const TArray<FInstancedStruct>& Schema);

	/**
	 * Apply HeaderId remaps from a schema dedup pass. Matches entries by (OldId, Name) --
	 * OldId alone would alias the entries the dedup just split apart. Idempotent; no-op
	 * outside the editor so call sites stay unguarded.
	 */
	void ApplyHeaderIdRemap(TConstArrayView<FPCGExHeaderIdRemap> Remaps);

	/** Check if override at index is enabled */
	bool IsOverrideEnabled(int32 Index) const
	{
		return Overrides.IsValidIndex(Index) && Overrides[Index].bEnabled;
	}

	/** Set override enabled state at index */
	void SetOverrideEnabled(int32 Index, bool bEnabled)
	{
		if (Overrides.IsValidIndex(Index))
		{
			Overrides[Index].bEnabled = bEnabled;
		}
	}

	/** Check if an enabled override exists for the given property name */
	bool HasOverride(FName PropertyName) const
	{
		return GetOverride(PropertyName) != nullptr;
	}

	/** Get enabled override by name (returns nullptr if not found or disabled) */
	const FInstancedStruct* GetOverride(FName PropertyName) const;

	/**
	 * Find an entry by property name, ignoring bEnabled. Returns nullptr if no entry matches.
	 * Use this when the caller needs to mutate the entry (e.g. enable it as part of a write);
	 * GetOverride is the read-only, enabled-only counterpart.
	 */
	FPCGExPropertyOverrideEntry* FindEntryMutableByName(FName PropertyName)
	{
		if (PropertyName.IsNone())
		{
			return nullptr;
		}
		for (FPCGExPropertyOverrideEntry& Entry : Overrides)
		{
			if (Entry.GetPropertyName() == PropertyName)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** Count enabled overrides */
	int32 GetEnabledCount() const
	{
		int32 Count = 0;
		for (const FPCGExPropertyOverrideEntry& Entry : Overrides)
		{
			if (Entry.bEnabled)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * Get typed property from enabled overrides by name.
	 * @param PropertyName The property name to search for
	 * @return Pointer to typed property if found and enabled, nullptr otherwise
	 */
	template <typename T>
	const T* GetProperty(FName PropertyName) const
	{
		static_assert(TIsDerivedFrom<T, FPCGExProperty>::Value,
		              "T must derive from FPCGExProperty");

		for (const FPCGExPropertyOverrideEntry& Entry : Overrides)
		{
			if (Entry.bEnabled && Entry.GetPropertyName() == PropertyName)
			{
				if (const T* Typed = Entry.Value.GetPtr<T>())
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}
};

/**
 * Schema entry for property definitions.
 * Used by Collections, Valency, and Tuple to define available properties with stable identity.
 *
 * A schema entry binds together:
 * - A Name (shown in UI, used as attribute name for output)
 * - A Property (FInstancedStruct holding any FPCGExProperty derivative)
 * - A HeaderId (editor-only, for stable override matching)
 *
 * HeaderId is preserved through type changes (stored outside FInstancedStruct), enabling:
 * - Rename property -> HeaderId stays same -> override state preserved
 * - Reorder properties -> HeaderId stays same -> values stay correct
 * - Change type -> HeaderId preserved -> bEnabled state preserved, value reset
 *
 * The FInstancedStruct picker is constrained via meta=(BaseStruct=".../PCGExProperty",
 * ExcludeBaseStruct) so only concrete property types appear in the dropdown.
 * Custom property types automatically appear here once their USTRUCT is compiled.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertySchema
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/** Stable identity for override matching, preserved through type changes */
	UPROPERTY()
	int32 HeaderId = 0;
#endif

	/** Property name (shown in UI, used for attribute output) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName Name = NAME_None;

	// NoResetToDefault: same reasoning as FPCGExPropertyOverrideEntry::Value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(BaseStruct="/Script/PCGExProperties.PCGExProperty", ExcludeBaseStruct, ShowOnlyInnerProperties, NoResetToDefault))
	FInstancedStruct Property;

	// Constructor and the three accessors below are implemented in .cpp: reaching through Property
	// needs FPCGExProperty complete, and this header stays free of it.
	FPCGExPropertySchema();

	/** Sync Name to Property.PropertyName and HeaderId */
	void SyncPropertyName();

	/** Get the property from Property (may be nullptr) */
	const FPCGExProperty* GetProperty() const;

	FPCGExProperty* GetPropertyMutable();

	bool IsValid() const
	{
		return Property.IsValid() && !Name.IsNone();
	}
};

/**
 * Resolved entry produced by FPCGExPropertySchemaCollection::Resolve.
 *
 * A resolved entry is a pointer into the source FPCGExPropertySchemaCollection::Schemas
 * array (either a local schema on the root collection, or one carried by an imported
 * UPCGExPropertySchemaAsset somewhere down the import tree).
 *
 * The pointer is valid for as long as the collections and assets that participated
 * in the resolution remain alive. Because the collection holds hard TObjectPtr refs
 * to its ImportedSchemas, callers that keep the resolved list for the duration of a
 * single operation (e.g. a node Execute) can safely use the raw pointers.
 *
 * Not a USTRUCT -- transient runtime view, not meant for serialization or BP reflection.
 */
struct PCGEXPROPERTIES_API FPCGExPropertyResolved
{
	/** Pointer into the source collection's Schemas array. Always non-null in a resolved entry. */
	const FPCGExPropertySchema* Source = nullptr;

	/** Asset that contributed this entry. Null when the entry comes from the root collection's locals. */
	UPCGExPropertySchemaAsset* OwningAsset = nullptr;

	/** Index within the source collection's Schemas array. */
	int32 SourceIndex = INDEX_NONE;

	/**
	 * Non-null when the root collection's ImportOverrides supplies an enabled override for this entry's Name.
	 * Points into ImportOverrides storage on the root collection; valid for the same lifetime as Source.
	 * Only ever set for imported entries (OwningAsset != null) -- locals are edited in-place.
	 */
	const FInstancedStruct* OverrideValue = nullptr;

	FPCGExPropertyResolved() = default;

	FPCGExPropertyResolved(const FPCGExPropertySchema* InSource, UPCGExPropertySchemaAsset* InOwningAsset, int32 InSourceIndex, const FInstancedStruct* InOverrideValue = nullptr)
		: Source(InSource), OwningAsset(InOwningAsset), SourceIndex(InSourceIndex), OverrideValue(InOverrideValue)
	{
	}

	/** Returns the override Property if one applies, otherwise the source schema's Property. */
	const FInstancedStruct& GetEffectiveProperty() const
	{
		return OverrideValue ? *OverrideValue : Source->Property;
	}
};

/**
 * Collection of property schemas with embedded utilities.
 * This is the primary container for defining a set of typed properties.
 *
 * Used by:
 * - Tuple node (Composition field) - defines columns of a param data table
 * - Collections (CollectionProperties) - defines per-entry properties on asset collections
 * - Valency (via UPCGExPropertyCollectionComponent) - defines cage/pattern properties
 * - Any custom node that needs user-definable typed properties
 *
 * INTEGRATING INTO YOUR OWN NODE:
 *
 *   // In your UPCGExSettings subclass:
 *   UPROPERTY(EditAnywhere, Category = Settings)
 *   FPCGExPropertySchemaCollection MyProperties;
 *
 *   // If you have override rows (like Tuple):
 *   UPROPERTY(EditAnywhere, Category = Settings)
 *   TArray<FPCGExPropertyOverrides> MyValues;
 *
 *   // In PostEditChangeProperty, on any schema change:
 *   MyProperties.SyncAllSchemas();
 *   MyProperties.ReconcileImportOverrides();
 *   MyProperties.ApplyToOverrides(MyValues);
 *
 *   // At runtime, access properties:
 *   const auto* FloatProp = MyProperties.GetProperty<FPCGExProperty_Float>(FName("MyFloat"));
 *
 * COMPOSITION via imported assets:
 *
 *   ImportedSchemas pulls in UPCGExPropertySchemaAsset entries (which themselves wrap
 *   an FPCGExPropertySchemaCollection -- recursion supported with cycle detection).
 *   Resolve() / BuildSchema() / FindByName() all walk locals first, then imports
 *   depth-first, deduping by Name with first-wins semantics: locals beat imports,
 *   earlier imports beat later ones.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertySchemaCollection
{
	GENERATED_BODY()

	/** Schema array (locals -- always take precedence over imported entries with matching names) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(TitleProperty="{Name}"))
	TArray<FPCGExPropertySchema> Schemas;

	/**
	 * Imported schema assets, resolved in array order after locals.
	 * Hard refs -- assets stay loaded as long as the owning collection exists.
	 * Recursion through imported assets' own ImportedSchemas is supported with cycle detection.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName="Imported Schemas"))
	TArray<TObjectPtr<UPCGExPropertySchemaAsset>> ImportedSchemas;

	/**
	 * Per-entry value overrides for imported entries.
	 *
	 * EditAnywhere is required so the detail panel's GetChildHandle can reach this property's
	 * children -- the registered FPCGExPropertySchemaCollectionCustomization takes over rendering
	 * entirely (CustomizeChildren controls every visible row), so this never appears as a
	 * top-level array editor in the inspector. The array is kept parallel with the imports-only
	 * schema via ReconcileImportOverrides (which delegates to FPCGExPropertyOverrides::SyncToSchema).
	 *
	 * UE per-instance UPROPERTY delta serialization handles three-layer composition:
	 * asset default -> template (collection on CDO) -> instance (collection on actor instance).
	 * Each layer overrides the previous via bEnabled toggles on individual entries.
	 *
	 * Locals do not appear here -- they are edited in-place on Schemas.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExPropertyOverrides ImportOverrides;

	/**
	 * Flatten the locals + imported asset tree into a name-deduped, first-wins resolved list.
	 *
	 * Walk order:
	 * - This collection's locals (in array order)
	 * - Each entry in ImportedSchemas (in array order), recursing depth-first
	 *
	 * Dedup is by FPCGExPropertySchema::Name; the first occurrence wins. Locals therefore
	 * override any imported entry with the same name, and earlier imports override later ones.
	 *
	 * Cycles (an asset reachable from itself through ImportedSchemas) are skipped and logged
	 * once per cycle via LogPCGEx. The first reach of an asset wins; subsequent reaches are no-ops.
	 *
	 * Entries with empty Name or invalid Property are skipped.
	 *
	 * Thread-safe: reads only. Mirrors the AssetCollection::BuildCache pattern -- the result
	 * is built on demand and owned by the caller. The collection itself holds no cached state.
	 *
	 * Optional FallbackChain layers extend the override lookup beyond this collection's own
	 * ImportOverrides: when this collection's entry returns null from GetOverride (disabled
	 * or missing), each fallback layer is tried in order. First non-null wins. Used by
	 * UPCGExPropertyCollectionComponent to walk the BP class chain so an instance defers
	 * to its CDO's authored override when the instance hasn't toggled its own.
	 *
	 * bIncludeOwnOverrides controls whether this collection's own ImportOverrides leads the
	 * chain (default true). Set false to walk the chain WITHOUT the instance's own authoring --
	 * used to extract "what value would surface if nothing was overridden?" (the CDO/asset view).
	 */
	void Resolve(TArray<FPCGExPropertyResolved>& Out, TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {}, bool bIncludeOwnOverrides = true) const;

	/** Find schema by property name (walks locals first, then imported assets) */
	const FPCGExPropertySchema* FindByName(FName PropertyName) const;

	/**
	 * Find schema by property name (mutable) -- LOCALS ONLY.
	 *
	 * Unlike FindByName, this never returns a pointer into an imported asset's schema array.
	 * Writes through this pointer must only affect the owning collection's local data; an
	 * asset-owned pointer would let callers silently mutate the source asset globally.
	 *
	 * To override an imported entry's value at the importing collection's level, modify the
	 * corresponding FPCGExPropertyOverrideEntry in ImportOverrides instead (set bEnabled=true
	 * and write to its inner Value).
	 */
	FPCGExPropertySchema* FindByNameMutable(FName PropertyName);

	/** Check if property exists by name */
	bool HasProperty(FName PropertyName) const
	{
		return FindByName(PropertyName) != nullptr;
	}

	/**
	 * Get the effective property by name, honoring the three-layer composition:
	 *   local schemas -> ImportOverrides (if enabled) -> imported asset's default.
	 *
	 * Read-only -- safe to call from any thread (see ReconcileImportOverrides contract).
	 */
	const FInstancedStruct* GetPropertyByName(FName PropertyName) const;

	/** Build FInstancedStruct array for SyncToSchema calls. FallbackChain and bIncludeOwnOverrides
	 *  have the same meaning as Resolve's. */
	TArray<FInstancedStruct> BuildSchema(TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {}, bool bIncludeOwnOverrides = true) const;

	/** Validate all LOCAL property names are unique (returns true if valid). Deliberately does not
	 *  span imports: same-name shadowing across layers is the designed first-seen-wins behavior
	 *  (see Resolve); a duplicate among locals is an authoring error. */
	bool ValidateUniqueNames(TArray<FName>& OutDuplicates) const;

	/** Get typed property by name */
	template <typename T>
	const T* GetProperty(FName PropertyName) const
	{
		static_assert(TIsDerivedFrom<T, FPCGExProperty>::Value,
		              "T must derive from FPCGExProperty");

		const FPCGExPropertySchema* Schema = FindByName(PropertyName);
		return Schema ? Schema->Property.GetPtr<T>() : nullptr;
	}

	/** Count valid schemas */
	int32 Num() const
	{
		return Schemas.Num();
	}

	bool IsEmpty() const
	{
		return Schemas.IsEmpty();
	}

	/**
	 * Sync all schemas -- updates PropertyName and HeaderId into each Property.
	 * Call before BuildSchema() to ensure schema has current data.
	 *
	 * Editor-only: zero HeaderIds and copy-paste-introduced duplicates are reassigned;
	 * first occurrence keeps its identity. Skipping the dedup leaves SyncToSchema's
	 * HeaderId index aliasing the duplicates and silently dropping one side's overrides.
	 */
	void SyncAllSchemas();

	/**
	 * Reports HeaderId reassignments via OutRemaps. Zero-bootstraps are NOT reported --
	 * a HeaderId of 0 never appeared in any saved override. Outside the editor, OutRemaps
	 * is always left empty. Most callers want SyncAllSchemasAndRemap[Rows] instead.
	 */
	void SyncAllSchemas(TArray<FPCGExHeaderIdRemap>& OutRemaps);

	/**
	 * Sync + invoke ApplyRemap iff any collisions were resolved. The empty-remap case
	 * skips the callback entirely, so callers don't repeat the guard.
	 *
	 * In non-editor builds ApplyRemap is never invoked, but the lambda body must still
	 * compile. FPCGExPropertyOverrides::ApplyHeaderIdRemap is no-op-stubbed there for
	 * exactly that reason.
	 */
	void SyncAllSchemasAndRemap(TFunctionRef<void(TConstArrayView<FPCGExHeaderIdRemap>)> ApplyRemap);

	/** Sync + apply remaps to a parallel array of row overrides. */
	template <typename TRow>
	void SyncAllSchemasAndRemapRows(TArray<TRow>& Rows)
	{
		static_assert(TIsDerivedFrom<TRow, FPCGExPropertyOverrides>::Value,
		              "SyncAllSchemasAndRemapRows: TRow must derive from FPCGExPropertyOverrides.");
		SyncAllSchemasAndRemap([&Rows](TConstArrayView<FPCGExHeaderIdRemap> Remaps)
		{
			for (TRow& Row : Rows)
			{
				Row.ApplyHeaderIdRemap(Remaps);
			}
		});
	}

	/**
	 * Apply the currently-resolved schema to a PropertyOverrides container, bringing its
	 * Overrides array into parallel structure with the resolved schema. Builds the schema
	 * once via BuildSchema(), then calls FPCGExPropertyOverrides::SyncToSchema.
	 *
	 * Read-only on the collection (const) -- assumes Schemas and ImportOverrides are
	 * already in canonical state. The typical full pipeline for a structural change is:
	 *
	 *   Collection.SyncAllSchemas();          // canonicalize outer -> inner identity
	 *   Collection.ReconcileImportOverrides(); // align ImportOverrides with imports tree
	 *   Collection.ApplyToOverrides(MyValues); // apply resolved schema to external overrides
	 *
	 * Skip the first two steps when the collection is known to be canonical (e.g., after
	 * a Resolve walk that already reconciled, or when only the overrides target changed).
	 */
	void ApplyToOverrides(FPCGExPropertyOverrides& Overrides) const;

	/**
	 * Array overload. Builds the schema ONCE and applies it to every element, so prefer
	 * this over a per-element loop when applying to multiple overrides containers.
	 */
	void ApplyToOverrides(TArray<FPCGExPropertyOverrides>& OverridesArray) const;

	/**
	 * Reconcile ImportOverrides against the current import tree.
	 *
	 * Builds an imports-only schema by resolving the tree, then calls
	 * ImportOverrides.SyncToSchema() to:
	 * - Preserve existing overrides whose imported entry still exists (HeaderId match)
	 * - Update Name/PropertyName when the asset renamed an entry (HeaderId stable, Name drifted)
	 * - Drop overrides whose imported entry was removed
	 * - Reset to schema default when an imported entry's type changed
	 *
	 * Safe to call on a collection with no imports -- ImportOverrides becomes empty.
	 *
	 * **Game-thread only.** Mutates ImportOverrides without locking. Asserts via
	 * check(IsInGameThread()) in dev builds. Runtime read paths (Resolve / BuildSchema /
	 * GetPropertyByName / GetOverride) are safe off-thread only because no Reconcile is
	 * allowed to run concurrently. All current call sites originate in editor
	 * PostEditChangeProperty handlers, customization callbacks, or asset broadcasts.
	 *
	 * Call after any change that may alter the import tree:
	 * - Editing local schemas (call as part of the SyncAllSchemas / ReconcileImportOverrides /
	 *   ApplyToOverrides pipeline)
	 * - ImportedSchemas array changes (add/remove an asset reference)
	 * - A referenced UPCGExPropertySchemaAsset broadcasting OnSchemaAssetChanged
	 *
	 * @return True if ImportOverrides actually changed. Callers may use this to gate
	 *         MarkPackageDirty; passing-through callers may safely discard.
	 */
	bool ReconcileImportOverrides();

	/** Overload that accepts a precomputed Resolved view -- avoids re-walking the tree. */
	bool ReconcileImportOverrides(const TArray<FPCGExPropertyResolved>& Resolved);

#if WITH_EDITOR
	/** True when Asset is reachable through ImportedSchemas (any depth, cycle-safe). The filter for
	 *  UPCGExPropertySchemaAsset::OnAnySchemaAssetChanged listeners. */
	bool ImportsAssetTransitive(const UPCGExPropertySchemaAsset* Asset) const;
#endif

	/**
	 * Rebuild this collection's structure to match Archetype, preserving Value overrides for
	 * entries whose identity matches between this and Archetype.
	 *
	 * Used to repair instance components after their owning Blueprint's schema is edited:
	 * UE's per-property propagation can leave FInstancedStruct entries default-constructed
	 * on existing instances (type falling through to the first registered FPCGExProperty
	 * subclass), which this method restores by copying the archetype's entry verbatim for
	 * any unmatched entry.
	 *
	 * Matching policy (editor-only, since HeaderId is editor-only):
	 * - Inner FPCGExProperty::HeaderId is the primary key. Constructors leave HeaderId at 0;
	 *   SyncAllSchemas assigns it explicitly, so CDO->instance propagation reliably ships
	 *   the CDO's HeaderId onto instances and matches line up.
	 * - Name is a fallback for entries with no HeaderId match. Catches legacy instances
	 *   saved before the ctor change (which still carry stale random HeaderIds on disk) so
	 *   their values aren't wiped on the first sync after upgrade.
	 * - When neither matches: take Archetype's entry verbatim (new property, or genuinely
	 *   unmatched after both lookups).
	 * - Entries in this collection with no match in Archetype: dropped (removed property).
	 *
	 * At runtime (cooked, no editor data), this is a no-op -- cooked data is finalized.
	 */
	void SyncFromArchetype(const FPCGExPropertySchemaCollection& Archetype);
};

/**
 * Property overrides with per-row weight for distribution.
 * Used by Tuple : Distribute to assign weighted probability to each row.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExWeightedPropertyOverrides : public FPCGExPropertyOverrides
{
	GENERATED_BODY()

	/** Weight for this row in distribution (higher = more likely to be picked) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(ClampMin=0, UIMin=0))
	int32 Weight = 1;
};
