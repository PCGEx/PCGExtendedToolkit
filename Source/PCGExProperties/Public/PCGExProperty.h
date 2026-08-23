// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExData.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/PCGExTypeTraits.h"

#include "PCGExPropertySchema.h"

#include "PCGExProperty.generated.h"

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
 *        TSharedPtr<PCGExData::TBuffer<FMyOutputType>> OutputBuffer;  // may differ from Value's type
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
 *
 * B) METADATA ATTRIBUTE OUTPUT (via Tuple node):
 *    CreateMetadataAttribute() -> creates attribute on UPCGParamData
 *    WriteMetadataValue()      -> writes Value to a metadata entry key
 *
 * Both paths are optional. Return false/nullptr from SupportsOutput()/
 * CreateMetadataAttribute() if your type doesn't support a path.
 *
 * C) TIME-BASED SAMPLING (via SupportsSampling / SampleAt):
 *    Orthogonal to A/B -- a value-only type (e.g. Float Curve) can be sampleable
 *    while SupportsOutput() stays false. Consumers read a per-point time, call
 *    SampleAt(time) on the resolved source property, and write the double result
 *    into their own output buffer.
 *
 * D) FLOAT PACKING (via GetPackedFloatCount / PackFloats):
 *    Projects the value into a flat float array -- the shape GPU-side consumers want
 *    (ISM Custom Primitive Data, custom float instance data). Also orthogonal to A/B:
 *    the default derives everything from GetOutputType(), so every type that reports
 *    a packable output type works with no per-type code.
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

	// --- Output naming & sidecars ---

	/**
	 * Final attribute name for this property's output, given the effective (validated) config name.
	 * Identity by default; types that write under a derived/namespaced name override. Every site that
	 * creates an output for a property MUST route the name through here.
	 */
	virtual FName ResolveOutputAttributeName(const FName InEffectiveName) const
	{
		return InEffectiveName;
	}

	/**
	 * Pin label of the attribute set this property contributes side data to (PCGExProperties::Labels),
	 * NAME_None when it contributes nothing. Writers group contributions per pin; the hosting node
	 * allocates the data and stages it.
	 */
	virtual FName GetOutputSidecarPin() const
	{
		return NAME_None;
	}

	/**
	 * Append this property's sidecar rows to the pin's attribute set. Must be idempotent across
	 * instances contributing to the same data (dedupe against existing rows).
	 */
	virtual void WriteOutputSidecar(UPCGMetadata* InSidecar) const
	{
	}

	/**
	 * Assets that must be RESIDENT for this property to write its output (not its value's own soft paths --
	 * see GatherSoftObjectPaths). Hosting nodes register these as async asset dependencies before writing.
	 */
	virtual void GatherOutputDependencies(TSet<FSoftObjectPath>& OutPaths) const
	{
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

	// --- Float Packing Interface ---

	/**
	 * How many floats PackFloats writes; 0 means no float projection at all.
	 *
	 * Must be constant for a given property *type*. Consumers resolve a slot layout from schema
	 * prototypes once and then fill it per entry, so a count that varied with the authored value
	 * would shift every slot after it.
	 */
	virtual int32 GetPackedFloatCount() const;

	/**
	 * Write this property's value into OutFloats; false means no value was produced and the slot
	 * has no data. A type can report a packable count and still fail here -- reporting a width it
	 * never fills would bake a silent zero into the layout.
	 *
	 * Component order matches the engine packers: Vector2 (X,Y), Vector (X,Y,Z),
	 * Vector4/Quat (X,Y,Z,W), Rotator (Roll, Pitch, Yaw).
	 *
	 * Override alongside GetPackedFloatCount for types with no packable GetOutputType but a float
	 * layout of their own. An override MUST refuse to write when OutFloats is narrower than what
	 * it intends to emit: the two virtuals are coupled only by convention, and callers size the
	 * view from GetPackedFloatCount().
	 */
	virtual bool PackFloats(TArrayView<float> OutFloats) const;

	/**
	 * Append every soft asset path this property contributes (its FSoftObjectPath /
	 * FSoftClassPath value). Default no-op; concrete soft-path property types
	 * (FPCGExProperty_SoftObjectPath, FPCGExProperty_SoftClassPath) override to emit theirs.
	 *
	 * Runtime-safe -- a plain value read with no editor dependency. Drives both the cook-time
	 * dependency walk (IPCGExCookDependencyProvider) and runtime resource preloading, so it is
	 * intentionally NOT editor-gated.
	 */
	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const
	{
	}

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
	namespace Labels
	{
		/** Attribute set describing external resources referenced by property values (e.g. a collection map). */
		const FName OutputMapLabel = TEXT("Map");
	}

	/**
	 * Floats a value of InType projects to; 0 for types with no projection.
	 *
	 * Value-identical to the engine's PCGInstanceDataPackerBase::GetTypePackingSize so packed
	 * layouts interoperate with the stock PCG packers, but owned here on purpose: that table is
	 * PCG-private with no compatibility guarantee, and inheriting it would let an engine upgrade
	 * silently re-lay-out every packed float consumer. Keep them in sync deliberately.
	 */
	PCGEXPROPERTIES_API int32 GetPackedFloatWidth(EPCGMetadataTypes InType);

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

	/** Same walks as GatherSoftObjectPaths, collecting FPCGExProperty::GatherOutputDependencies instead. */
	PCGEXPROPERTIES_API void GatherOutputDependencies(const FPCGExPropertyOverrides& Overrides, TSet<FSoftObjectPath>& OutPaths);
	PCGEXPROPERTIES_API void GatherOutputDependencies(const FPCGExPropertySchemaCollection& Collection, TSet<FSoftObjectPath>& OutPaths);

	/**
	 * Effective property for InName: the overrides' enabled entry when there is one, else the schema's
	 * own declared value -- GetOverride returns null for a disabled entry and does not fall through.
	 *
	 * Null ONLY when the schema does not declare InName, and means "skip this row", never "write
	 * nothing": FPCGExPropertyWriter::WriteProperties flushes its writer instance unconditionally, so
	 * a skipped copy republishes the previous row's value.
	 *
	 * Read-only -- safe off the game thread under the same conditions as GetPropertyByName (no
	 * concurrent Reconcile).
	 */
	PCGEXPROPERTIES_API const FInstancedStruct* ResolveEffective(
		const FPCGExPropertySchemaCollection& InSchema, const FPCGExPropertyOverrides* InOverrides, FName InName);
}
