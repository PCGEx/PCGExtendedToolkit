// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMTCommon.h"
#include "Math/InterpCurvePoint.h" // EInterpCurveMode
#include "Math/PCGExMath.h"

struct FPCGExPathIntersectionDetails;
struct FPCGExPathEdgeIntersectionDetails;
template <typename ValueType, typename ViewType>
class TPCGValueRange;

template <typename ElementType>
using TConstPCGValueRange = TPCGValueRange<const ElementType, TConstStridedView<ElementType>>;

enum class EPCGExSplinePointTypeRedux : uint8;
struct FPCGSplineStruct;
class UPCGBasePointData;

namespace PCGExPaths
{
	class FPathEdgeLength;
	struct FPathEdge;
	class FPath;
}

namespace PCGExData
{
	class FFacade;
	class FPointIO;
}

class UPCGData;

namespace PCGExPaths
{
	namespace Helpers
	{
		PCGEXCORE_API void SetClosedLoop(UPCGData* InData, const bool bIsClosedLoop);

		PCGEXCORE_API void SetClosedLoop(const TSharedPtr<PCGExData::FPointIO>& InData, const bool bIsClosedLoop);

		PCGEXCORE_API bool GetClosedLoop(const UPCGData* InData);

		PCGEXCORE_API bool GetClosedLoop(const TSharedPtr<PCGExData::FPointIO>& InData);

		/**
		 * Hole state, carried only by the HoleIdentifier @Data attribute.
		 *
		 * KNOWN GAP: nothing writes it onto UPCGSplineData (Write Path Properties
		 * and Watabou both emit point data), so spline holes can only be
		 * hand-authored. Nodes accepting splines should say so.
		 */
		PCGEXCORE_API void SetIsHole(UPCGData* InData, const bool bIsHole);

		PCGEXCORE_API void SetIsHole(const TSharedPtr<PCGExData::FPointIO>& InData, const bool bIsHole);

		PCGEXCORE_API bool GetIsHole(const UPCGData* InData);

		PCGEXCORE_API bool GetIsHole(const TSharedPtr<PCGExData::FPointIO>& InData);

		PCGEXCORE_API void FetchPrevNext(const TSharedPtr<PCGExData::FFacade>& InFacade, const TArray<PCGExMT::FScope>& Loops);

		PCGEXCORE_API TSharedPtr<FPath> MakePath(const UPCGBasePointData* InPointData, const double Expansion);

		PCGEXCORE_API double GetPathLength(const TSharedPtr<FPath>& InPath);

		PCGEXCORE_API FTransform GetClosestTransform(const FPCGSplineStruct& InSpline, const FVector& InLocation, const bool bUseScale = true);

		PCGEXCORE_API FTransform GetClosestTransform(const TSharedPtr<const FPCGSplineStruct>& InSpline, const FVector& InLocation, const bool bUseScale = true);

		/** Canonical encoding of a spline control point's interp mode to int32, shared by nodes that emit a
		 *  "PointType" attribute (Spline to Path, Get Path Data) so the values stay consistent across them. */
		PCGEXCORE_API int32 SplinePointTypeToInt(const EInterpCurveMode Mode);

		PCGEXCORE_API TSharedPtr<FPCGSplineStruct> MakeSplineFromPoints(const TConstPCGValueRange<FTransform>& InTransforms, const EPCGExSplinePointTypeRedux InPointType, const bool bClosedLoop, bool bSmoothLinear);

		PCGEXCORE_API TSharedPtr<FPCGSplineStruct> MakeSplineCopy(const FPCGSplineStruct& Original);

		PCGEXCORE_API PCGExMath::FClosestPosition FindClosestIntersection(const TArray<TSharedPtr<FPath>>& Paths, const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& InSegment, int32& OutPathIndex);

		PCGEXCORE_API PCGExMath::FClosestPosition FindClosestIntersection(const TArray<TSharedPtr<FPath>>& Paths, const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& InSegment, int32& OutPathIndex, PCGExMath::FClosestPosition& OutClosestPosition);
	}

	struct PCGEXCORE_API FCrossing
	{
		FCrossing() = default;
		FCrossing(const uint64 InHash, const FVector& InLocation, const double InAlpha, const bool InIsPoint, const FVector& InDir);


		uint64 Hash;      // Point Index | IO Index
		FVector Location; // Position in between edges
		double Alpha;     // Position along the edge
		bool bIsPoint;    // Is crossing a point
		FVector Dir;      // Direction of the crossing edge
	};

	struct PCGEXCORE_API FPathEdgeCrossings
	{
		int32 Index = -1;

		TArray<FCrossing> Crossings;

		explicit FPathEdgeCrossings(const int32 InIndex)
			: Index(InIndex)
		{
		}

		FORCEINLINE bool IsEmpty() const
		{
			return Crossings.IsEmpty();
		}

		bool FindSplit(
			const TSharedPtr<FPath>& Path, const FPathEdge& Edge, const TSharedPtr<FPathEdgeLength>& PathLength,
			const TSharedPtr<FPath>& OtherPath, const FPathEdge& OtherEdge,
			const FPCGExPathEdgeIntersectionDetails& InIntersectionDetails);

		bool RemoveCrossing(const int32 EdgeStartIndex, const int32 IOIndex);
		bool RemoveCrossing(const TSharedPtr<FPath>& Path, const int32 EdgeStartIndex);
		bool RemoveCrossing(const TSharedPtr<FPath>& Path, const FPathEdge& Edge);

		void SortByAlpha();
		void SortByHash();
	};

	struct PCGEXCORE_API FInclusionInfos
	{
		FInclusionInfos() = default;
		int32 Depth = 0;       // Inclusion "depth"
		int32 Children = 0;    // Number of paths included in this one
		int32 ParentIdx = -1;  // Idx of the nearest enclosing path (direct parent); -1 when not enclosed (outer)
		bool bOdd = false;
	};

	class PCGEXCORE_API FPathInclusionHelper : public TSharedFromThis<FPathInclusionHelper>
	{
	protected:
		TSet<int32> PathsSet;
		TArray<TSharedPtr<FPath>> Paths;
		TMap<int32, FInclusionInfos> IdxMap;

		// Recorded (ContainerIdx, ContainedIdx) pairs, used to resolve each path's nearest
		// enclosing parent once all depths are final (see ResolveParents).
		TArray<TPair<int32, int32>> Containments;

		// Resolves FInclusionInfos::ParentIdx for every path (nearest parent = deepest container).
		void ResolveParents();

	public:
		FPathInclusionHelper() = default;

		void AddPath(const TSharedPtr<FPath>& InPath, const double Tolerance = 0);
		void AddPaths(const TArrayView<TSharedPtr<FPath>> InPaths, const double Tolerance = 0);
		bool Find(const int32 Idx, FInclusionInfos& OutInfos) const;
	};
}
