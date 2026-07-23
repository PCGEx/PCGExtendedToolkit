// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExStagingBoundsModifier.generated.h"

/**
 * Lightweight polymorphic base for staging-bounds modifiers.
 *
 * Held in an FInstancedStruct on FPCGExAssetStagingData (BoundsStagingModifier). When set,
 * FCache::RegisterEntry runs ComputeAlteredBounds() to derive the entry's AlteredBounds -- the
 * bounds used for fitting, spacing and best-fit selection -- from its original, asset-derived
 * Bounds. The bounds applied to the mesh itself always remain the original ones; this only
 * affects fitting.
 *
 * Adding a modifier:
 * 1. Subclass this struct, add your UPROPERTY knobs.
 * 2. Override Modify() to return the altered box.
 * No registration step is needed -- UHT discovers the USTRUCT automatically and it appears in
 * any picker constrained to meta=(BaseStruct=".../PCGExStagingBoundsModifier").
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExStagingBoundsModifier
{
	GENERATED_BODY()

	virtual ~FPCGExStagingBoundsModifier() = default;

	/**
	 * Sanitized entry point used by callers (e.g. FCache::RegisterEntry). Runs Modify(), then
	 * enforces the AlteredBounds invariant: an invalid input box, or a result that is unset or
	 * inverted (Min > Max on any axis -- a negative Scale component, an over-contracting Pad,
	 * crossed Offsets), falls back to InBounds. Degenerate-but-planar boxes (Min == Max) are
	 * kept. Non-virtual: every modifier shares this guarantee and cannot opt out of it.
	 */
	FBox ComputeAlteredBounds(const FBox& InBounds) const;

protected:
	/** Express the bounds transform. Identity by default. Result is sanitized by ComputeAlteredBounds(). */
	virtual FBox Modify(const FBox& InBounds) const;
};

/**
 * Adds independent offsets to the box Min and Max corners.
 * Result = FBox(Min + OffsetMin, Max + OffsetMax).
 */
USTRUCT(BlueprintType, DisplayName="Offset Min/Max")
struct PCGEXCOLLECTIONS_API FPCGExStagingBoundsModifierOffset : public FPCGExStagingBoundsModifier
{
	GENERATED_BODY()

	/** Offset added to Bounds.Min. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector OffsetMin = FVector::ZeroVector;

	/** Offset added to Bounds.Max. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector OffsetMax = FVector::ZeroVector;

protected:
	virtual FBox Modify(const FBox& InBounds) const override;
};

/**
 * Multiplies the box extents by a per-axis factor, pivoting about the box center.
 * The center is preserved; the box grows/shrinks symmetrically.
 */
USTRUCT(BlueprintType, DisplayName="Scale")
struct PCGEXCOLLECTIONS_API FPCGExStagingBoundsModifierScale : public FPCGExStagingBoundsModifier
{
	GENERATED_BODY()

	/** Per-axis multiplier applied to the box extents about its center. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector Scale = FVector::OneVector;

protected:
	virtual FBox Modify(const FBox& InBounds) const override;
};

/**
 * Expands (or contracts) the box by a symmetric per-axis margin on all sides.
 * Result = FBox(Min - Amount, Max + Amount). Negative components shrink the box.
 */
USTRUCT(BlueprintType, DisplayName="Pad / Expand")
struct PCGEXCOLLECTIONS_API FPCGExStagingBoundsModifierPad : public FPCGExStagingBoundsModifier
{
	GENERATED_BODY()

	/** Per-axis amount pushed outward on every side (Min by -Amount, Max by +Amount). */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector Amount = FVector::ZeroVector;

protected:
	virtual FBox Modify(const FBox& InBounds) const override;
};

/**
 * Scales the box extents about its center, then offsets the center.
 * Result = FBox((Center + Offset) - Extent * Scale, (Center + Offset) + Extent * Scale).
 * A single-op scale+offset (as opposed to chaining a Scale and an Offset modifier).
 */
USTRUCT(BlueprintType, DisplayName="Scale + Offset")
struct PCGEXCOLLECTIONS_API FPCGExStagingBoundsModifierScaleOffset : public FPCGExStagingBoundsModifier
{
	GENERATED_BODY()

	/** Per-axis multiplier applied to the box extents about its center. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector Scale = FVector::OneVector;

	/** Offset added to the box center after scaling. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector Offset = FVector::ZeroVector;

protected:
	virtual FBox Modify(const FBox& InBounds) const override;
};
