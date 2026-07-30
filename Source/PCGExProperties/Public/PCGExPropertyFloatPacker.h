// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExPropertyFloatPacker.generated.h"

class UPCGExPropertySchemaAsset;

// Mesh selectors run inside stock PCG elements, so this can't assume a PCGEx-owned context.
struct FPCGContext;

/** One property's position in a packed float array. */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPackedFloatSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bEnabled = true;

	/** Schema property to read; its type decides how many floats the slot occupies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta=(EditCondition="bEnabled"))
	FName PropertyName;

	/** Start index, or -1 to append after the previous slot. Pin it when a shader reads a fixed index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta=(EditCondition="bEnabled", ClampMin=-1, UIMin=-1))
	int32 Offset = -1;
};

/**
 * Authored packed-float layout: row order is pack order, with optional per-row pinning.
 *
 * Positions are authored rather than derived because consumers index these arrays positionally --
 * a material reading Custom Primitive Data slot 3 cannot notice the property there changed. Config
 * order, schema array order and host order all get edited for unrelated reasons, so none of them
 * can be allowed to own a slot's position. Pinned rows are immune to everything above them, which
 * is also what lets an unresolved row be skipped without relocating the rest.
 *
 * Schema assets expand into real rows at author time, so editing a shared schema can never
 * silently re-lay-out a graph that referenced it.
 */
USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPackedFloatLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(TitleProperty="{PropertyName}"))
	TArray<FPCGExPackedFloatSlot> Slots;

	/** Silences the not-declared warning -- expected when one layout spans differing collection schemas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bQuietMissingPropertyWarnings = false;

#if WITH_EDITORONLY_DATA
	/** Assign to append its properties as rows; cleared on use, so it never becomes a live dependency. */
	UPROPERTY(EditAnywhere, Transient, Category = Settings, meta=(DisplayName="Populate From"))
	TObjectPtr<UPCGExPropertySchemaAsset> PopulateFromSchema;
#endif

	bool IsEmpty() const { return Slots.IsEmpty(); }

#if WITH_EDITOR
	/** Appends one row per resolved schema property, skipping names already present.
	 *  @return how many rows were added. */
	int32 EDITOR_PopulateFromSchemaAsset(const UPCGExPropertySchemaAsset* InAsset);
#endif
};

/**
 * Projects schema properties into a flat float array following an authored layout.
 * Float-array sibling of FPCGExPropertySetWriter (metadata) and FPCGExPropertyWriter (buffers).
 *
 *   Packer.Initialize(Ctx, Provider, Layout, MaxFloats);          // single-threaded boot
 *   Packer.Pack(Floats, [&](FName N){ return ResolveFor(N); });   // per source
 *
 * Only widths and prototype values come from the schema; positions come from the layout. So an
 * unresolvable row is skipped rather than closing the gap it held, and that is fatal only when a
 * later row is auto-positioned, since that offset genuinely depended on the missing one.
 * Overlapping rows are an error rather than a silent overwrite.
 *
 * Unresolved slots keep the prototype's packed value, so a missing per-source override reads as
 * the schema default rather than zero. A differently-typed source is rejected because equal width
 * does not imply equal semantics -- Vector and Rotator are both 3 floats, Vector4/Quat/Color all 4.
 */
class PCGEXPROPERTIES_API FPCGExPropertyFloatPacker
{
public:
	FPCGExPropertyFloatPacker() = default;

	/**
	 * Resolve widths and prototype values for an authored layout.
	 *
	 * MaxFloats is the consumer's hardware limit (e.g. FCustomPrimitiveData::NumCustomPrimitiveDataFloats);
	 * rows that don't fit whole are dropped so GetNumFloats() never reports unstorable capacity.
	 * Provider is not retained past this call.
	 * @return true if a usable layout with at least one slot was resolved.
	 */
	bool Initialize(
		FPCGContext* InContext,
		const IPCGExPropertyProvider* InProvider,
		const FPCGExPackedFloatLayout& Layout,
		int32 MaxFloats = MAX_int32);

	/** Packed width, gaps included. A row that resolved to nothing does not extend it. */
	int32 GetNumFloats() const { return Defaults.Num(); }

	bool HasOutputs() const { return !Slots.IsEmpty(); }

	int32 GetNumSlots() const { return Slots.Num(); }
	FName GetSlotPropertyName(const int32 SlotIndex) const { return Slots[SlotIndex].PropertyName; }

	/** Fill OutFloats from ResolveSource. A null or differently-typed source keeps the prototype default. */
	void Pack(TArray<float>& OutFloats, TFunctionRef<const FInstancedStruct*(FName)> ResolveSource) const;

#if WITH_EDITOR
	/** Slot map for diagnostics, e.g. "[0..2] Tint (Color) | [3] Roughness (Float)". */
	FString DescribeLayout() const;
#endif

protected:
	struct FSlot
	{
		FName PropertyName;
		/** Equal float width isn't enough to make two types interchangeable, so writes match on this. */
		const UScriptStruct* StructType = nullptr;
#if WITH_EDITORONLY_DATA
		FName TypeName;
#endif
		int32 Offset = 0;
		int32 Count = 0;
	};

	TArray<FSlot> Slots;

	/** Prototype-packed at full width; Pack() copies this so the fallback is one memcpy, not a branch. */
	TArray<float> Defaults;
};
