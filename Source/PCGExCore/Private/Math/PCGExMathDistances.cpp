// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExMathDistances.h"
#include "Math/PCGExMathDistancesImpl.h"

namespace PCGExMath
{
	// Single instance -- lives only in this TU, accessed via GetDistances()/GetNoneDistances()
	static FDistancesStatic GDistancesStatic;

#define PCGEX_FOREACH_DISTANCE_PAIR(MACRO) \
    MACRO(EPCGExDistance::Center, EPCGExDistance::Center) \
    MACRO(EPCGExDistance::Center, EPCGExDistance::SphereBounds) \
    MACRO(EPCGExDistance::Center, EPCGExDistance::BoxBounds) \
    MACRO(EPCGExDistance::Center, EPCGExDistance::None) \
    MACRO(EPCGExDistance::SphereBounds, EPCGExDistance::Center) \
    MACRO(EPCGExDistance::SphereBounds, EPCGExDistance::SphereBounds) \
    MACRO(EPCGExDistance::SphereBounds, EPCGExDistance::BoxBounds) \
    MACRO(EPCGExDistance::SphereBounds, EPCGExDistance::None) \
    MACRO(EPCGExDistance::BoxBounds, EPCGExDistance::Center) \
    MACRO(EPCGExDistance::BoxBounds, EPCGExDistance::SphereBounds) \
    MACRO(EPCGExDistance::BoxBounds, EPCGExDistance::BoxBounds) \
    MACRO(EPCGExDistance::BoxBounds, EPCGExDistance::None) \
    MACRO(EPCGExDistance::None, EPCGExDistance::Center) \
    MACRO(EPCGExDistance::None, EPCGExDistance::SphereBounds) \
    MACRO(EPCGExDistance::None, EPCGExDistance::BoxBounds) \
    MACRO(EPCGExDistance::None, EPCGExDistance::None)

	const IDistances* GetDistances(const EPCGExDistance Source, const EPCGExDistance Target, const bool bOverlapIsZero, const EPCGExDistanceType Type)
	{
		const TTuple<EPCGExDistance, EPCGExDistance, EPCGExDistanceType, bool> Key(Source, Target, Type, bOverlapIsZero);
		return GDistancesStatic.Cache[Key].Get();
	}


	const IDistances* GetNoneDistances()
	{
		return GetDistances(EPCGExDistance::None, EPCGExDistance::None);
	}

	FDistancesStatic::FDistancesStatic()
	{
		// Pre-instantiate all (Source × Target × Type × Overlap) distance function combinations
		// into a static lookup cache. This avoids runtime allocation and ensures thread safety
		// since GetDistances() only reads from this cache during parallel processing.

		TArray<bool> Overlap = {true, false};
		TArray<EPCGExDistance> Dist = {EPCGExDistance::BoxBounds, EPCGExDistance::SphereBounds, EPCGExDistance::Center, EPCGExDistance::None};
		TArray<EPCGExDistanceType> Types = {EPCGExDistanceType::Euclidian, EPCGExDistanceType::Manhattan, EPCGExDistanceType::Chebyshev};

		for (bool bOverlapIsZero : Overlap)
		{
			for (EPCGExDistance Source : Dist)
			{
				for (EPCGExDistance Target : Dist)
				{
					for (EPCGExDistanceType Type : Types)
					{
						TSharedPtr<IDistances> NewDistances;
						const TTuple<EPCGExDistance, EPCGExDistance, EPCGExDistanceType, bool> Key(Source, Target, Type, bOverlapIsZero);
						switch (Type)
						{
						default:
						case EPCGExDistanceType::Euclidian:
							if (Source == EPCGExDistance::None || Target == EPCGExDistance::None)
							{
								NewDistances = MakeShared<TEuclideanDistances<EPCGExDistance::None, EPCGExDistance::None>>(bOverlapIsZero);
							}
#define PCGEX_DIST_TPL(_FROM, _TO) \
else if (Source == _FROM && Target == _TO) { NewDistances = MakeShared<TEuclideanDistances<_FROM, _TO>>(bOverlapIsZero); }
							PCGEX_FOREACH_DISTANCE_PAIR(PCGEX_DIST_TPL)
#undef PCGEX_DIST_TPL
							break;

						case EPCGExDistanceType::Manhattan:
							if (Source == EPCGExDistance::None || Target == EPCGExDistance::None)
							{
								NewDistances = MakeShared<TManhattanDistances<EPCGExDistance::None, EPCGExDistance::None>>(bOverlapIsZero);
							}
#define PCGEX_DIST_TPL(_FROM, _TO) \
else if (Source == _FROM && Target == _TO) { NewDistances = MakeShared<TManhattanDistances<_FROM, _TO>>(bOverlapIsZero); }
							PCGEX_FOREACH_DISTANCE_PAIR(PCGEX_DIST_TPL)
#undef PCGEX_DIST_TPL
							break;

						case EPCGExDistanceType::Chebyshev:
							if (Source == EPCGExDistance::None || Target == EPCGExDistance::None)
							{
								NewDistances = MakeShared<TChebyshevDistances<EPCGExDistance::None, EPCGExDistance::None>>(bOverlapIsZero);
							}
#define PCGEX_DIST_TPL(_FROM, _TO) \
else if (Source == _FROM && Target == _TO) { NewDistances = MakeShared<TChebyshevDistances<_FROM, _TO>>(bOverlapIsZero); }
							PCGEX_FOREACH_DISTANCE_PAIR(PCGEX_DIST_TPL)
#undef PCGEX_DIST_TPL
							break;
						}

						Cache.Add(Key, NewDistances);
					}
				}
			}
		}
	}

#undef PCGEX_FOREACH_DISTANCE_PAIR
}
