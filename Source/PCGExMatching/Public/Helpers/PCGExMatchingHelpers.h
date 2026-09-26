// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"

namespace PCGExMatching
{
	class FDataMatcher;
}

struct FPCGPinProperties;
struct FPCGExMatchingDetails;
struct FPCGExTaggedData;

namespace PCGExMatching::Helpers
{
	PCGEXMATCHING_API
	void DeclareMatchingRulesInputs(const FPCGExMatchingDetails& InDetails, TArray<FPCGPinProperties>& PinProperties, const FName InPrimaryLabel = NAME_None);

	PCGEXMATCHING_API
	void DeclareMatchingRulesOutputs(const FPCGExMatchingDetails& InDetails, TArray<FPCGPinProperties>& PinProperties);

	/** Raw tagged-data variant. Sources must be the array the matcher was initialized with: pre-deduped
	 *  (the matcher refuses duplicate UPCGData*) and with tag sidecars kept alive (tags are weak).
	 *  Prefer the matcher-sources overload below. */
	PCGEXMATCHING_API
	int32 GetMatchingSourcePartitions(
		const TSharedPtr<FDataMatcher>& Matcher,
		const TArray<FPCGExTaggedData>& Sources,
		TArray<TArray<int32>>& OutPartitions,
		bool bExclusive,
		const TSet<int32>* OnceIndices = nullptr);

	/** Partitions the matcher's own registered sources -- no parallel array to build or keep in sync. */
	PCGEXMATCHING_API
	int32 GetMatchingSourcePartitions(
		const TSharedPtr<FDataMatcher>& Matcher,
		TArray<TArray<int32>>& OutPartitions,
		bool bExclusive,
		const TSet<int32>* OnceIndices = nullptr);
}
