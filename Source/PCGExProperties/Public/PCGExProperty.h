// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExData.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/PCGExTypeTraits.h"

#include "PCGExProperty.generated.h"

class UPCGExPropertySchemaAsset;

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
 * Base struct for all PCGEx property types.
 *
 * ============================================================================
 * CREATING A CUSTOM PROPERTY TYPE
 * ============================================================================
 *
 * To add a new property type that integrates with the entire plugin:
 *
 * 1. DEFINE your struct deriving from FPCGExProperty in PCGExPropertyTypes.h
 *    (or in your own module if it has special dependencies):
 *
 *    USTRUCT(BlueprintType, meta=(PCGExInlineValue))
 *    struct FPCGExProperty_MyType : public FPCGExProperty
 *    {
 *        GENERATED_BODY()
 *
 *        UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property")
 *        FMyValueType Value;  // <-- your authored value
 *
 *    protected:
 *        TSharedPtr<PCGExData::TBuffer<FMyOutputType>> OutputBuffer;
 *        // Note: OutputType can differ from Value type (see Color: FLinearColor -> FVector4)
 *
 *    public:
 *        // Override ALL virtual methods listed below.
 *    };
 *
 * 2. IMPLEMENT the methods. For simple 1:1 type mappings, use PCGEX_PROPERTY_IMPL
 *    macro in the .cpp (see PCGExPropertyTypes.cpp). For type conversions, implement
 *    each method manually (see Color and Enum for examples).
 *
 * 3. The USTRUCT meta=(PCGExInlineValue) tag is optional - it controls how the
 *    property appears in the FInstancedStruct picker UI.
 *
 * 4. No registration step is needed. UHT discovers the USTRUCT automatically.
 *    Your type will appear in any FInstancedStruct picker that uses
 *    meta=(BaseStruct="/Script/PCGExProperties.PCGExProperty").
 *
 * ============================================================================
 * TWO OUTPUT PATHS
 * ============================================================================
 *
 * Properties support two independent output mechanisms:
 *
 * A) POINT ATTRIBUTE OUTPUT (via FPCGExPropertyWriter):
 *    InitializeOutput() -> creates a TBuffer on a facade
 *    WriteOutput()      -> writes Value to buffer at a point index
 *    WriteOutputFrom()  -> writes from source property directly (thread-safe)
 *    Used by: Collections, Distribute Tuple, any node outputting properties to points
 *
 * B) METADATA ATTRIBUTE OUTPUT (via Tuple node):
 *    CreateMetadataAttribute() -> creates attribute on UPCGParamData
 *    WriteMetadataValue()      -> writes Value to a metadata entry key
 *    Used by: Tuple node for creating param data tables
 *
 * Both paths are optional. Return false/nullptr from SupportsOutput()/
 * CreateMetadataAttribute() if your type doesn't support a path.
 *
 * C) TIME-BASED SAMPLING (via SupportsSampling / SampleAt):
 *    Orthogonal to A/B -- a value-only type (e.g. Float Curve) can be sampleable
 *    while SupportsOutput() stays false. Consumers read a per-point time, call
 *    SampleAt(time) on the resolved source property, and write the double result
 *    into their own output buffer.
 *    Used by: Staging : Load Properties (Sampled Properties Mapping)
 *
 * ============================================================================
 * THREAD SAFETY
 * ============================================================================
 *
 * - WriteOutputFrom() is the ONLY output method safe for parallel processing loops.
 *   It reads from Source and writes directly to the buffer without mutating 'this'.
 * - SampleAt() MUST be const, re-entrant and mutation-free -- it is called per-point
 *   from parallel loops on a shared, immutable property instance.
 * - CopyValueFrom() + WriteOutput() is NOT thread-safe (mutates Value field).
 *   Only use this pattern in single-threaded contexts.
 * - InitializeOutput() must be called during boot phase (single-threaded).
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExProperty
{
	GENERATED_BODY()

	/**
	 * User-defined name for disambiguation when multiple properties exist.
	 * This name is used to match properties across schemas, overrides, and output configs.
	 * Must be unique within a schema collection.
	 */
	UPROPERTY()
	FName PropertyName;

#if WITH_EDITORONLY_DATA
	/**
	 * Stable identity for override matching across schema changes.
	 * Auto-generated on construction, preserved by FPCGExPropertySchema through:
	 * - Property renames (HeaderId stays same, overrides follow)
	 * - Property reordering (HeaderId stays same, values stay correct)
	 * - Type changes (HeaderId preserved, bEnabled state preserved, value reset to default)
	 * Custom properties inherit this automatically - no action needed.
	 */
	UPROPERTY()
	int32 HeaderId = 0;
#endif

	// HeaderId is left at 0 by the ctor; assigning it here would defeat UE's CDO->instance
	// propagation for arrays-of-structs (fresh values on every default-construction that
	// UE then never overrides). FPCGExPropertySchemaCollection::SyncAllSchemas assigns it.
	FPCGExProperty() = default;

	virtual ~FPCGExProperty() = default;

	// --- Output Interface ---

	/**
	 * Initialize output buffer(s) on the facade.
	 * Override in derived types that support output.
	 * @param OutputFacade The facade to create buffers on
	 * @param OutputName The attribute name to use
	 * @return true if initialization succeeded
	 */
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName)
	{
		return false;
	}

	/**
	 * Write this property's value(s) to the initialized buffer(s).
	 * Call after InitializeOutput() succeeded.
	 * WARNING: Not thread-safe if Value was modified. Use WriteOutputFrom() for parallel processing.
	 * @param PointIndex The point index to write to
	 */
	virtual void WriteOutput(int32 PointIndex) const
	{
	}

	/**
	 * Thread-safe: Write value from source property directly to buffer.
	 * Use this in parallel processing (PCGEX_SCOPE_LOOP) instead of CopyValueFrom + WriteOutput.
	 * @param PointIndex The point index to write to
	 * @param Source The source property to read value from (must be same concrete type)
	 */
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const
	{
	}

	/**
	 * Copy value from another property of the same type.
	 * WARNING: Not thread-safe. Mutates this property's Value field.
	 * For parallel processing, use WriteOutputFrom() instead.
	 * @param Source The source property to copy from (must be same concrete type)
	 */
	virtual void CopyValueFrom(const FPCGExProperty* Source)
	{
	}

	/**
	 * Check if this property type supports attribute output.
	 */
	virtual bool SupportsOutput() const
	{
		return false;
	}

	/**
	 * Get the PCG metadata type for this property (for UI/validation).
	 * Return EPCGMetadataTypes::Unknown if not applicable or multi-valued.
	 */
	virtual EPCGMetadataTypes GetOutputType() const
	{
		return EPCGMetadataTypes::Unknown;
	}

	// --- Sampling Interface ---

	/**
	 * Check if this property type supports time-based sampling (see SampleAt).
	 * Orthogonal to SupportsOutput() -- value-only types (e.g. Float Curve) can be
	 * sampleable without supporting raw attribute output.
	 */
	virtual bool SupportsSampling() const
	{
		return false;
	}

	/**
	 * Sample this property's value at InTime.
	 * The time domain is type-defined -- curve-backed types use their own key range and
	 * extrapolation settings; out-of-range times are NOT clamped here.
	 *
	 * THREAD SAFETY: must be const, re-entrant and mutation-free (including mutable
	 * caches) -- called per-point from parallel processing loops on a shared, immutable
	 * property instance.
	 *
	 * @param InTime The time to sample at
	 * @return Sampled value; base implementation returns 0.
	 */
	virtual double SampleAt(const double InTime) const
	{
		return 0.0;
	}

	/**
	 * Get the human-readable type name for this property (e.g., "String", "Int32", "Vector").
	 * Used for registry display.
	 */
	virtual FName GetTypeName() const
	{
		return FName("Unknown");
	}

	/**
	 * Like GetTypeName but used for UI labels that show what the property actually targets.
	 * Defaults to GetTypeName; types that carry a structural narrowing field (e.g. soft
	 * paths with AllowedClass) override this to return the narrowed class name instead
	 * of the generic "SoftObjectPath" / "SoftClassPath" string.
	 *
	 * Kept separate from GetTypeName so the registry / serialization side keeps a stable
	 * type identifier regardless of editor-only narrowing fields.
	 */
	virtual FName GetDisplayTypeName() const
	{
		return GetTypeName();
	}

	// --- Metadata Interface (for Tuple/ParamData) ---

	/**
	 * Create a metadata attribute on param data.
	 * Override in derived types that support metadata output (most types do).
	 * @param Metadata The metadata to create attribute on
	 * @param AttributeName The attribute name to use
	 * @return Pointer to created attribute, or nullptr if failed
	 */
	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const
	{
		return nullptr;
	}

	/**
	 * Write this property's value to a metadata attribute.
	 * @param Attribute The attribute to write to (must match type)
	 * @param EntryKey The metadata entry key to write to
	 */
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const
	{
	}

	/**
	 * Copy default value from another property (for Tuple header initialization).
	 * Similar to CopyValueFrom but called during header initialization.
	 * @param Source The source property to copy from
	 */
	virtual void InitializeFrom(const FPCGExProperty* Source)
	{
		CopyValueFrom(Source);
	}

	/**
	 * Copy "structural" sub-fields from a sibling schema entry.
	 *
	 * For property types whose Value contains both schema-owned (structural) and
	 * user-overridable parts, this hook is invoked from FPCGExPropertyOverrides::SyncToSchema
	 * on every preserved override, so the structural parts continue to mirror the schema
	 * while the user-overridable parts stay intact.
	 *
	 * Default implementation is a no-op -- types whose Value is entirely user-overridable
	 * (the common case) need not override this.
	 *
	 * Example: FPCGExProperty_Enum's Value.Class is structural (the schema decides the
	 * enum type), but Value.Value (the int64 selection) is user-overridable.
	 *
	 * @param Schema The matching schema-side property to copy structural fields from.
	 *               Same script struct as `this` is guaranteed by the caller.
	 * @return       True if any field was changed. Lets SyncToSchema report whether the
	 *               reconcile actually touched override state (drives PostLoad's dirty gate).
	 */
	virtual bool SyncStructuralFromSchema(const FPCGExProperty& Schema)
	{
		return false;
	}

	// --- Value Read Interface (type-erased) ---

	/**
	 * Type-erased value read: converts this property's effective value to TargetType,
	 * writing into OutBuffer. OutBuffer must point to a storage location of TargetType.
	 *
	 * For simple properties (Value type matches a supported PCG type), the default
	 * macro implementation dispatches through FConversionTable, giving free N×N
	 * conversion across all supported types.
	 *
	 * Converting properties (e.g., Color: FLinearColor -> FVector4, Enum: FPCGExEnumSelector
	 * -> int64) override this manually to first project to their output type, then
	 * dispatch the conversion.
	 *
	 * Returns false if this property does not expose a convertible value.
	 *
	 * Prefer the templated TryGetValue<T> wrapper at call sites.
	 */
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const
	{
		return false;
	}

	/**
	 * Templated value read: resolves T to its EPCGMetadataTypes at compile time and
	 * forwards to TryWriteValue. Returns false if T is not a supported PCG type or
	 * the property has no convertible value.
	 */
	template <typename T>
	bool TryGetValue(T& Out) const
	{
		static_assert(PCGExTypes::TTraits<T>::Type != EPCGMetadataTypes::Unknown,
		              "TryGetValue<T>: T must be a PCG-supported metadata type.");
		return TryWriteValue(PCGExTypes::TTraits<T>::Type, &Out);
	}

	/**
	 * Type-erased value write: read InBuffer as SourceType and store it in this property's
	 * authored Value. Inverse of TryWriteValue; uses the same FConversionTable for N×N
	 * conversion. Converting properties (Color, Enum) override this manually to first
	 * receive the buffer as their projection type, then assign back to Value.
	 *
	 * Returns false if this property does not accept a value write.
	 *
	 * Prefer the templated TrySetValue<T> wrapper at call sites.
	 */
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer)
	{
		return false;
	}

	/**
	 * Templated value write: resolves T to its EPCGMetadataTypes at compile time and
	 * forwards to TryReadValue. Returns false if T is not a supported PCG type or the
	 * property does not accept a value write.
	 */
	template <typename T>
	bool TrySetValue(const T& In)
	{
		static_assert(PCGExTypes::TTraits<T>::Type != EPCGMetadataTypes::Unknown,
		              "TrySetValue<T>: T must be a PCG-supported metadata type.");
		return TryReadValue(PCGExTypes::TTraits<T>::Type, &In);
	}

	/**
	 * Append every soft asset path this property contributes (its FSoftObjectPath /
	 * FSoftClassPath value). Default no-op; concrete soft-path property types
	 * (FPCGExProperty_SoftObjectPath, FPCGExProperty_SoftClassPath) override to emit theirs.
	 *
	 * Runtime-safe -- a plain value read with no editor dependency. Drives both the cook-time
	 * dependency walk (IPCGExCookDependencyProvider) and runtime resource preloading, so it is
	 * intentionally NOT editor-gated.
	 */
	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const {}

	// --- Registry ---

	/**
	 * Create a registry entry for this property.
	 */
	FPCGExPropertyRegistryEntry ToRegistryEntry() const
	{
		return FPCGExPropertyRegistryEntry(PropertyName, GetTypeName(), GetOutputType(), SupportsOutput(), SupportsSampling());
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

#if WITH_EDITORONLY_DATA
	// Copy inner FPCGExProperty identity (PropertyName, HeaderId) into the outer cache fields.
	// Idempotent; safe to call any time Value has been assigned a non-default content.
	void SeedOuterIdentityFromInner()
	{
		if (const FPCGExProperty* Prop = Value.GetPtr<FPCGExProperty>())
		{
			PropertyName = Prop->PropertyName;
			HeaderId = Prop->HeaderId;
		}
	}
#endif

	FName GetPropertyName() const
	{
#if WITH_EDITORONLY_DATA
		if (!PropertyName.IsNone())
		{
			return PropertyName;
		}
#endif
		if (const FPCGExProperty* Prop = Value.GetPtr<FPCGExProperty>())
		{
			return Prop->PropertyName;
		}
		return NAME_None;
	}

	/** Get the property from Value (may be nullptr) */
	const FPCGExProperty* GetProperty() const
	{
		return Value.GetPtr<FPCGExProperty>();
	}

	FPCGExProperty* GetPropertyMutable()
	{
		return Value.GetMutablePtr<FPCGExProperty>();
	}

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
 * Schema Source: The editor customization looks for a "CollectionProperties" or "Properties"
 * property on the outer object to determine available property types for the picker.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyOverrides
{
	GENERATED_BODY()

	/** Overrides array - parallel with schema (same size, same order) */
	UPROPERTY(EditAnywhere, Category = Settings)
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

	FPCGExPropertySchema(); // Implemented in .cpp (needs full type definitions)

	/** Sync Name to Property.PropertyName and HeaderId */
	void SyncPropertyName()
	{
		if (FPCGExProperty* Prop = GetPropertyMutable())
		{
			Prop->PropertyName = Name;
#if WITH_EDITOR
			Prop->HeaderId = HeaderId;
#endif
		}
	}

	/** Get the property from Property (may be nullptr) */
	const FPCGExProperty* GetProperty() const
	{
		return Property.GetPtr<FPCGExProperty>();
	}

	FPCGExProperty* GetPropertyMutable()
	{
		return Property.GetMutablePtr<FPCGExProperty>();
	}

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

	/** Validate all property names are unique (returns true if valid) */
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

/**
 * Query helpers for accessing properties from FInstancedStruct arrays.
 * All functions accept TConstArrayView to work with both TArray and TArrayView.
 *
 * These are the primary runtime lookup functions. Use them when you have
 * a flat array of FInstancedStruct (e.g., from BuildSchema() or a provider).
 *
 * For lookups on FPCGExPropertySchemaCollection, prefer its member methods
 * (FindByName, GetProperty<T>) which operate on the schema directly.
 */
namespace PCGExProperties
{
	/**
	 * Get first property of specified type, optionally filtered by name.
	 * @param Properties - Array view of FInstancedStruct containing properties
	 * @param PropertyName - Optional name filter (NAME_None matches any)
	 * @return Pointer to property if found, nullptr otherwise
	 */
	template <typename T>
	const T* GetProperty(TConstArrayView<FInstancedStruct> Properties, FName PropertyName = NAME_None)
	{
		static_assert(TIsDerivedFrom<T, FPCGExProperty>::Value,
		              "T must derive from FPCGExProperty");

		for (const FInstancedStruct& Prop : Properties)
		{
			if (const T* Typed = Prop.GetPtr<T>())
			{
				if (PropertyName.IsNone() || Typed->PropertyName == PropertyName)
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Get all properties of specified type.
	 * @param Properties - Array view of FInstancedStruct containing properties
	 * @return Array of pointers to matching properties
	 */
	template <typename T>
	TArray<const T*> GetAllProperties(TConstArrayView<FInstancedStruct> Properties)
	{
		static_assert(TIsDerivedFrom<T, FPCGExProperty>::Value,
		              "T must derive from FPCGExProperty");

		TArray<const T*> Result;
		for (const FInstancedStruct& Prop : Properties)
		{
			if (const T* Typed = Prop.GetPtr<T>())
			{
				Result.Add(Typed);
			}
		}
		return Result;
	}

	/**
	 * Get property by name regardless of type.
	 * @param Properties - Array view of FInstancedStruct containing properties
	 * @param PropertyName - Name to search for
	 * @return Pointer to FInstancedStruct if found, nullptr otherwise
	 */
	inline const FInstancedStruct* GetPropertyByName(TConstArrayView<FInstancedStruct> Properties, FName PropertyName)
	{
		if (PropertyName.IsNone())
		{
			return nullptr;
		}

		for (const FInstancedStruct& Prop : Properties)
		{
			if (const FPCGExProperty* Base = Prop.GetPtr<FPCGExProperty>())
			{
				if (Base->PropertyName == PropertyName)
				{
					return &Prop;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Check if properties array contains a property with given name.
	 */
	inline bool HasProperty(TConstArrayView<FInstancedStruct> Properties, FName PropertyName)
	{
		return GetPropertyByName(Properties, PropertyName) != nullptr;
	}

	/**
	 * Check if properties array contains any property of given type.
	 */
	template <typename T>
	bool HasPropertyOfType(TConstArrayView<FInstancedStruct> Properties)
	{
		return GetProperty<T>(Properties) != nullptr;
	}

	/**
	 * Build a registry from an array of property instanced structs.
	 * @param Properties - Array of FInstancedStruct containing FPCGExProperty derivatives
	 * @param OutRegistry - Output array to populate with registry entries
	 */
	inline void BuildRegistry(TConstArrayView<FInstancedStruct> Properties, TArray<FPCGExPropertyRegistryEntry>& OutRegistry)
	{
		OutRegistry.Empty(Properties.Num());
		for (const FInstancedStruct& Prop : Properties)
		{
			if (const FPCGExProperty* Base = Prop.GetPtr<FPCGExProperty>())
			{
				OutRegistry.Add(Base->ToRegistryEntry());
			}
		}
	}

	/**
	 * Gather every FSoftObjectPath / FSoftClassPath value carried by a property graph. Drives
	 * both the cook-time dependency walk (IPCGExCookDependencyProvider) and runtime resource
	 * preloading, so it is intentionally NOT editor-gated.
	 *
	 * The schema-collection overload is cycle-safe: it threads a Visited set through
	 * recursive descent into ImportedSchemas (UPCGExPropertySchemaAsset entries), so an
	 * A->B->A loop in imports doesn't reprocess A.
	 *
	 * Accumulate into OutPaths -- do not clear -- so callers can batch across sources.
	 * For a single property in hand, call its virtual GatherSoftObjectPaths directly.
	 */
	PCGEXPROPERTIES_API void GatherSoftObjectPaths(const FPCGExPropertyOverrides& Overrides, TSet<FSoftObjectPath>& OutPaths);
	PCGEXPROPERTIES_API void GatherSoftObjectPaths(const FPCGExPropertySchemaCollection& Collection, TSet<FSoftObjectPath>& OutPaths);
}
