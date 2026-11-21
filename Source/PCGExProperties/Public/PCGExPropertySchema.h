// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/StridedView.h"
#include "Data/PCGExData.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/PCGExTypeTraits.h"

#include "PCGExPropertySchema.generated.h"

class UPCGExPropertySchemaAsset;

// FPCGExProperty stays out of this header by design: this is what the collection stack embeds by
// value, so pulling the property base in here re-couples every such translation unit to it.
struct FPCGExProperty;

struct FPCGExPropertyResolved;
struct FPCGExResolvedChoicesCache;

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
 * One named favourite value of a property. Value is a CARRIER: an instance of the host property's own type whose
 * Value field holds the choice; its other fields (identity, Choices) are meaningless and kept empty.
 * Editor-only data, by use: only ever stored under WITH_EDITORONLY_DATA fields.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExPropertyChoice
{
	GENERATED_BODY()

	/**
	 * Stable identity: patches and rows bind to it, never to Label. Minted by MakeChoiceFromValue and by the
	 * NormalizeIds passes (SyncAllSchemas, ReconcileImportOverrides, the choice commits), never by the constructor
	 * -- see HeaderId.
	 */
	UPROPERTY()
	FGuid Id;

	/** Display text only; nothing binds to it. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FString Label;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExProperties.PCGExProperty", ExcludeBaseStruct, NoResetToDefault))
	FInstancedStruct Value;

	bool operator==(const FPCGExPropertyChoice& Other) const
	{
		// FString's own operator== ignores case, which would hide a case-only rename from the row mirrors.
		return Id == Other.Id && Label.Equals(Other.Label, ESearchCase::CaseSensitive) && Value == Other.Value;
	}

	bool operator!=(const FPCGExPropertyChoice& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * A property's favourite values and whether its editors are restricted to them. Authored on the schema, patched
 * by importers (FPCGExPropertyChoicesPatch), mirrored read-only onto every override row. UI-only: nothing
 * outside the details panel enforces the lock.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExPropertyChoices
{
	GENERATED_BODY()

	/** Value editors show the choices alone; a value outside the list is kept and shown as custom, never coerced. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bLocked = false;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(TitleProperty="{Label}"))
	TArray<FPCGExPropertyChoice> Items;

	/** Mint every unset Id and re-mint duplicates; the last holder keeps a shared Id. True if any changed. */
	bool NormalizeIds();

	bool operator==(const FPCGExPropertyChoices& Other) const
	{
		return bLocked == Other.bLocked && Items == Other.Items;
	}

	bool operator!=(const FPCGExPropertyChoices& Other) const
	{
		return !(*this == Other);
	}
};

/** How an importer rewrites the choice list of a property it imports. */
UENUM()
enum class EPCGExChoicesPatchMode : uint8
{
	Inherit = 0 UMETA(Tooltip = "Keep the list as the imported schema defines it."),
	Modify  = 1 UMETA(Tooltip = "Remove the listed choices, then add the patch's choices (one carrying an imported choice's identity replaces it)."),
	Replace = 2 UMETA(Tooltip = "Discard the imported list and use the patch's choices alone."),
};

/** How an importer rewrites the lock of a property it imports. */
UENUM()
enum class EPCGExChoicesLock : uint8
{
	Inherit  = 0 UMETA(Tooltip = "Keep the imported lock state."),
	Locked   = 1 UMETA(Tooltip = "Restrict value editors to the choices."),
	Unlocked = 2 UMETA(Tooltip = "Allow any value."),
};

/**
 * An importer's rewrite of an imported property's choices. Lives on the import override entry and applies whether
 * or not that entry's value override is enabled; patches stack from the declaring asset up to the root importer.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExPropertyChoicesPatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExChoicesPatchMode Mode = EPCGExChoicesPatchMode::Inherit;

	/** The patch's own choices; Mode decides how they combine with the imported list. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(TitleProperty="{Label}"))
	TArray<FPCGExPropertyChoice> Items;

	/** Imported choices dropped by Id. EditAnywhere only so the details tree builds its node. */
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FGuid> RemovedIds;

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExChoicesLock Lock = EPCGExChoicesLock::Inherit;

	/** True when applying this patch changes nothing. */
	bool IsIdentity() const
	{
		return Mode == EPCGExChoicesPatchMode::Inherit && Lock == EPCGExChoicesLock::Inherit;
	}

	/** True when there is nothing worth storing: an identity patch that holds no list data either. */
	bool IsEmpty() const
	{
		return IsIdentity() && Items.IsEmpty() && RemovedIds.IsEmpty();
	}

	/** FPCGExPropertyChoices::NormalizeIds over Items. */
	bool NormalizeIds();

	void Apply(FPCGExPropertyChoices& InOut) const;

	bool operator==(const FPCGExPropertyChoicesPatch& Other) const
	{
		return Mode == Other.Mode && Lock == Other.Lock && RemovedIds == Other.RemovedIds && Items == Other.Items;
	}

	bool operator!=(const FPCGExPropertyChoicesPatch& Other) const
	{
		return !(*this == Other);
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

	/**
	 * Only read on an ImportOverrides entry: this importer's rewrite of the imported property's choices, unset when it
	 * rewrites nothing. EditAnywhere so the details tree builds its node; the entry customization decides where it shows.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	TInstancedStruct<FPCGExPropertyChoicesPatch> ChoicesPatch;
#endif

	/** Whether this override is active (false = use collection default) */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEnabled = false;

	//~ NoResetToDefault: the default reset on the outer FInstancedStruct would clear the
	//~ struct shape entirely. The inner property's per-UPROPERTY reset arrows provide the
	//~ right-level "reset to CDO value" gesture; chaining to the outer would also fight the
	//~ instance-data restore path (see FPCGExPropertyCollectionInstanceData).
	/** Value used in place of the collection default when this override is enabled */
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

	// The members below are implemented in .cpp: reaching through Value needs FPCGExProperty
	// complete, and this header stays free of it.

#if WITH_EDITORONLY_DATA
	// Copy inner FPCGExProperty identity (PropertyName, HeaderId) into the outer cache fields.
	// Idempotent; safe to call any time Value has been assigned a non-default content.
	void SeedOuterIdentityFromInner();

	/** The choices patch this entry stores; null when it rewrites nothing. */
	const FPCGExPropertyChoicesPatch* GetChoicesPatch() const
	{
		return ChoicesPatch.GetPtr();
	}

	/** Store Patch, or drop the storage when Patch is empty. True if the stored state changed. */
	bool SetChoicesPatch(const FPCGExPropertyChoicesPatch& Patch);
#endif

	FName GetPropertyName() const;

	/** Get the property from Value (may be nullptr) */
	const FPCGExProperty* GetProperty() const;

	FPCGExProperty* GetPropertyMutable();

	/**
	 * Re-mirror Value's schema-owned fields from SchemaValue: the type's own (FPCGExProperty::SyncStructuralFromSchema),
	 * then the base Choices list. Nothing happens unless both hold the same property type.
	 * EffectiveChoices replaces the list read off SchemaValue; SyncInPlace passes it because its schema view is
	 * not baked the way BuildSchema's is.
	 *
	 * A value that stood on its picked choice follows that choice's new value; any other value is left alone and
	 * loses a pick it no longer matches. bFollowPick false never writes Value: for a value adopted from another host.
	 *
	 * @return True if any field changed.
	 */
	bool SyncStructuralFields(const FInstancedStruct& SchemaValue, const FPCGExPropertyChoices* EffectiveChoices = nullptr, bool bFollowPick = true);

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
 *   // At runtime, read a row against the resolved schema by name and type, never by position alone
 *   // (writers cast a value to its column's type unchecked):
 *   TArray<FPCGExPropertyResolved> Columns;
 *   MySchema.Resolve(Columns);
 *   for (int Col = 0; Col < Columns.Num(); ++Col) {
 *       const UScriptStruct* Type = Columns[Col].GetEffectiveProperty().GetScriptStruct();
 *       if (const FInstancedStruct* Slot = MyRows[RowIdx].FindEnabledSlot(Columns[Col].Source->Name, Type, Col)) {
 *           const FPCGExProperty* Prop = Slot->GetPtr<FPCGExProperty>();
 *           // Use Prop...
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
	 * In the editor, each existing override is matched to the property it stands for: on HeaderId, or on the name
	 * where it has no id or several share it. No override is handed to two properties.
	 * - A matched override preserves its bEnabled state
	 * - Same-type matches also preserve the override value (one standing on its picked choice follows that choice)
	 * - Type changes preserve bEnabled but reset the value to schema default; a choices patch crosses the retype
	 * - New properties (no match) are added as disabled
	 *
	 * At runtime (no editor data), overrides are rebuilt from schema defaults.
	 *
	 * @param Schema       The schema to sync to (use FPCGExPropertySchemaCollection::BuildSchema())
	 * @param bFollowPicks False when the values were just adopted from another host: see
	 *                     FPCGExPropertyOverrideEntry::SyncStructuralFields.
	 * @return             True if anything changed. Slow path (structural drift) returns true
	 *                     unconditionally; fast path returns true only if a PropertyName or
	 *                     structural field actually differed. Drives conditional MarkPackageDirty.
	 */
	bool SyncToSchema(const TArray<FInstancedStruct>& Schema, bool bFollowPicks = true);

	/**
	 * In-place counterpart of SyncToSchema. Each row is matched to a resolved property (HeaderId, then name),
	 * takes that property's name and re-mirrors its schema-owned fields from the DECLARATION, never from an
	 * override row standing in for it. Rows are never added, removed, reordered or retyped, so unlike
	 * SyncToSchema this is safe against an incomplete schema (a load with an import missing) and under live
	 * detail rows. A row whose type differs from its property's is left untouched.
	 *
	 * @param ChoicesCache Optional, see FPCGExResolvedChoicesCache.
	 * @return True if any row changed.
	 */
	bool SyncInPlace(TConstArrayView<FPCGExPropertyResolved> Resolved, FPCGExResolvedChoicesCache* ChoicesCache = nullptr);

	/**
	 * Apply HeaderId remaps from a schema dedup pass. Matches entries by (OldId, Name) --
	 * OldId alone would alias the entries the dedup just split apart. Idempotent; no-op
	 * outside the editor so call sites stay unguarded.
	 */
	void ApplyHeaderIdRemap(TConstArrayView<FPCGExHeaderIdRemap> Remaps);

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
		return const_cast<FPCGExPropertyOverrideEntry*>(AsConst(*this).FindEntryByName(PropertyName));
	}

	/** Read-only FindEntryMutableByName: the entry named PropertyName whatever its bEnabled, else nullptr. */
	const FPCGExPropertyOverrideEntry* FindEntryByName(FName PropertyName) const
	{
		if (PropertyName.IsNone())
		{
			return nullptr;
		}
		for (const FPCGExPropertyOverrideEntry& Entry : Overrides)
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
	 * First ENABLED slot named PropertyName whose value derives from RequiredType. Scans the whole
	 * array rather than stopping at the first name match, so a duplicate-named slot of the wrong type
	 * never shadows a correct one behind it (GetOverride stops at the first). Null RequiredType = miss.
	 * IndexHint is tried first: the index of the property in the schema these rows are kept parallel to.
	 */
	const FInstancedStruct* FindEnabledSlot(FName PropertyName, const UScriptStruct* RequiredType, int32 IndexHint = INDEX_NONE) const;

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

		const FInstancedStruct* Slot = FindEnabledSlot(PropertyName, T::StaticStruct());
		return Slot ? Slot->GetPtr<T>() : nullptr;
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

	//~ NoResetToDefault: same reasoning as FPCGExPropertyOverrideEntry::Value.
	/** Property type and default value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(BaseStruct="/Script/PCGExProperties.PCGExProperty", ExcludeBaseStruct, ShowOnlyInnerProperties, NoResetToDefault))
	FInstancedStruct Property;

#if WITH_EDITORONLY_DATA
	/**
	 * Property as it stood before a retype in progress, set by the editor's pre-change hook and consumed by
	 * ConsumeRetype within the same edit. Not reflected: never saved, transacted or kept across a frame.
	 */
	FInstancedStruct RetypedFrom;

	/** Carry RetypedFrom's choices onto Property when the type did change, then clear it. True if Property changed. */
	bool ConsumeRetype();
#endif

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
	 * Non-null when some layer of the override chain supplies an enabled override for this entry's Name: the
	 * root collection's ImportOverrides, a fallback layer, or the ImportOverrides of an asset between the root
	 * and OwningAsset (nearest the root wins). Points into that layer's storage; valid for the same lifetime
	 * as Source. Only ever set for imported entries (OwningAsset != null) -- locals are edited in-place.
	 * Always of Source->Property's type: a layer whose row still holds another type is skipped, not served.
	 */
	const FInstancedStruct* OverrideValue = nullptr;

#if WITH_EDITORONLY_DATA
	/**
	 * Non-identity choice patches found on the chain's entries for this Name, root-most first, enabled or not.
	 * Point into the layers' storage, same lifetime as OverrideValue. Empty for locals.
	 */
	TArray<const FPCGExPropertyChoicesPatch*, TInlineAllocator<2>> ChoicePatches;

	/**
	 * The choices this entry's rows mirror: the declaration's list with ChoicePatches applied, deepest first, carriers
	 * synced to the declaration. What an importer INHERITS before its own rewrite is the same call on a Resolve made
	 * with bIncludeOwnOverrides false.
	 */
	void GetEffectiveChoices(FPCGExPropertyChoices& Out) const;
#endif

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

	/** A copy of the effective property carrying the effective choices: an override row's list is a mirror that may be stale. */
	FInstancedStruct MakeEffectiveCopy() const;
};

/**
 * Effective choice lists of one Resolve() result, each built on first use. Hand the same instance to every row
 * container synced against that result (FPCGExPropertyOverrides::SyncInPlace). An empty shell outside the editor.
 */
struct PCGEXPROPERTIES_API FPCGExResolvedChoicesCache
{
#if WITH_EDITORONLY_DATA
	const FPCGExPropertyChoices& Get(TConstArrayView<FPCGExPropertyResolved> Resolved, int32 Index);

private:
	TArray<FPCGExPropertyChoices> Lists;
	TBitArray<> Built;
#endif
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
 *
 * IMPORT OVERRIDES:
 *
 *   ImportOverrides is EditAnywhere so GetChildHandle can reach its children:
 *   FPCGExPropertySchemaCollectionCustomization renders every row, so it never shows as a
 *   top-level array editor. ReconcileImportOverrides keeps it parallel with the imports-only
 *   schema (through FPCGExPropertyOverrides::SyncToSchema). Per-instance delta serialization
 *   layers it: asset default -> template (collection on CDO) -> instance (collection on actor
 *   instance), each entry's bEnabled deciding whether its layer answers.
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
	 * Per-property value overrides for imported properties, each with its own enable toggle.
	 * Locals are edited in place on Schemas.
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
	 *
	 * Imported assets' own ImportOverrides are layered too: what asset B imports reads
	 * [this collection, fallbacks..., B.ImportOverrides], so B's override of an entry it imports from A
	 * is what an importer of B sees unless the importer overrides it itself. B's own locals never read B's layer.
	 */
	void Resolve(TArray<FPCGExPropertyResolved>& Out, TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {}, bool bIncludeOwnOverrides = true) const;

	/**
	 * Find the DECLARATION of PropertyName (locals first, then imported assets). Ignores every override layer:
	 * use GetPropertyByName for the effective value.
	 */
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
	 * Get the effective property by name, honoring the same composition as Resolve:
	 *   local schemas -> ImportOverrides (if enabled) -> each imported asset's own ImportOverrides, root-most
	 *   first -> the declaring asset's default.
	 * An override surfaces only for a name the import tree still declares.
	 *
	 * Read-only -- safe to call from any thread (see ReconcileImportOverrides contract).
	 */
	const FInstancedStruct* GetPropertyByName(FName PropertyName) const;

	/** Build FInstancedStruct array for SyncToSchema calls. FallbackChain and bIncludeOwnOverrides
	 *  have the same meaning as Resolve's. In the editor every copy carries its effective Choices
	 *  (FPCGExPropertyResolved::GetEffectiveChoices), so rows synced from it mirror the patched list. */
	TArray<FInstancedStruct> BuildSchema(TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {}, bool bIncludeOwnOverrides = true) const;

	/** Validate all LOCAL property names are unique (returns true if valid). Deliberately does not
	 *  span imports: same-name shadowing across layers is the designed first-seen-wins behavior
	 *  (see Resolve); a duplicate among locals is an authoring error. */
	bool ValidateUniqueNames(TArray<FName>& OutDuplicates) const;

	/** Typed DECLARATION lookup (FindByName): ignores override layers. */
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
	 * Ends with SyncDeclaredChoices.
	 */
	void SyncAllSchemas();

	/**
	 * Maintain every local declaration's choices: finish a retype in progress, mint choice ids, re-mirror the carriers'
	 * schema-owned fields and drop a default's pick it no longer stands on. Never writes a value. For hosts whose
	 * schema identity is not theirs to canonicalize (component instances); everyone else gets it from SyncAllSchemas.
	 * No-op outside the editor.
	 *
	 * @return True if anything changed.
	 */
	bool SyncDeclaredChoices();

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

	/** ApplyToOverrides over rows of any type derived from FPCGExPropertyOverrides. True if any row changed. */
	template <typename TRow>
	bool ApplyToRows(TArray<TRow>& Rows) const
	{
		static_assert(TIsDerivedFrom<TRow, FPCGExPropertyOverrides>::Value,
		              "ApplyToRows: TRow must derive from FPCGExPropertyOverrides.");
		const TArray<FInstancedStruct> Schema = BuildSchema();
		bool bChanged = false;
		for (TRow& Row : Rows)
		{
			bChanged |= Row.SyncToSchema(Schema);
		}
		return bChanged;
	}

	/** FPCGExPropertyOverrides::SyncInPlace over such rows, one resolve and one choices cache for all of them. */
	template <typename TRow>
	bool SyncRowsInPlace(TArray<TRow>& Rows) const
	{
		static_assert(TIsDerivedFrom<TRow, FPCGExPropertyOverrides>::Value,
		              "SyncRowsInPlace: TRow must derive from FPCGExPropertyOverrides.");
		TArray<FPCGExPropertyResolved> Resolved;
		Resolve(Resolved);
		FPCGExResolvedChoicesCache ChoicesCache;
		bool bChanged = false;
		for (TRow& Row : Rows)
		{
			bChanged |= Row.SyncInPlace(Resolved, &ChoicesCache);
		}
		return bChanged;
	}

	/**
	 * Load-time heal of the import layer: every imported asset post-loaded first (the engine does not order
	 * referenced-asset PostLoads, and the reconcile reads identities their own PostLoad canonicalizes), then
	 * ReconcileImportOverrides. Nothing can alias the layer at load, so the reshape is safe there. While an import
	 * slot is empty anywhere in the tree (an asset that failed to load leaves one) the layer is only synced in place:
	 * a reshape would drop that asset's rows, which is an edit's decision to make.
	 *
	 * @return True if ImportOverrides changed.
	 */
	bool ReconcileImportOverridesAtLoad();

#if WITH_EDITOR
	/**
	 * Load-time heal of the value rows held against this collection; call it after ReconcileImportOverridesAtLoad.
	 * A row sitting entry for entry on the resolved schema is synced in place; any other is rebuilt (SyncToSchema),
	 * unless an import slot is empty anywhere in the tree: a rebuild would drop that asset's values.
	 * Editor-only: SyncToSchema keeps no value outside the editor.
	 *
	 * @return True if any row changed.
	 */
	template <typename TRow>
	bool SyncRowsAtLoad(TArray<TRow>& Rows) const
	{
		static_assert(TIsDerivedFrom<TRow, FPCGExPropertyOverrides>::Value,
		              "SyncRowsAtLoad: TRow must derive from FPCGExPropertyOverrides.");
		return SyncRowsAtLoad(MakeStridedViewOfBase<FPCGExPropertyOverrides, TRow>(MakeArrayView(Rows)));
	}

	/** SyncRowsAtLoad over rows of any layout derived from FPCGExPropertyOverrides. */
	bool SyncRowsAtLoad(TStridedView<FPCGExPropertyOverrides> Rows) const;
#endif

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

	/**
	 * Overload that accepts a precomputed Resolved view -- avoids re-walking the tree. A host whose schema resolves
	 * through fallback layers passes a view resolved with them, so its rows mirror the patches those layers carry.
	 */
	bool ReconcileImportOverrides(const TArray<FPCGExPropertyResolved>& Resolved);

	/**
	 * In-place counterpart of ReconcileImportOverrides: each import row takes its declaration's name, schema-owned
	 * fields and effective choices (FPCGExPropertyOverrides::SyncInPlace against the imported entries). Never
	 * reshapes ImportOverrides, so it is the one sync that may run on a collection whose panel is not the caller's.
	 * FallbackChain as in Resolve. Game-thread only.
	 *
	 * @return True if any row changed.
	 */
	bool SyncImportOverridesInPlace(TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {});

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
