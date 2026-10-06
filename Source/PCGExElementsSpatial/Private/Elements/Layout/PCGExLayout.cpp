// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Layout/PCGExLayout.h"

namespace PCGExLayout
{
	bool FSpace::CanFit(const FVector& InTestSize) const
	{
		return (InTestSize.X <= Size.X && InTestSize.Y <= Size.Y && InTestSize.Z <= Size.Z);
	}

	void FSpace::Expand(FBox& InBox, const FVector& Expansion) const
	{
		InBox = InBox.ExpandBy(Expansion);
		for (int C = 0; C < 3; C++)
		{
			if (InBox.Min[C] < Box.Min[C])
			{
				InBox.Min[C] = Box.Min[C];
			}
			if (InBox.Max[C] > Box.Max[C])
			{
				InBox.Max[C] = Box.Max[C];
			}
		}
	}

	FVector FSpace::Inflate(FBox& InBox, const FVector& Thresholds) const
	{
		FVector AmplitudeMin = Box.Min - InBox.Min;
		FVector AmplitudeMax = Box.Max - InBox.Max;

		for (int C = 0; C < 3; C++)
		{
			if (FMath::Abs(AmplitudeMin[C]) < Thresholds[C])
			{
				InBox.Min[C] = Box.Min[C];
			}
			else
			{
				AmplitudeMin[C] = 0;
			}

			if (FMath::Abs(AmplitudeMax[C]) < Thresholds[C])
			{
				InBox.Max[C] = Box.Max[C];
			}
			else
			{
				AmplitudeMax[C] = 0;
			}
		}

		return AmplitudeMin + AmplitudeMax;
	}
}
