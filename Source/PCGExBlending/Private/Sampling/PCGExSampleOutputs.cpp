// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExSampleOutputs.h"

#include "Data/PCGExData.h"
#include "Sampling/PCGExSampleAccumulator.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Types/PCGExTypes.h"

namespace PCGExSampling
{
	FCommonOutputs::FCommonOutputs() = default;
	FCommonOutputs::~FCommonOutputs() = default;

#define PCGEX_OUTPUT_INIT_CONFIG(_NAME, _TYPE, _DEFAULT_VALUE) if(Config.bWrite##_NAME){ _NAME##Writer = OutputFacade->GetWritable<_TYPE>(Config._NAME##AttributeName, _DEFAULT_VALUE, true, PCGExData::EBufferInit::Inherit); }

	void FCommonOutputs::Init(const TSharedRef<PCGExData::FFacade>& OutputFacade, const FCommonOutputConfig& InConfig)
	{
		Config = InConfig;
		PCGEX_FOREACH_FIELD_SAMPLING_COMMON(PCGEX_OUTPUT_INIT_CONFIG)
	}

#undef PCGEX_OUTPUT_INIT_CONFIG

	void FCommonOutputs::WriteSuccess(const int32 Index, const FSampleAccumulator& Acc, const bool bApplySign) const
	{
		const double Sign = bApplySign ? FMath::Sign(Acc.WeightedSignAxis.Dot(Acc.LookAt)) : 1.0;

		PCGEX_OUTPUT_VALUE(Success, Index, true)
		PCGEX_OUTPUT_VALUE(Transform, Index, Acc.WeightedTransform)
		PCGEX_OUTPUT_VALUE(LookAtTransform, Index, Acc.LookAtTransform)
		PCGEX_OUTPUT_VALUE(Distance, Index, Config.bOutputNormalizedDistance ? Acc.Distance : Acc.Distance * Config.DistanceScale)
		PCGEX_OUTPUT_VALUE(SignedDistance, Index, Sign * Acc.Distance * Config.SignedDistanceScale)
		PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, Config.bAbsoluteComponentWiseDistance ? PCGExTypes::Abs(Acc.CWDistance) : Acc.CWDistance)
		PCGEX_OUTPUT_VALUE(Angle, Index, Helpers::GetAngle(Config.AngleRange, Acc.WeightedAngleAxis, Acc.LookAt))
		PCGEX_OUTPUT_VALUE(NumSamples, Index, Acc.Count)
	}

	void FCommonOutputs::WriteFailure(const int32 Index, const FTransform& Passthrough, const double FailDistance) const
	{
		PCGEX_OUTPUT_VALUE(Success, Index, false)
		PCGEX_OUTPUT_VALUE(Transform, Index, Passthrough)
		PCGEX_OUTPUT_VALUE(LookAtTransform, Index, Passthrough)
		PCGEX_OUTPUT_VALUE(Distance, Index, (Config.bOutputNormalizedDistance || !Config.bScaleFailDistance) ? FailDistance : FailDistance * Config.DistanceScale)
		PCGEX_OUTPUT_VALUE(SignedDistance, Index, Config.bScaleFailDistance ? FailDistance * Config.SignedDistanceScale : FailDistance)
		PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, FVector(FailDistance))
		if (Config.bWriteAngleOnFailure)
		{
			PCGEX_OUTPUT_VALUE(Angle, Index, 0)
		}
		PCGEX_OUTPUT_VALUE(NumSamples, Index, 0)
	}

	void FCommonOutputs::NormalizeDistances(const TArray<int8>& SamplingMask, const double MaxDistance) const
	{
		if (!Config.bOutputNormalizedDistance || !DistanceWriter)
		{
			return;
		}

		Helpers::NormalizeDistances(DistanceWriter, SamplingMask.Num(), Config.bNormalizeFailedDistance ? nullptr : &SamplingMask, MaxDistance, Config.bOutputOneMinusDistance, Config.DistanceScale);
	}
}
