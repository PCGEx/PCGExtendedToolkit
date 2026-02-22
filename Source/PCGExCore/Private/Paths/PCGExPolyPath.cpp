// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExPolyPath.h"

#include "Data/PCGSplineData.h"
#include "Data/PCGSplineStruct.h"
#include "Curve/CurveUtil.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExBestFitPlane.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Paths/PCGExPathsCommon.h"

#if PCGEX_ENGINE_VERSION > 506
#include "Data/PCGPolygon2DData.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExPaths"
#define PCGEX_NAMESPACE PCGExPaths

namespace PCGExPaths
{
	FPolyPath::FPolyPath(const TSharedPtr<PCGExData::FPointIO>& InPointIO, const FPCGExGeo2DProjectionDetails& InProjection, const double Expansion, const double ExpansionZ, const EPCGExWindingMutation WindingMutation)
		: FPath(InPointIO->GetIn()->GetConstTransformValueRange(), Helpers::GetClosedLoop(InPointIO), Expansion)
	{
		Projection = InProjection;
		if (Projection.Method != EPCGExProjectionMethod::Normal) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); }
		else { if (!Projection.Init(InPointIO)) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); } }

		InitFromTransforms(WindingMutation);
	}

	FPolyPath::FPolyPath(const TSharedPtr<PCGExData::FFacade>& InPathFacade, const FPCGExGeo2DProjectionDetails& InProjection, const double Expansion, const double ExpansionZ, const EPCGExWindingMutation WindingMutation)
		: FPath(InPathFacade->GetIn()->GetConstTransformValueRange(), Helpers::GetClosedLoop(InPathFacade->Source), Expansion)
	{
		Projection = InProjection;
		if (Projection.Method != EPCGExProjectionMethod::Normal) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); }
		else { if (!Projection.Init(InPathFacade)) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); } }

		InitFromTransforms(WindingMutation);
	}

	FPolyPath::FPolyPath(const UPCGSplineData* SplineData, const double Fidelity, const FPCGExGeo2DProjectionDetails& InProjection, const double Expansion, const double ExpansionZ, const EPCGExWindingMutation WindingMutation)
		: FPath(SplineData->IsClosed())
	{
		Spline = &SplineData->SplineStruct; // MakeSplineCopy(SplineData->SplineStruct);

		TArray<FVector> TempPolyline;
		Spline->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, FMath::Square(Fidelity), TempPolyline);

		LocalTransforms.Reserve(TempPolyline.Num());
		for (int i = 0; i < TempPolyline.Num(); i++) { LocalTransforms.Emplace(TempPolyline[i]); }

		Positions = TConstPCGValueRange<FTransform>(MakeConstStridedView(LocalTransforms));

		Projection = InProjection;
		if (Projection.Method != EPCGExProjectionMethod::Normal) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); }
		else { if (!Projection.Init(SplineData)) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); } }


		InitFromTransforms(WindingMutation);

		// Need to force-build path post initializations
		this->BuildPath(Expansion);
	}

#if PCGEX_ENGINE_VERSION > 506
	FPolyPath::FPolyPath(const UPCGPolygon2DData* PolygonData, const FPCGExGeo2DProjectionDetails& InProjection, const double Expansion, const double ExpansionZ, const EPCGExWindingMutation WindingMutation)
	{
		const UE::Geometry::TPolygon2<double>& Polygon = PolygonData->GetPolygon().GetOuter();

		const int32 NumVertices = Polygon.VertexCount();
		LocalTransforms.Reserve(NumVertices);

		for (int i = 0; i < NumVertices; i++)
		{
			const UE::Math::TVector2<double>& V2 = Polygon.GetVertices()[i];
			LocalTransforms.Emplace(FVector(V2.X, V2.Y, 0));
		}

		Positions = TConstPCGValueRange<FTransform>(MakeConstStridedView(LocalTransforms));

		Projection = InProjection;
		if (Projection.Method != EPCGExProjectionMethod::Normal) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); }
		else { if (!Projection.Init(PolygonData)) { Projection.Init(PCGExMath::FBestFitPlane(Positions)); } }

		InitFromTransforms(WindingMutation);

		// Need to force-build path post initializations
		this->BuildPath(Expansion);
	}
#endif

	void FPolyPath::InitFromTransforms(const EPCGExWindingMutation WindingMutation)
	{
		NumPoints = Positions.Num();
		LastIndex = NumPoints - 1;

		BuildProjection();

		// Enforce winding order by checking the signed area of the 2D projected polygon.
		// Negative signed area = clockwise winding; if it doesn't match the desired winding,
		// reverse both the projected points and the source transforms.
		if (WindingMutation != EPCGExWindingMutation::Unchanged)
		{
			const EPCGExWinding Wants = WindingMutation == EPCGExWindingMutation::Clockwise ? EPCGExWinding::Clockwise : EPCGExWinding::CounterClockwise;
			if (!PCGExMath::IsWinded(Wants, UE::Geometry::CurveUtil::SignedArea2<double, FVector2D>(ProjectedPoints) < 0))
			{
				Algo::Reverse(ProjectedPoints);
				if (!LocalTransforms.IsEmpty()) { Algo::Reverse(LocalTransforms); }
			}
		}

		if (!Spline)
		{
			LocalSpline = Helpers::MakeSplineFromPoints(Positions, EPCGExSplinePointTypeRedux::Linear, bClosedLoop, false);
			Spline = LocalSpline.Get();
		}
	}

	FTransform FPolyPath::GetClosestTransform(const FVector& WorldPosition, int32& OutEdgeIndex, float& OutLerp, const bool bUseScale) const
	{
		const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(WorldPosition);
		OutEdgeIndex = FMath::FloorToInt32(ClosestKey);
		OutLerp = ClosestKey - OutEdgeIndex;
		return Spline->GetTransformAtSplineInputKey(ClosestKey, ESplineCoordinateSpace::World, bUseScale);
	}

	FTransform FPolyPath::GetClosestTransform(const FVector& WorldPosition, float& OutAlpha, const bool bUseScale) const
	{
		const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(WorldPosition);
		OutAlpha = ClosestKey / Spline->GetNumberOfSplineSegments();
		return Spline->GetTransformAtSplineInputKey(ClosestKey, ESplineCoordinateSpace::World, bUseScale);
	}

	FTransform FPolyPath::GetClosestTransform(const FVector& WorldPosition, bool& bIsInside, const bool bUseScale) const
	{
		bIsInside = IsInsideProjection(WorldPosition);
		return Spline->GetTransformAtSplineInputKey(Spline->FindInputKeyClosestToWorldLocation(WorldPosition), ESplineCoordinateSpace::World, bUseScale);
	}

	FTransform FPolyPath::GetClosestTransform(const FVector& WorldPosition, const bool bUseScale) const
	{
		return Spline->GetTransformAtSplineInputKey(Spline->FindInputKeyClosestToWorldLocation(WorldPosition), ESplineCoordinateSpace::World, bUseScale);
	}

	bool FPolyPath::GetClosestPosition(const FVector& WorldPosition, FVector& OutPosition) const
	{
		check(EdgeOctree)
		//EdgeOctree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), OutPosition);
		return false;
	}

	bool FPolyPath::GetClosestPosition(const FVector& WorldPosition, FVector& OutPosition, bool& bIsInside) const
	{
		check(EdgeOctree)
		bIsInside = IsInsideProjection(WorldPosition);
		return false;
	}

	int32 FPolyPath::GetClosestEdge(const FVector& WorldPosition, float& OutLerp) const
	{
		const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(WorldPosition);
		const int32 OutEdgeIndex = FMath::FloorToInt32(ClosestKey);
		OutLerp = ClosestKey - OutEdgeIndex;
		return FMath::Min(OutEdgeIndex, this->LastEdge);
	}

	int32 FPolyPath::GetClosestEdge(const double InTime, float& OutLerp) const
	{
		const double ScaledTime = InTime * this->NumEdges;
		const int32 OutEdgeIndex = FMath::FloorToInt32(ScaledTime);
		OutLerp = static_cast<float>(ScaledTime - OutEdgeIndex);
		return FMath::Min(OutEdgeIndex, this->LastEdge);
	}

	FTransform FPolyPath::GetTransformAtInputKey(const float InKey, const bool bUseScale) const
	{
		return Spline->GetTransformAtSplineInputKey(InKey, ESplineCoordinateSpace::World, bUseScale);
	}

	void FPolyPath::GetEdgeElements(const int32 EdgeIndex, PCGExData::FElement& OutEdge, PCGExData::FElement& OutEdgeStart, PCGExData::FElement& OutEdgeEnd) const
	{
		OutEdge = PCGExData::FElement(EdgeIndex, Idx);
		OutEdgeStart = PCGExData::FElement(Edges[EdgeIndex].Start, Idx);
		OutEdgeEnd = PCGExData::FElement(Edges[EdgeIndex].End, Idx);
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
