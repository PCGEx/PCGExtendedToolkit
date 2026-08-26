// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "PCGExLaplacianRelax.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Laplacian (Poisson)", PCGExNodeLibraryDoc="clusters/transform/cluster-relax/laplacian-poisson"))
class UPCGExLaplacianRelax : public UPCGExRelaxClusterOperation
{
	GENERATED_BODY()

public:
	virtual void Step1(const PCGExClusters::FNode& Node) override;
};
