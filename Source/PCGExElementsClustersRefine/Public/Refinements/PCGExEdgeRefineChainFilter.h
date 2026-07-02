// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExChainGating.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineChainFilter.generated.h"

UENUM()
enum class EPCGExChainFilterOverride : uint8
{
	ForceKeep   = 0 UMETA(DisplayName = "Force Keep", ToolTip="Chains hit by the edge filters are kept, regardless of the gating criteria."),
	ForceRemove = 1 UMETA(DisplayName = "Force Remove", ToolTip="Chains hit by the edge filters are removed, regardless of the gating criteria."),
};

/**
 *
 */
class FPCGExEdgeRefineChainFilter : public FPCGExEdgeRefineOperation
{
public:
	virtual void Process() override;

	FPCGExChainGatingDetails Gating;
	bool bInvert = false;
	EPCGExChainFilterOverride FilterOverride = EPCGExChainFilterOverride::ForceKeep;
	bool bRequireAllEdgesPass = false;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : Chain Filter", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-chain-filter"))
class UPCGExEdgeRefineChainFilter : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool SupportFilters() const override
	{
		return true;
	}

	virtual bool GetDefaultEdgeValidity() const override
	{
		// Chains partition all edges and are set explicitly; edges not covered by any chain stay kept.
		return true;
	}

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	/** Chain selection criteria. Leave all criteria disabled to make the node inert. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExChainGatingDetails Gating;

	/** If disabled, chains that match the criteria are removed. If enabled, matching chains are kept and every other chain is removed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** Behavior for chains hit by the optional edge filters. Overrides the gating verdict. Only applies when the edge filter pin is connected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExChainFilterOverride FilterOverride = EPCGExChainFilterOverride::ForceKeep;

	/** If enabled, every edge of a chain must pass the filters to trigger the override; otherwise a single passing edge is enough. Only applies when the edge filter pin is connected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bRequireAllEdgesPass = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineChainFilter, {
	                              Operation->Gating = Gating;
	                              Operation->bInvert = bInvert;
	                              Operation->FilterOverride = FilterOverride;
	                              Operation->bRequireAllEdgesPass = bRequireAllEdgesPass;
	                              })
};
