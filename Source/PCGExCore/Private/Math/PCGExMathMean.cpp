// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExMathMean.h"

namespace PCGExMath
{
	double GetMode(const TArray<double>& Values, const bool bHighest, const uint32 Tolerance)
	{
		TMap<double, int32> Map;
		int32 LastCount = 0;
		double Mode = 0;

		for (const double Value : Values)
		{
			const double AdjustedValue = FMath::RoundToZero(Value / Tolerance) * Tolerance;
			const int32* Count = Map.Find(AdjustedValue);
			const int32 UpdatedCount = Count ? *Count + 1 : 1;
			Map.Add(AdjustedValue, UpdatedCount);

			if (LastCount < UpdatedCount)
			{
				LastCount = UpdatedCount;
				Mode = AdjustedValue;
			}
			else if (LastCount == UpdatedCount)
			{
				if (bHighest)
				{
					Mode = FMath::Max(Mode, AdjustedValue);
				}
				else
				{
					Mode = FMath::Min(Mode, AdjustedValue);
				}
			}
		}

		return Mode;
	}
}
