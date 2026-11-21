// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFittingCommon.h"
#include "PCGExFittingVariations.generated.h"

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExFittingVariations
{
	GENERATED_BODY()

	FPCGExFittingVariations() = default;

	/** Minimum random offset per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector OffsetMin = FVector::ZeroVector;

	/** Maximum random offset per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector OffsetMax = FVector::ZeroVector;

	/** How to snap the random offset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVariationSnapping SnapPosition = EPCGExVariationSnapping::None;

	/** Grid size for offset snapping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector OffsetSnap = FVector(100);

	/** Apply offset in world space instead of local space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAbsoluteOffset = false;

	/** Minimum random rotation per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FRotator RotationMin = FRotator::ZeroRotator;

	/** Maximum random rotation per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FRotator RotationMax = FRotator::ZeroRotator;

	/** How to snap the random rotation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVariationSnapping SnapRotation = EPCGExVariationSnapping::None;

	/** Angular increment for rotation snapping (degrees). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FRotator RotationSnap = FRotator(90);

	/** Replace rotation instead of adding to it on the selected axes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExAbsoluteRotationFlags"))
	uint8 AbsoluteRotation = 0;

	/** Minimum random scale per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (AllowPreserveRatio, PCG_Overridable))
	FVector ScaleMin = FVector::OneVector;

	/** Maximum random scale per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (AllowPreserveRatio, PCG_Overridable))
	FVector ScaleMax = FVector::OneVector;

	/** How to snap the random scale. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVariationSnapping SnapScale = EPCGExVariationSnapping::None;

	/** Increment for scale snapping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector ScaleSnap = FVector(0.1);

	/** Use same scale on all axes (X component only). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bUniformScale = true;

	void ApplyOffset(const FRandomStream& RandomStream, FTransform& OutTransform) const;
	void ApplyRotation(const FRandomStream& RandomStream, FTransform& OutTransform) const;
	void ApplyScale(const FRandomStream& RandomStream, FTransform& OutTransform) const;
};
