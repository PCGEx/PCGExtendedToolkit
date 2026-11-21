// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExSamplingUnionData.h"

#include "Containers/PCGExIndexLookup.h"
#include "Misc/ScopeRWLock.h"


namespace PCGExSampling
{
	int32 FSampingUnionData::ComputeWeights(
		const TArray<const UPCGBasePointData*>& Sources,
		const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
		const PCGExData::FPoint& Target,
		const PCGExMath::IDistances* InDistances,
		TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		const int32 NumElements = Elements.Num();
		OutWeightedPoints.Reset(NumElements);

		double TotalWeight = 0;
		int32 Index = 0;

		if (WeightRange == -2)
		{
			// Don't remap weight
			for (const PCGExData::FElement& Element : Elements)
			{
				const int32 IOIdx = IdxLookup->Get(Element.IO);
				if (IOIdx == -1)
				{
					continue;
				}

				const double Weight = Weights[Element];
				OutWeightedPoints.Emplace(Element.Index, Weight, IOIdx);
				TotalWeight += Weight;
				Index++;
			}
		}
		else if (WeightRange == -1)
		{
			double InternalRange = 0;
			for (const TPair<PCGExData::FElement, double>& W : Weights)
			{
				InternalRange = FMath::Max(InternalRange, W.Value);
			}

			// Remap weight to available max
			for (const PCGExData::FElement& Element : Elements)
			{
				const int32 IOIdx = IdxLookup->Get(Element.IO);
				if (IOIdx == -1)
				{
					continue;
				}

				const double Weight = 1 - (Weights[Element] / InternalRange);
				OutWeightedPoints.Emplace(Element.Index, Weight, IOIdx);
				TotalWeight += Weight;
				Index++;
			}
		}
		else
		{
			// Remap weight to specified max
			for (const PCGExData::FElement& Element : Elements)
			{
				const int32 IOIdx = IdxLookup->Get(Element.IO);
				if (IOIdx == -1)
				{
					continue;
				}

				const double Weight = 1 - (Weights[Element] / WeightRange);
				OutWeightedPoints.Emplace(Element.Index, Weight, IOIdx);
				TotalWeight += Weight;
				Index++;
			}
		}

		if (Index == 0)
		{
			return 0;
		}
		if (TotalWeight == 0)
		{
			const double FixedWeight = 1 / static_cast<double>(Index);
			for (PCGExData::FWeightedPoint& P : OutWeightedPoints)
			{
				P.Weight = FixedWeight;
			}
			return Index;
		}

		return Index;
	}

	void FSampingUnionData::Reserve(const int32 InSetReserve, const int32 InElementReserve)
	{
		IUnionData::Reserve(InSetReserve, InElementReserve);
		if (InElementReserve > 8)
		{
			Weights.Reserve(InElementReserve);
		}
	}

	void FSampingUnionData::Reset()
	{
		IUnionData::Reset();
		Weights.Reset();
	}
}
