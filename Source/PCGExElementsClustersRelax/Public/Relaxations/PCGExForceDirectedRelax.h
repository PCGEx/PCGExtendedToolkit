// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "PCGExForceDirectedRelax.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName="Force Directed", PCGExNodeLibraryDoc="clusters/transform/cluster-relax/force-directed"))
class UPCGExForceDirectedRelax : public UPCGExRelaxClusterOperation
{
	GENERATED_BODY()

public:
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;
	virtual void Step1(const PCGExClusters::FNode& Node) override;

	/** Strength of the attraction pulling connected Vtx together. Grows with edge length, like a spring. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double SpringConstant = 0.1;

	/** Strength of the repulsion pushing every Vtx pair apart. Falls off with squared distance, and is
	 * capped at the pair's own distance so near-coincident Vtx cannot be thrown apart. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double ElectrostaticConstant = 1000;

protected:
	void CalculateAttractiveForce(FVector& Force, const FVector& A, const FVector& B) const;
	void CalculateRepulsiveForce(FVector& Force, const FVector& A, const FVector& B) const;
};
