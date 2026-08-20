// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Lattice/PCGExLatticeBasis.h"

#include "PCGExClusterSnapProvider.generated.h"

/**
 * Provides the snap-lattice model a cluster sketch is authored against: a FPCGExLatticeBasis built from
 * whatever the concrete provider describes (a uniform grid here; an orbital set on the Valency side).
 * Lives instanced ON the sketch and is nullable -- no provider means free-form authoring.
 *
 * Consumed at print/setup time into the POD basis on the game-thread side; parallel work only ever
 * touches the basis, never this object.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PCGEXGRAPHS_API UPCGExClusterSnapProvider : public UObject
{
	GENERATED_BODY()

public:
	/** Build the lattice basis this provider describes. @return false when the config forms no usable lattice. */
	virtual bool BuildBasis(FPCGExLatticeBasis& OutBasis) const PURE_VIRTUAL(UPCGExClusterSnapProvider::BuildBasis, return false;);

	/** Soft references the provider needs loaded before BuildBasis can run off the authoring path. */
	virtual void CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const
	{
	}

#if WITH_EDITOR
	/**
	 * An undo restoring THIS provider's state enrolls only the provider in the transaction, so the owning
	 * sketch's PostEditUndo never fires and bound-vertex locations go stale. Forward to the owner; the
	 * sync is idempotent, so overlap with the owner's own PostEditUndo is free.
	 */
	virtual void PostEditUndo() override;

	/** Any provider-parameter edit moves the lattice; the owner re-derives bound-vertex locations. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/** Axis-aligned (optionally rotated) uniform grid -- the built-in snap model. */
UCLASS(BlueprintType, DisplayName = "Uniform Grid")
class PCGEXGRAPHS_API UPCGExClusterSnapProvider_UniformGrid : public UPCGExClusterSnapProvider
{
	GENERATED_BODY()

public:
	/** Lattice spacing along each enabled axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0001"))
	double CellSize = 100.0;

	/** Lattice origin (a node sits exactly here), in the sketch's authoring space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FVector Origin = FVector::ZeroVector;

	/** Rigid rotation of the whole grid frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FRotator Rotation = FRotator::ZeroRotator;

	/** Which axes the grid spans -- disable one for a planar grid, two for a linear one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bSpanX = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bSpanY = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bSpanZ = true;

	virtual bool BuildBasis(FPCGExLatticeBasis& OutBasis) const override;
};
