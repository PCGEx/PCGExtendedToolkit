// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Helpers/PCGExMatchingHelpers.h"
#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExMatchingDetails.h"
#include "Helpers/PCGExDataMatcher.h"

namespace PCGExMatching::Helpers
{
	void DeclareMatchingRulesInputs(const FPCGExMatchingDetails& InDetails, TArray<FPCGPinProperties>& PinProperties, const FName InPrimaryLabel)
	{
		{
			FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(InPrimaryLabel.IsNone() ? Labels::SourceMatchRulesLabel : InPrimaryLabel, FPCGExDataTypeInfoMatchRule::AsId());
			PCGEX_PIN_TOOLTIP("Matching rules to determine which target data can be paired with each input. If target only accept a single data, individual target points will be evaluated.")
			Pin.PinStatus = InDetails.Mode != EPCGExMapMatchMode::Disabled ? EPCGPinStatus::Required : EPCGPinStatus::Advanced;
		}

		if (InDetails.Usage == EPCGExMatchingDetailsUsage::Cluster && InDetails.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Separated)
		{
			FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(Labels::SourceMatchRulesEdgesLabel, FPCGExDataTypeInfoMatchRule::AsId());
			PCGEX_PIN_TOOLTIP("Extra matching rules to determine which edges data can be paired with each input. If target only accept a single data, individual target points will be evaluated.")
			Pin.PinStatus = InDetails.Mode != EPCGExMapMatchMode::Disabled ? EPCGPinStatus::Required : EPCGPinStatus::Advanced;
		}
	}

	void DeclareMatchingRulesOutputs(const FPCGExMatchingDetails& InDetails, TArray<FPCGPinProperties>& PinProperties)
	{
		const bool bShowAsAdvanced = !InDetails.WantsUnmatchedSplit();
		if (InDetails.Usage == EPCGExMatchingDetailsUsage::Cluster)
		{
			{
				FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(Labels::OutputUnmatchedVtxLabel, EPCGDataType::Point);
				PCGEX_PIN_TOOLTIP("Vtx data that couldn't be matched to any target, and couldn't be processed. Note that Vtx data may exist in regular pin as well, this is to ensure unmatched edges are still bound to valid vtx.")
				Pin.PinStatus = bShowAsAdvanced ? EPCGPinStatus::Advanced : EPCGPinStatus::Normal;
			}
			{
				FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(Labels::OutputUnmatchedEdgesLabel, EPCGDataType::Point);
				PCGEX_PIN_TOOLTIP("Edge data that couldn't be matched to any target, and couldn't be processed.")
				Pin.PinStatus = bShowAsAdvanced ? EPCGPinStatus::Advanced : EPCGPinStatus::Normal;
			}
		}
		else
		{
			FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(Labels::OutputUnmatchedLabel, EPCGDataType::Point);
			PCGEX_PIN_TOOLTIP("Data that couldn't be matched to any target, and couldn't be processed.")
			Pin.PinStatus = bShowAsAdvanced ? EPCGPinStatus::Advanced : EPCGPinStatus::Normal;
		}
	}

	int32 GetMatchingSourcePartitions(const TSharedPtr<FDataMatcher>& Matcher, const TArray<TSharedPtr<PCGExData::FFacade>>& Facades, TArray<TArray<int32>>& OutPartitions, const bool bExclusive, const TSet<int32>* OnceIndices)
	{
		// NOTE : Uses Idx instead of IOIndex
		// This is primarily aimed to help clipper2 module to create sub-groups of paths
		// as well as MergeByTags to deprecate existing API and support non-exclusive groups.

		TArray<FPCGExTaggedData> Sources;
		Sources.Reserve(Facades.Num());
		for (const TSharedPtr<PCGExData::FFacade>& Facade : Facades)
		{
			Sources.Add(Facade->Source->GetTaggedData());
		}

		return GetMatchingSourcePartitions(Matcher, Sources, OutPartitions, bExclusive, OnceIndices);
	}

	int32 GetMatchingSourcePartitions(const TSharedPtr<FDataMatcher>& Matcher, TArray<TArray<int32>>& OutPartitions, const bool bExclusive, const TSet<int32>* OnceIndices)
	{
		return GetMatchingSourcePartitions(Matcher, Matcher->GetSources(), OutPartitions, bExclusive, OnceIndices);
	}

	int32 GetMatchingSourcePartitions(const TSharedPtr<FDataMatcher>& Matcher, const TArray<FPCGExTaggedData>& Sources, TArray<TArray<int32>>& OutPartitions, const bool bExclusive, const TSet<int32>* OnceIndices)
	{
		// Partition entries are positions in Sources, which must mirror the matcher's registration.
		const int32 NumSources = Matcher->GetNumSources();
		check(NumSources == Sources.Num())

		if (NumSources == 0)
		{
			return 0;
		}

		OutPartitions.Reserve(NumSources);

		if (bExclusive)
		{
			TBitArray<> DistributedIndices(false, NumSources);
			TSet<int32> DistributedIndicesSet;
			DistributedIndicesSet.Reserve(NumSources);

			for (int32 i = 0; i < NumSources; ++i)
			{
				if (DistributedIndices[i])
				{
					continue;
				}

				TArray<int32>& Partition = OutPartitions.Emplace_GetRef();
				Partition.Reserve(NumSources);

				FScope Scope = FScope(NumSources, true);
				Matcher->GetMatchingSourcesIndices(Sources[i], Scope, Partition, &DistributedIndicesSet);
				Partition.AddUnique(i);

				// Remove any indices that were already distributed (defensive check for recursive matching)
				Partition.RemoveAll([&DistributedIndices](const int32 Idx)
				{
					return DistributedIndices[Idx];
				});

				// Mark all partition members as distributed
				for (const int32 Idx : Partition)
				{
					DistributedIndices[Idx] = true;
					DistributedIndicesSet.Add(Idx);
				}
			}

			return OutPartitions.Num();
		}

		// Non-exclusive mode: indices can appear in multiple partitions,
		// except for OnceIndices which should only appear in one partition
		TBitArray<> OnceDistributed(false, NumSources);

		for (int32 i = 0; i < NumSources; ++i)
		{
			TArray<int32>& Partition = OutPartitions.Emplace_GetRef();
			Partition.Reserve(NumSources);

			FScope Scope = FScope(NumSources, true);
			Matcher->GetMatchingSourcesIndices(Sources[i], Scope, Partition);
			Partition.AddUnique(i);

			// Remove OnceIndices that have already been distributed to a previous partition
			if (OnceIndices && !OnceIndices->IsEmpty())
			{
				Partition.RemoveAll([&OnceIndices, &OnceDistributed](const int32 Idx)
				{
					return OnceIndices->Contains(Idx) && OnceDistributed[Idx];
				});

				// Mark OnceIndices in this partition as distributed
				for (const int32 Idx : Partition)
				{
					if (OnceIndices->Contains(Idx))
					{
						OnceDistributed[Idx] = true;
					}
				}
			}
		}

		return OutPartitions.Num();
	}
}
