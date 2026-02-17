// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExConnectorTransformStrategy.generated.h"

/**
 * Base connector transform strategy (no-op).
 * Subclasses override TransformConnector() to apply different space adjustments.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorTransformStrategy
{
	GENERATED_BODY()

	virtual ~FPCGExConnectorTransformStrategy() = default;

	/** Whether this strategy depends on the asset-to-cage relative transform.
	 *  When true, the scan pipeline must track asset transforms for change detection. */
	virtual bool IsTransformSensitive() const { return false; }

	/**
	 * Called during Compile() per connector. Mutates LocalOffset in-place.
	 * @param InOutOffset The connector's local offset transform to modify
	 * @param AssetRelativeXform The asset-to-cage relative transform
	 */
	virtual void TransformConnector(
		FTransform& InOutOffset,
		const FTransform& AssetRelativeXform) const
	{
		// Base: no-op
	}
};

/**
 * Adjusts connectors from cage-space to mesh-pivot-space using the asset-relative transform.
 * This is the standard behavior when asset origin differs from cage origin.
 */
USTRUCT(BlueprintType, DisplayName = "Asset Relative")
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorTransform_AssetRelative : public FPCGExConnectorTransformStrategy
{
	GENERATED_BODY()

	virtual bool IsTransformSensitive() const override { return true; }

	virtual void TransformConnector(
		FTransform& InOutOffset,
		const FTransform& AssetRelativeXform) const override
	{
		if (!AssetRelativeXform.Equals(FTransform::Identity, KINDA_SMALL_NUMBER))
		{
			const FVector OriginalScale = InOutOffset.GetScale3D();
			InOutOffset = InOutOffset.GetRelativeTransform(AssetRelativeXform);
			InOutOffset.SetScale3D(OriginalScale);
		}
	}
};

/**
 * Keeps connectors in cage-relative space (no adjustment).
 * Use when connector positions are already defined relative to the cage origin.
 */
USTRUCT(BlueprintType, DisplayName = "Cage Relative")
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorTransform_CageRelative : public FPCGExConnectorTransformStrategy
{
	GENERATED_BODY()

	// Inherits base no-op - connectors stay cage-relative
};
