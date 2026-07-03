// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExPath.h"

#include "GeomTools.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExMathAxis.h"
#include "Misc/ScopeLock.h"
#include "Paths/PCGExPathIntersectionDetails.h"
#include "Paths/PCGExPathsHelpers.h"

#include "Data/PCGPolygon2DData.h"

#define LOCTEXT_NAMESPACE "PCGExPaths"
#define PCGEX_NAMESPACE PCGExPaths

namespace PCGExPaths
{
	void BuildProjectedPoints2D(
		const TConstPCGValueRange<FTransform>& Positions,
		const FPCGExGeo2DProjectionDetails& Projection,
		TArray<FVector2D>& OutProjectedPoints,
		FBox2D& OutBounds)
	{
		const int32 NumPts = Positions.Num();
		OutProjectedPoints.SetNumUninitialized(NumPts);
		OutBounds = FBox2D(ForceInit);
		for (int32 i = 0; i < NumPts; ++i)
		{
			const FVector P = Projection.ProjectFlat(Positions[i].GetLocation());
			const FVector2D P2D(P.X, P.Y);
			OutProjectedPoints[i] = P2D;
			OutBounds += P2D;
		}
	}

	FPathEdge::FPathEdge(const int32 InStart, const int32 InEnd, const TConstPCGValueRange<FTransform>& Positions, const double Expansion)
		: Start(InStart)
		  , End(InEnd)
		  , AltStart(InStart)
	{
		Update(Positions, Expansion);
	}

	void FPathEdge::Update(const TConstPCGValueRange<FTransform>& Positions, const double Expansion)
	{
		PCGEX_SET_BOX_TOLERANCE(Bounds, Positions[Start].GetLocation(), Positions[End].GetLocation(), Expansion);
		const FVector Delta = Positions[End].GetLocation() - Positions[Start].GetLocation();
		Length = Delta.Size();
		Dir = Length > SMALL_NUMBER ? Delta / Length : FVector::ZeroVector;
	}

	bool FPathEdge::ShareIndices(const FPathEdge& Other) const
	{
		return Start == Other.Start || Start == Other.End || End == Other.Start || End == Other.End;
	}

	bool FPathEdge::Connects(const FPathEdge& Other) const
	{
		return Start == Other.End || End == Other.Start;
	}

	bool FPathEdge::ShareIndices(const FPathEdge* Other) const
	{
		return Start == Other->Start || Start == Other->End || End == Other->Start || End == Other->End;
	}

	void IPathEdgeExtra::ProcessingDone(const FPath* Path)
	{
	}

	FPath::FPath(const bool IsClosed)
		: bClosedLoop(IsClosed)
	{
	}

	FPath::FPath(const TConstPCGValueRange<FTransform>& InTransforms, const bool IsClosed, const double Expansion)
		: FPath(IsClosed)
	{
		Positions = InTransforms;
		NumPoints = InTransforms.Num();
		LastIndex = NumPoints - 1;

		BuildPath(Expansion);
	}

	FPath::FPath(const UPCGBasePointData* InPointData, const double Expansion)
		: FPath(InPointData->GetConstTransformValueRange(), Helpers::GetClosedLoop(InPointData), Expansion)
	{
	}

	PCGExMT::FScope FPath::GetEdgeScope(const int32 InLoopIndex) const
	{
		return PCGExMT::FScope(0, NumEdges, InLoopIndex);
	}

	int32 FPath::LoopPointIndex(const int32 Index) const
	{
		const int32 W = Index % NumPoints;
		return W < 0 ? W + NumPoints : W;
	}

	int32 FPath::SafePointIndex(const int32 Index) const
	{
		if (bClosedLoop)
		{
			return PCGExMath::Tile(Index, 0, LastIndex);
		}
		return Index < 0 ? 0 : Index > LastIndex ? LastIndex : Index;
	}

	FVector FPath::DirToNextPoint(const int32 Index) const
	{
		if (bClosedLoop)
		{
			return Edges[Index].Dir;
		}
		return Index == LastIndex ? Edges[Index - 1].Dir : Edges[Index].Dir;
	}

	FVector FPath::DirToNeighbor(const int32 Index, const int32 Offset) const
	{
		if (Offset < 0)
		{
			return DirToPrevPoint(Index);
		}
		return DirToNextPoint(Index);
	}

	PCGExMath::FClosestPosition FPath::FindClosestIntersection(const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& Segment) const
	{
		PCGExMath::FClosestPosition Closest(Segment.A);

		if (!Bounds.Intersect(Segment.Bounds))
		{
			return Closest;
		}

		const uint8 Strictness = InDetails.Strictness;

		GetEdgeOctree()->FindElementsWithBoundsTest(Segment.Bounds, [&](const FPathEdge* PathEdge)
		{
			if (InDetails.bWantsDotCheck)
			{
				if (!InDetails.CheckDot(FMath::Abs(Segment.Dot(PathEdge->Dir))))
				{
					return;
				}
			}

			FVector OnSegment = FVector::ZeroVector;
			FVector OnPath = FVector::ZeroVector;

			if (!Segment.FindIntersection(GetPos_Unsafe(PathEdge->Start), GetPos_Unsafe(PathEdge->End), InDetails.ToleranceSquared, OnSegment, OnPath, Strictness))
			{
				return;
			}

			Closest.Update(OnPath, PathEdge->Start);
		});

		return Closest;
	}

	PCGExMath::FClosestPosition FPath::FindClosestIntersection(const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& Segment, PCGExMath::FClosestPosition& OutClosestPosition) const
	{
		PCGExMath::FClosestPosition Closest(Segment.A);

		if (!Bounds.Intersect(Segment.Bounds))
		{
			return Closest;
		}

		const uint8 Strictness = InDetails.Strictness;

		GetEdgeOctree()->FindElementsWithBoundsTest(Segment.Bounds, [&](const FPathEdge* PathEdge)
		{
			if (InDetails.bWantsDotCheck)
			{
				if (!InDetails.CheckDot(FMath::Abs(Segment.Dot(PathEdge->Dir))))
				{
					return;
				}
			}

			FVector OnSegment = FVector::ZeroVector;
			FVector OnPath = FVector::ZeroVector;

			if (!Segment.FindIntersection(GetPos_Unsafe(PathEdge->Start), GetPos_Unsafe(PathEdge->End), InDetails.ToleranceSquared, OnSegment, OnPath, Strictness))
			{
				OutClosestPosition.Update(OnPath, -2);
				return;
			}

			OutClosestPosition.Update(OnPath, -2);
			Closest.Update(OnPath, PathEdge->Start);
		});

		return Closest;
	}

	void FPath::BuildEdgeOctree()
	{
		if (EdgeOctree)
		{
			return;
		}
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (FPathEdge& Edge : Edges)
		{
			if (!IsEdgeValid(Edge))
			{
				continue;
			}                              // Skip zero-length edges
			EdgeOctree->AddElement(&Edge); // Might be a problem if edges gets reallocated
		}
	}

	void FPath::BuildPartialEdgeOctree(const TArray<int8>& Filter)
	{
		if (EdgeOctree)
		{
			return;
		}
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (int i = 0; i < Edges.Num(); i++)
		{
			FPathEdge& Edge = Edges[i];
			if (!Filter[i] || !IsEdgeValid(Edge))
			{
				continue;
			}                              // Skip filtered out & zero-length edges
			EdgeOctree->AddElement(&Edge); // Might be a problem if edges gets reallocated
		}
	}

	void FPath::BuildPartialEdgeOctree(const TBitArray<>& Filter)
	{
		if (EdgeOctree)
		{
			return;
		}
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (int i = 0; i < Edges.Num(); i++)
		{
			FPathEdge& Edge = Edges[i];
			if (!Filter[i] || !IsEdgeValid(Edge))
			{
				continue;
			}                              // Skip filtered out & zero-length edges
			EdgeOctree->AddElement(&Edge); // Might be a problem if edges gets reallocated
		}
	}

	void FPath::UpdateConvexity(const int32 Index)
	{
		if (!bIsConvex)
		{
			return;
		}

		const int32 A = SafePointIndex(Index - 1);
		const int32 B = SafePointIndex(Index + 1);
		if (A == B)
		{
			bIsConvex = false;
			return;
		}

		PCGExMath::CheckConvex(Positions[A].GetLocation(), Positions[Index].GetLocation(), Positions[B].GetLocation(), bIsConvex, ConvexitySign);
	}

	void FPath::ComputeEdgeExtra(const int32 Index)
	{
		if (NumEdges == 1)
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
			{
				Extra->ProcessSingleEdge(this, Edges[0]);
			}
		}
		else
		{
			if (Index == 0)
			{
				for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
				{
					Extra->ProcessFirstEdge(this, Edges[0]);
				}
			}
			else if (Index == LastEdge)
			{
				for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
				{
					Extra->ProcessLastEdge(this, Edges[LastEdge]);
				}
			}
			else
			{
				for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
				{
					Extra->ProcessEdge(this, Edges[Index]);
				}
			}
		}
	}

	void FPath::ExtraComputingDone()
	{
		for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
		{
			Extra->ProcessingDone(this);
		}
		Extras.Empty(); // So we don't update them anymore
	}

	void FPath::ComputeAllEdgeExtra()
	{
		if (NumEdges == 1)
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
			{
				Extra->ProcessSingleEdge(this, Edges[0]);
			}
		}
		else
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
			{
				Extra->ProcessFirstEdge(this, Edges[0]);
			}
			for (int i = 1; i < LastEdge; i++)
			{
				for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
				{
					Extra->ProcessEdge(this, Edges[i]);
				}
			}
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras)
			{
				Extra->ProcessLastEdge(this, Edges[LastEdge]);
			}
		}

		ExtraComputingDone();
	}

	bool FPath::IsInsideProjection(const FVector& WorldPosition) const
	{
		const FVector2D ProjectedPoint = FVector2D(Projection.ProjectFlat(WorldPosition));
		if (!ProjectedBounds.IsInside(ProjectedPoint))
		{
			return false;
		}
		return FGeomTools2D::IsPointInPolygon(ProjectedPoint, ProjectedPoints);
	}

	bool FPath::Contains(const TConstPCGValueRange<FTransform>& InPositions, const double Tolerance) const
	{
		const int32 OtherNumPoints = InPositions.Num();
		const int32 Threshold = FMath::Max(1, FMath::RoundToInt(static_cast<double>(OtherNumPoints) * (1 - FMath::Clamp(Tolerance, 0, 1))));

		int32 InsideCount = 0;

		for (int i = 0; i < OtherNumPoints; i++)
		{
			if (IsInsideProjection(InPositions[i].GetLocation()))
			{
				InsideCount++;
				if (InsideCount >= Threshold)
				{
					return true;
				}
			}
		}

		return false;
	}

	void FPath::BuildProjection()
	{
		BuildProjectedPoints2D(Positions, Projection, ProjectedPoints, ProjectedBounds);
	}

	void FPath::BuildProjection(const FPCGExGeo2DProjectionDetails& InProjectionDetails)
	{
		Projection = InProjectionDetails;
		BuildProjection();
	}

	void FPath::OffsetProjection(const double Offset)
	{
		// Inset (negative offset) or outset (positive offset) the projected polygon.
		// At each vertex, average the inward normals of the two adjacent edges to get
		// a smooth offset direction, then shift the vertex along that direction.
		// This is a simple normal-averaging approach (not Minkowski-based), so it can
		// produce self-intersections on sharp concave corners with large offsets.
		if (FMath::IsNearlyZero(Offset))
		{
			return;
		}

		const int32 N = ProjectedPoints.Num();
		if (N < 3)
		{
			ProjectedBounds = ProjectedBounds.ExpandBy(Offset);
			return;
		}

		TArray<FVector2D> InsetPositions;
		InsetPositions.SetNum(N);

		ProjectedBounds = FBox2D();

		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = ProjectedPoints[(i - 1 + N) % N];
			const FVector2D& B = ProjectedPoints[i];
			const FVector2D& C = ProjectedPoints[(i + 1) % N];

			const FVector2D AB = (B - A).GetSafeNormal();
			const FVector2D BC = (C - B).GetSafeNormal();

			const FVector2D N1 = FVector2D(-AB.Y, AB.X);
			const FVector2D N2 = FVector2D(-BC.Y, BC.X);

			const FVector2D Avg = (N1 + N2).GetSafeNormal();

			const FVector2D Pos = B - Avg * Offset;
			InsetPositions[i] = Pos;
			ProjectedBounds += Pos;
		}

		if (Offset > 0)
		{
			ProjectedBounds = ProjectedBounds.ExpandBy(Offset);
		}

		ProjectedPoints = MoveTemp(InsetPositions);
	}

	void FPath::BuildPath(const double Expansion)
	{
		if (bClosedLoop)
		{
			NumEdges = NumPoints;
		}
		else
		{
			NumEdges = LastIndex;
		}

		LastEdge = NumEdges - 1;

		Edges.SetNumUninitialized(NumEdges);

		for (int i = 0; i < NumEdges; i++)
		{
			const FPathEdge& E = (Edges[i] = FPathEdge(i, (i + 1) % NumPoints, Positions, Expansion));
			TotalLength += E.Length;
			Bounds += E.Bounds.GetBox();
		}
	}

#pragma region Edge extras

#pragma region FPathEdgeLength

	void FPathEdgeLength::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = Edge.Length;
		TotalLength += Edge.Length;
	}

	void FPathEdgeLength::ProcessingDone(const FPath* Path)
	{
		TPathEdgeExtra<double>::ProcessingDone(Path);
		CumulativeLength.SetNumUninitialized(Data.Num());
		CumulativeLength[0] = Data[0];
		for (int i = 1; i < Data.Num(); i++)
		{
			CumulativeLength[i] = CumulativeLength[i - 1] + Data[i];
		}
	}

#pragma endregion

#pragma region FPathEdgeLength

	void FPathEdgeLengthSquared::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = Edge.Length * Edge.Length;
	}

#pragma endregion

#pragma region FPathEdgeNormal

	void FPathEdgeNormal::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = PCGExMath::SafeCrossNormal(Up, Edge.Dir);
	}

#pragma endregion

#pragma region FPathEdgeBinormal

	void FPathEdgeBinormal::ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		const FVector N = PCGExMath::SafeCrossNormal(Up, Edge.Dir);
		Normals[Edge.Start] = N;
		GetMutable(Edge.Start) = N;
	}

	void FPathEdgeBinormal::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		const FVector N = PCGExMath::SafeCrossNormal(Up, Edge.Dir);
		Normals[Edge.Start] = N;

		// The angular bisector between the incoming and outgoing edge directions gives a smooth
		// "binormal" at the vertex; for unit directions that is simply their normalized sum
		// (equivalent to rotating the incoming direction by half the angle between them).
		// If the result points away from the edge normal, flip it to maintain consistent orientation.
		const FVector A = Path->DirToPrevPoint(Edge.Start);
		FVector D = (A + Edge.Dir).GetSafeNormal();

		if (D.IsZero())
		{
			// Collinear vertex: the bisector is undefined, fall back to the edge normal
			D = N;
		}
		else if (FVector::DotProduct(N, D) < 0.0)
		{
			D *= -1;
		}

		GetMutable(Edge.Start) = D;
	}

#pragma endregion

#pragma region FPathEdgeAvgNormal

	void FPathEdgeAvgNormal::ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = PCGExMath::SafeCrossNormal(Up, Edge.Dir);
	}

	void FPathEdgeAvgNormal::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		const FVector A = PCGExMath::SafeCrossNormal(Up, Path->DirToPrevPoint(Edge.Start) * -1);
		const FVector B = PCGExMath::SafeCrossNormal(Up, Edge.Dir);
		FVector Avg = FMath::Lerp(A, B, 0.5).GetSafeNormal();

		if (Avg.IsZero())
		{
			// Opposite edge normals (hairpin): the average is undefined, favor the current edge
			Avg = B;
		}

		GetMutable(Edge.Start) = Avg;
	}

#pragma endregion

#pragma region FPathEdgeParallelTransportNormal

	void FPathEdgeParallelTransportNormal::ProcessSingleEdge(const FPath* Path, const FPathEdge& Edge)
	{
		ComputeAll(Path);
	}

	void FPathEdgeParallelTransportNormal::ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
	{
		ComputeAll(Path);
	}

	void FPathEdgeParallelTransportNormal::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		ComputeAll(Path);
	}

	void FPathEdgeParallelTransportNormal::ProcessLastEdge(const FPath* Path, const FPathEdge& Edge)
	{
		ComputeAll(Path);
	}

	void FPathEdgeParallelTransportNormal::ComputeAll(const FPath* Path)
	{
		if (bComputed.load(std::memory_order_acquire))
		{
			return;
		}

		FScopeLock Lock(&ComputeLock);

		if (bComputed.load(std::memory_order_relaxed))
		{
			return;
		}

		const int32 NumEdges = Path->NumEdges;

		// Seed from the first edge with a usable direction; zero-length edges carry the running normal
		int32 FirstUsableIndex = -1;
		for (int i = 0; i < NumEdges; i++)
		{
			if (!Path->Edges[i].Dir.IsZero())
			{
				FirstUsableIndex = i;
				break;
			}
		}

		if (FirstUsableIndex == -1)
		{
			// Fully degenerate path (all points coincident)
			for (int i = 0; i < NumEdges; i++)
			{
				GetMutable(i) = FVector::ZeroVector;
			}
			bComputed.store(true, std::memory_order_release);
			return;
		}

		const FVector FirstUsableDir = Path->Edges[FirstUsableIndex].Dir;
		FVector PrevDir = FirstUsableDir;
		FVector N = PCGExMath::SafeCrossNormal(Up, FirstUsableDir);

		for (int i = 0; i < NumEdges; i++)
		{
			const FVector Dir = Path->Edges[i].Dir;
			if (!Dir.IsZero())
			{
				N = FQuat::FindBetweenNormals(PrevDir, Dir).RotateVector(N);

				// Re-orthogonalize against the tangent to cancel accumulated numerical drift
				const FVector Ortho = (N - FVector::DotProduct(N, Dir) * Dir).GetSafeNormal();
				N = Ortho.IsZero() ? PCGExMath::SafeCrossNormal(Up, Dir) : Ortho;

				PrevDir = Dir;
			}

			GetMutable(i) = N;
		}

		// Closed loop: transport across the seam and distribute the residual twist along the path
		// so the frame does not jump between the last and first edge
		if (bClosedLoop && NumEdges > 1)
		{
			const FVector SeamN = FQuat::FindBetweenNormals(PrevDir, FirstUsableDir).RotateVector(N);
			const FVector FirstN = Get(FirstUsableIndex);
			const double SeamTwist = FMath::Atan2(
				FVector::DotProduct(FVector::CrossProduct(FirstN, SeamN), FirstUsableDir),
				FVector::DotProduct(FirstN, SeamN));

			if (!FMath::IsNearlyZero(SeamTwist))
			{
				const double TwistStep = SeamTwist / static_cast<double>(NumEdges);
				for (int i = 1; i < NumEdges; i++)
				{
					const FVector Dir = Path->Edges[i].Dir;
					if (Dir.IsZero())
					{
						continue;
					}
					GetMutable(i) = FQuat(Dir, -TwistStep * i).RotateVector(Get(i));
				}
			}
		}

		bComputed.store(true, std::memory_order_release);
	}

#pragma endregion

#pragma region FPathEdgeHalfAngle

	void FPathEdgeHalfAngle::ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = PI;
	}

	void FPathEdgeHalfAngle::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = FMath::Acos(FVector::DotProduct(Path->DirToPrevPoint(Edge.Start), Edge.Dir));
	}

#pragma endregion

#pragma region FPathEdgeAngle

	void FPathEdgeFullAngle::ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = 0;
	}

	void FPathEdgeFullAngle::ProcessEdge(const FPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = PCGExMath::GetAngle(Path->DirToPrevPoint(Edge.Start) * -1, Edge.Dir);
	}

#pragma endregion
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
