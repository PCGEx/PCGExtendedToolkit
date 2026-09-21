// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSamplingCommon.h"

namespace PCGExData
{
	class FFacade;

	template <typename T>
	class TBuffer;
}

namespace PCGExSampling
{
	class FSampleAccumulator;

	/** Copied once per processor from the node's settings so the write path never touches the node's types. */
	struct PCGEXBLENDING_API FCommonOutputConfig
	{
		PCGEX_FOREACH_FIELD_SAMPLING_COMMON(PCGEX_OUTPUT_CONFIG_DECL)

		double DistanceScale = 1;
		double SignedDistanceScale = 1;
		bool bOutputNormalizedDistance = false;
		bool bOutputOneMinusDistance = false;
		bool bAbsoluteComponentWiseDistance = true;
		EPCGExAngleRange AngleRange = EPCGExAngleRange::PIRadians;

		// Failure policy, set per node
		bool bScaleFailDistance = true;      // DistanceScale / SignedDistanceScale applied to the fail distance
		bool bWriteAngleOnFailure = false;
		bool bNormalizeFailedDistance = true; // failed points run through the normalized-distance pass too
	};

	/** The writers every target sampler shares, plus their success / failure / normalization passes. */
	class PCGEXBLENDING_API FCommonOutputs
	{
	public:
		PCGEX_FOREACH_FIELD_SAMPLING_COMMON(PCGEX_OUTPUT_DECL)
		FCommonOutputConfig Config;

		FCommonOutputs();
		~FCommonOutputs();

		void Init(const TSharedRef<PCGExData::FFacade>& OutputFacade, const FCommonOutputConfig& InConfig);

		/** bApplySign false writes the unsigned distance to SignedDistance (path samplers' closed-loop gate). */
		void WriteSuccess(const int32 Index, const FSampleAccumulator& Acc, const bool bApplySign = true) const;
		void WriteFailure(const int32 Index, const FTransform& Passthrough, const double FailDistance) const;

		/** No-op unless normalized distance is on; a max of 0 leaves the values untouched. */
		void NormalizeDistances(const TArray<int8>& SamplingMask, const double MaxDistance) const;
	};
}
