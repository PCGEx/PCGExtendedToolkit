// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Core/PCGExUnionData.h"

namespace PCGExSampling
{
	class PCGEXBLENDING_API FSampingUnionData : public PCGExData::IUnionData
	{
	public:
		TMap<PCGExData::FElement, double> Weights;

		FSampingUnionData() = default;
		virtual ~FSampingUnionData() override = default;

		double WeightRange = -1;

		// Gather data into arrays and return the required iteration count
		virtual int32 ComputeWeights(
			const TArray<const UPCGBasePointData*>& Sources,
			const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
			const PCGExData::FPoint& Target,
			const PCGExMath::IDistances* InDistances,
			TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const override;

		FORCEINLINE void AddWeighted_Unsafe(const PCGExData::FElement& Element, const double InWeight)
		{
			Add_Unsafe(Element.Index, Element.IO);
			Weights.Add(Element, InWeight);
		}

		virtual void Reserve(const int32 InSetReserve, const int32 InElementReserve = 8) override;
		virtual void Reset() override;
	};
}
