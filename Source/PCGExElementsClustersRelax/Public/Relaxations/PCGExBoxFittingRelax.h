// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFittingRelaxBase.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "Types/PCGExTypes.h"

#include "PCGExBoxFittingRelax.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Box Fitting", PCGExNodeLibraryDoc="clusters/transform/cluster-relax/box-fitting"))
class UPCGExBoxFittingRelax : public UPCGExFittingRelaxBase
{
	GENERATED_BODY()

public:
	UPCGExBoxFittingRelax(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
	}

	/** A padding value added to the box bounds to attempt to reduce overlap or add more spacing between boxes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Padding = 10;

	virtual bool PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster) override;
	virtual EPCGExClusterElement PrepareNextStep(const int32 InStep) override;
	virtual void Step2(const PCGExClusters::FNode& Node) override;

protected:
	TArray<FBox> BoxBuffer;
};
