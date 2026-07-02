// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineChainFilter.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExChain.h"
#include "Clusters/Artifacts/PCGExCachedChain.h"
#include "Core/PCGExMTCommon.h"

#pragma region FPCGExEdgeRefineChainFilter

void FPCGExEdgeRefineChainFilter::Process()
{
	// Build (or reuse cached) topological chains. No breakpoints: we want the base cluster topology.
	TArray<TSharedPtr<PCGExClusters::FNodeChain>> Chains;
	if (!PCGExClusters::ChainHelpers::GetOrBuildChains(Cluster.ToSharedRef(), Chains, nullptr, false))
	{
		return;
	}

	const bool bUseFilters = bHasEdgeFilters && EdgeFilterCache;

	PCGExMT::ParallelOrSequential(
		Chains.Num(),
		[&](const int32 Index)
		{
			const TSharedPtr<PCGExClusters::FNodeChain>& Chain = Chains[Index];
			if (!Chain)
			{
				return;
			}

			// bInvert only flips an actual gating verdict; with no criteria enabled the node stays inert
			// (the optional filter override below can still act).
			const bool bMatch = Gating.Test(*Chain, *Cluster);
			bool bRemove = Gating.IsEnabled() ? (bInvert ? !bMatch : bMatch) : false;

			if (bUseFilters)
			{
				bool bAnyPass = false;
				bool bAllPass = true;

				auto AccumulateFilter = [&](const int32 EdgeIndex)
				{
					const bool bPass = static_cast<bool>((*EdgeFilterCache)[EdgeIndex]);
					bAnyPass |= bPass;
					bAllPass &= bPass;
				};

				for (const PCGExGraphs::FLink& Lk : Chain->Links)
				{
					AccumulateFilter(Lk.Edge);
				}
				if (Chain->bIsClosedLoop)
				{
					AccumulateFilter(Chain->Seed.Edge);
				}

				if (bRequireAllEdgesPass ? bAllPass : bAnyPass)
				{
					bRemove = (FilterOverride == EPCGExChainFilterOverride::ForceRemove);
				}
			}

			// Chains own disjoint edges, so these writes never race across parallel iterations.
			const int8 Validity = bRemove ? 0 : 1;
			for (const PCGExGraphs::FLink& Lk : Chain->Links)
			{
				Cluster->GetEdge(Lk.Edge)->bValid = Validity;
			}
			if (Chain->bIsClosedLoop)
			{
				Cluster->GetEdge(Chain->Seed.Edge)->bValid = Validity;
			}
		},
		32, EParallelForFlags::Unbalanced);
}

#pragma endregion

#pragma region UPCGExEdgeRefineChainFilter

void UPCGExEdgeRefineChainFilter::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExEdgeRefineChainFilter* TypedOther = Cast<UPCGExEdgeRefineChainFilter>(Other))
	{
		Gating = TypedOther->Gating;
		bInvert = TypedOther->bInvert;
		FilterOverride = TypedOther->FilterOverride;
		bRequireAllEdgesPass = TypedOther->bRequireAllEdgesPass;
	}
}

#pragma endregion
