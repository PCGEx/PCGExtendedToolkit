// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineTrajanDFS.generated.h"

/**
 *
 */
class FPCGExEdgeRefineTrajanDFS : public FPCGExEdgeRefineOperation
{
public:
	virtual void Process() override;

	bool bInvert = false;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : DFS (Tarjan)", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-dfs-tarjan"))
class UPCGExEdgeRefineTrajanDFS : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool GetDefaultEdgeValidity() const override
	{
		return !bInvert;
	}

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	/** Invert the refinement result (keep edges that would be removed and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineTrajanDFS, { Operation->bInvert = bInvert; })
};
