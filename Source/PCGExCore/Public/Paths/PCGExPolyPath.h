// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPath.h"
#include "PCGExVersion.h"
#include "Math/PCGExWinding.h"

struct FPCGExContext;
class UPCGPolygon2DData;
struct FPCGSplineStruct;
class UPCGSplineData;

namespace PCGExData
{
	class FPointIO;
	struct FElement;
}

namespace PCGExPaths
{
	class PCGEXCORE_API FPolyPath : public FPath
	{
		TArray<FTransform> LocalTransforms;

		const FPCGSplineStruct* Spline = nullptr;

		// True only when constructed from an actual spline (UPCGSplineData). For point/polygon-built paths the
		// geometry is a polyline, so the closest-point/eval helpers operate directly on Edges/Positions and no
		// FPCGSplineStruct is synthesized.
		bool bIsSplineBacked = false;

	public:
		FPolyPath(
			const TSharedPtr<PCGExData::FPointIO>& InPointIO,
			const FPCGExGeo2DProjectionDetails& InProjection,
			const double Expansion = 0, const double ExpansionZ = -1,
			const EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged);

		FPolyPath(
			const TSharedPtr<PCGExData::FFacade>& InPathFacade,
			const FPCGExGeo2DProjectionDetails& InProjection,
			const double Expansion = 0, const double ExpansionZ = -1,
			const EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged);

		FPolyPath(
			const UPCGSplineData* SplineData, const double Fidelity,
			const FPCGExGeo2DProjectionDetails& InProjection,
			const double Expansion = 0, const double ExpansionZ = -1,
			const EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged);

		FPolyPath(
			const UPCGPolygon2DData* PolygonData, const FPCGExGeo2DProjectionDetails& InProjection,
			const double Expansion = 0, const double ExpansionZ = -1,
			const EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged);

	protected:
		void InitFromTransforms(const EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged);

		// Polyline closest-point + interpolation used when !bIsSplineBacked. Reproduces the linear spline they replace,
		// straight off the precomputed FPathEdge Dir/Length and the source point transforms.
		int32 ClosestEdgeLerp(const FVector& WorldPosition, float& OutLerp) const;
		FTransform LerpEdgeTransform(const int32 EdgeIndex, const float Lerp, const bool bUseScale) const;

	public:
		virtual FTransform GetClosestTransform(const FVector& WorldPosition, int32& OutEdgeIndex, float& OutLerp, const bool bUseScale = false) const override;
		virtual FTransform GetClosestTransform(const FVector& WorldPosition, float& OutAlpha, const bool bUseScale = false) const override;
		virtual FTransform GetClosestTransform(const FVector& WorldPosition, bool& bIsInside, const bool bUseScale = false) const override;
		virtual FTransform GetClosestTransform(const FVector& WorldPosition, const bool bUseScale = false) const override;

		virtual int32 GetClosestEdge(const FVector& WorldPosition, float& OutLerp) const override;
		virtual int32 GetClosestEdge(const double InTime, float& OutLerp) const override;

		FTransform GetTransformAtInputKey(const float InKey, const bool bUseScale = false) const;

		void GetEdgeElements(const int32 EdgeIndex, PCGExData::FElement& OutEdge, PCGExData::FElement& OutEdgeStart, PCGExData::FElement& OutEdgeEnd) const;
	};
}
