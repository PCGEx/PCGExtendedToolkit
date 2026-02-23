// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExCell.h"
#include "Misc/ScopeExit.h"

#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExCachedFaceEnumerator.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExPointElements.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGBasePointData.h"
#include "Math/Geo/PCGExGeo.h"

namespace PCGExClusters
{
	void SetPointProperty(PCGExData::FMutablePoint& InPoint, const double InValue, const EPCGExPointPropertyOutput InProperty)
	{
		if (InProperty == EPCGExPointPropertyOutput::Density)
		{
			TPCGValueRange<float> Density = InPoint.Data->GetDensityValueRange(false);
			Density[InPoint.Index] = InValue;
		}
		else if (InProperty == EPCGExPointPropertyOutput::Steepness)
		{
			TPCGValueRange<float> Steepness = InPoint.Data->GetSteepnessValueRange(false);
			Steepness[InPoint.Index] = InValue;
		}
		else if (InProperty == EPCGExPointPropertyOutput::ColorR)
		{
			TPCGValueRange<FVector4> Color = InPoint.Data->GetColorValueRange(false);
			Color[InPoint.Index].Component(0) = InValue;
		}
		else if (InProperty == EPCGExPointPropertyOutput::ColorG)
		{
			TPCGValueRange<FVector4> Color = InPoint.Data->GetColorValueRange(false);
			Color[InPoint.Index].Component(1) = InValue;
		}
		else if (InProperty == EPCGExPointPropertyOutput::ColorB)
		{
			TPCGValueRange<FVector4> Color = InPoint.Data->GetColorValueRange(false);
			Color[InPoint.Index].Component(2) = InValue;
		}
		else if (InProperty == EPCGExPointPropertyOutput::ColorA)
		{
			TPCGValueRange<FVector4> Color = InPoint.Data->GetColorValueRange(false);
			Color[InPoint.Index].Component(3) = InValue;
		}
	}

	void FProjectedPointSet::EnsureProjected()
	{
		{
			FReadScopeLock ReadScopeLock(ProjectionLock);
			if (!ProjectedPoints.IsEmpty()) { return; }
		}

		{
			FWriteScopeLock WriteScopeLock(ProjectionLock);
			if (!ProjectedPoints.IsEmpty()) { return; }

			// Project all points
			ProjectionDetails.ProjectFlat(PointDataFacade, ProjectedPoints);

			// Compute tight 2D and 3D AABBs
			TightBounds = FBox2D(ForceInit);
			TightBounds3D = FBox(ForceInit);

			const TConstPCGValueRange<FTransform> Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();
			const int32 Num = Transforms.Num();

			for (int32 i = 0; i < Num; ++i)
			{
				TightBounds += ProjectedPoints[i];
				TightBounds3D += Transforms[i].GetLocation();
			}
		}
	}

	bool FProjectedPointSet::OverlapsPolygon(const TArray<FVector2D>& Polygon, const FBox2D& PolygonBounds) const
	{
		const_cast<FProjectedPointSet*>(this)->EnsureProjected();

		// Coarse: Do bounds even overlap?
		if (!TightBounds.Intersect(PolygonBounds))
		{
			return false;
		}

		// Fine: Check individual points
		return PCGExMath::Geo::IsAnyPointInPolygon(ProjectedPoints, Polygon);
	}

	bool FProjectedPointSet::OverlapsPolygonLocal(
		const TArray<FVector2D>& Polygon,
		const FBox2D& PolygonBounds,
		const FBox& FaceBounds3D,
		const FPCGExGeo2DProjectionDetails& FaceProjection) const
	{
		const_cast<FProjectedPointSet*>(this)->EnsureProjected();

		// Coarse 3D check: do the hole points' 3D bounds even intersect the face's 3D bounds?
		if (!TightBounds3D.Intersect(FaceBounds3D))
		{
			return false;
		}

		// Fine: Project each hole point into the face's local 2D space and test containment
		const TConstPCGValueRange<FTransform> Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();
		const int32 NumPoints = Transforms.Num();

		for (int32 i = 0; i < NumPoints; ++i)
		{
			const FVector Projected = FaceProjection.Project(Transforms[i].GetLocation());
			const FVector2D Point2D(Projected.X, Projected.Y);

			if (!PolygonBounds.IsInside(Point2D)) { continue; }
			if (PCGExMath::Geo::IsPointInPolygon(Point2D, Polygon)) { return true; }
		}

		return false;
	}

	int32 FProjectedPointSet::Num() const{ return PointDataFacade->GetNum(); }

	FCellConstraints::FCellConstraints(const FPCGExCellConstraintsDetails& InDetails)
	{
		Winding = InDetails.OutputWinding;
		bConcaveOnly = InDetails.AspectFilter == EPCGExCellShapeTypeOutput::ConcaveOnly;
		bConvexOnly = InDetails.AspectFilter == EPCGExCellShapeTypeOutput::ConvexOnly;
		bKeepCellsWithLeaves = InDetails.bKeepCellsWithLeaves;
		bDuplicateLeafPoints = InDetails.bDuplicateLeafPoints;

		bBuildWrapper = InDetails.bOmitWrappingBounds;

		if (InDetails.bOmitBelowPointCount) { MinPointCount = InDetails.MinPointCount; }
		if (InDetails.bOmitAbovePointCount) { MaxPointCount = InDetails.MaxPointCount; }

		if (InDetails.bOmitBelowBoundsSize) { MinBoundsSize = InDetails.MinBoundsSize; }
		if (InDetails.bOmitAboveBoundsSize) { MaxBoundsSize = InDetails.MaxBoundsSize; }

		if (InDetails.bOmitBelowArea) { MinArea = InDetails.MinArea; }
		if (InDetails.bOmitAboveArea) { MaxArea = InDetails.MaxArea; }

		if (InDetails.bOmitBelowPerimeter) { MinPerimeter = InDetails.MinPerimeter; }
		if (InDetails.bOmitAbovePerimeter) { MaxPerimeter = InDetails.MaxPerimeter; }

		if (InDetails.bOmitBelowSegmentLength) { MinSegmentLength = InDetails.MinSegmentLength; }
		if (InDetails.bOmitAboveSegmentLength) { MaxSegmentLength = InDetails.MaxSegmentLength; }

		if (InDetails.bOmitBelowCompactness) { MinCompactness = InDetails.MinCompactness; }
		if (InDetails.bOmitAboveCompactness) { MaxCompactness = InDetails.MaxCompactness; }
	}

	void FCellConstraints::Reserve(const int32 InCellHashReserve)
	{
		UniqueStartHalfEdgesHash.Reserve(InCellHashReserve);
		UniquePathsHashSet.Reserve(InCellHashReserve);
	}

	bool FCellConstraints::ContainsSignedEdgeHash(const uint64 Hash)
	{
		return UniqueStartHalfEdgesHash.Contains(Hash);
	}

	bool FCellConstraints::IsUniqueStartHalfEdge(const uint64 Hash)
	{
		bool bAlreadyExists = false;
		UniqueStartHalfEdgesHash.Add(Hash, bAlreadyExists);
		return !bAlreadyExists;
	}

	bool FCellConstraints::IsUniqueCellHash(const TSharedPtr<FCell>& InCell)
	{
		bool bAlreadyExists;
		UniquePathsHashSet.Add(InCell->GetCellHash(), bAlreadyExists);
		return !bAlreadyExists;
	}

	TSharedPtr<FPlanarFaceEnumerator> FCellConstraints::GetOrBuildEnumerator(
		const TSharedRef<FCluster>& InCluster,
		const FPCGExGeo2DProjectionDetails& ProjectionDetails)
	{
		if (Enumerator) { return Enumerator; }

		// Compute projection hash for cache lookup
		const uint32 ProjHash = FFaceEnumeratorCacheFactory::ComputeProjectionHash(ProjectionDetails);

		// Try cluster cache first
		if (TSharedPtr<FCachedFaceEnumerator> Cached = InCluster->GetCachedData<FCachedFaceEnumerator>(
			FFaceEnumeratorCacheFactory::CacheKey, ProjHash))
		{
			Enumerator = Cached->Enumerator;
			return Enumerator;
		}

		// LocalTangent path: retrieve cached tangent frames and build with per-node frames
		if (ProjectionDetails.Method == EPCGExProjectionMethod::LocalTangent)
		{
			static const FName TangentFramesKey = FName("TangentFrames");
			if (TSharedPtr<FCachedTangentFrames> CachedFrames = InCluster->GetCachedData<FCachedTangentFrames>(TangentFramesKey))
			{
				Enumerator = MakeShared<FPlanarFaceEnumerator>();
				Enumerator->Build(InCluster, CachedFrames->NodeTangentFrames);

				// Opportunistically cache the enumerator
				if (Enumerator->IsBuilt())
				{
					TSharedPtr<FCachedFaceEnumerator> NewCached = MakeShared<FCachedFaceEnumerator>();
					NewCached->ContextHash = ProjHash;
					NewCached->Enumerator = Enumerator;
					InCluster->SetCachedData(FFaceEnumeratorCacheFactory::CacheKey, NewCached);
				}

				return Enumerator;
			}
		}

		// Build fresh with node-indexed positions (Normal/BestFit, or LocalTangent fallback if no cached frames)
		Enumerator = MakeShared<FPlanarFaceEnumerator>();
		Enumerator->Build(InCluster, ProjectionDetails);

		// Opportunistically cache for downstream
		if (Enumerator->IsBuilt())
		{
			TSharedPtr<FCachedFaceEnumerator> NewCached = MakeShared<FCachedFaceEnumerator>();
			NewCached->ContextHash = ProjHash;
			NewCached->Enumerator = Enumerator;
			// Enumerator now owns its projected positions (node-indexed)
			NewCached->ProjectedPositions = Enumerator->GetProjectedPositions();
			InCluster->SetCachedData(FFaceEnumeratorCacheFactory::CacheKey, NewCached);
		}

		return Enumerator;
	}

	void FCellConstraints::BuildWrapperCell(const TSharedPtr<FCellConstraints>& InConstraints)
	{
		if (!Enumerator || !Enumerator->IsBuilt())
		{
			// Cannot build wrapper without an enumerator - caller should call GetOrBuildEnumerator first
			return;
		}

		// LocalTangent: no topological wrapper — identify wrapper as the largest-area cell
		if (Enumerator->IsLocalTangent())
		{
			const TArray<FRawFace>& RawFaces = Enumerator->EnumerateRawFaces();
			const FCluster* Cluster = Enumerator->GetCluster();
			if (!Cluster || RawFaces.IsEmpty()) { return; }

			TSharedPtr<FCellConstraints> TempConstraints = MakeShared<FCellConstraints>();
			TempConstraints->bKeepCellsWithLeaves = true;
			TempConstraints->bDuplicateLeafPoints = InConstraints ? InConstraints->bDuplicateLeafPoints : bDuplicateLeafPoints;

			double LargestArea = -1.0;

			for (const FRawFace& RawFace : RawFaces)
			{
				TSharedPtr<FCell> Cell = MakeShared<FCell>(TempConstraints.ToSharedRef());
				const ECellResult Result = Enumerator->BuildCellFromRawFace(RawFace, Cell, TempConstraints.ToSharedRef());

				if (Result != ECellResult::Success && Result != ECellResult::Duplicate) { continue; }

				if (Cell->Data.Area > LargestArea)
				{
					LargestArea = Cell->Data.Area;
					WrapperCell = Cell;
				}
			}

			if (WrapperCell) { IsUniqueCellHash(WrapperCell); }
			return;
		}

		// Create minimal constraints for wrapper detection - no filtering
		TSharedPtr<FCellConstraints> TempConstraints = MakeShared<FCellConstraints>();
		TempConstraints->bKeepCellsWithLeaves = true;
		TempConstraints->bDuplicateLeafPoints = InConstraints ? InConstraints->bDuplicateLeafPoints : bDuplicateLeafPoints;

		// Get cached raw faces
		const TArray<FRawFace>& RawFaces = Enumerator->EnumerateRawFaces();
		const FCluster* Cluster = Enumerator->GetCluster();
		const TSharedPtr<TArray<FVector2D>>& ProjectedPositions = Enumerator->GetProjectedPositions();

		if (!Cluster || !ProjectedPositions) { return; }

		// Find the wrapper face by computing signed area directly from projected positions
		// CCW face (positive signed area) is the exterior/wrapper due to coordinate system inversion.
		// Note: ProjectedPositions is node-indexed, access directly via NodeIdx
		int32 WrapperFaceIdx = INDEX_NONE;
		double MostPositiveArea = 0; // Looking for most positive (CCW = wrapper)

		for (int32 FaceIdx = 0; FaceIdx < RawFaces.Num(); ++FaceIdx)
		{
			const TArray<int32>& FaceNodes = RawFaces[FaceIdx].Nodes;
			if (FaceNodes.Num() < 3) { continue; }

			// Compute signed area using shoelace formula on projected positions
			double SignedArea = 0;
			for (int32 i = 0; i < FaceNodes.Num(); ++i)
			{
				const int32 NodeA = FaceNodes[i];
				const int32 NodeB = FaceNodes[(i + 1) % FaceNodes.Num()];
				const FVector2D& PosA = (*ProjectedPositions)[NodeA];
				const FVector2D& PosB = (*ProjectedPositions)[NodeB];
				SignedArea += (PosA.X * PosB.Y - PosB.X * PosA.Y);
			}
			SignedArea *= 0.5;

			// CCW faces have positive signed area - find the most positive (largest CCW = wrapper)
			if (SignedArea > MostPositiveArea)
			{
				MostPositiveArea = SignedArea;
				WrapperFaceIdx = FaceIdx;
			}
		}

		// Build only the wrapper face into a full cell
		if (WrapperFaceIdx != INDEX_NONE)
		{
			TSharedPtr<FCell> Cell = MakeShared<FCell>(TempConstraints.ToSharedRef());
			const ECellResult Result = Enumerator->BuildCellFromRawFace(RawFaces[WrapperFaceIdx], Cell, TempConstraints.ToSharedRef());

			if (Result == ECellResult::Success || Result == ECellResult::Duplicate)
			{
				WrapperCell = Cell;
			}
		}

		// Fallback for tree structures (no CCW wrapper means no cycles = tree structure)
		if (!WrapperCell && Cluster->Nodes->Num() >= 2)
		{
			const TArray<FNode>& Nodes = *Cluster->Nodes;
			const int32 NumNodes = Nodes.Num();

			// Find a leaf node to start from (gives cleaner traversal)
			int32 StartNode = 0;
			for (int32 i = 0; i < NumNodes; ++i)
			{
				if (Nodes[i].IsLeaf())
				{
					StartNode = i;
					break;
				}
			}

			// DFS tree walk - visits each edge twice (once each direction)
			TArray<int32> WalkNodes;
			WalkNodes.Reserve(Cluster->Edges->Num() * 2 + 1);

			const bool bDuplicateLeaves = TempConstraints->bDuplicateLeafPoints;

			TSet<int32> VisitedNodes;
			TArray<TPair<int32, int32>> Stack;
			Stack.Reserve(NumNodes);
			Stack.Emplace(StartNode, 0);
			WalkNodes.Add(StartNode);
			if (bDuplicateLeaves && Nodes[StartNode].IsLeaf()) { WalkNodes.Add(StartNode); }
			VisitedNodes.Add(StartNode);

			while (!Stack.IsEmpty())
			{
				TPair<int32, int32>& Current = Stack.Last();
				const FNode& Node = Nodes[Current.Key];

				bool bFoundNext = false;
				while (Current.Value < Node.Links.Num())
				{
					const int32 NeighborNode = Node.Links[Current.Value].Node;
					Current.Value++;

					if (!VisitedNodes.Contains(NeighborNode))
					{
						VisitedNodes.Add(NeighborNode);
						WalkNodes.Add(NeighborNode);
						if (bDuplicateLeaves && Nodes[NeighborNode].IsLeaf()) { WalkNodes.Add(NeighborNode); }
						Stack.Emplace(NeighborNode, 0);
						bFoundNext = true;
						break;
					}
				}

				if (!bFoundNext)
				{
					Stack.Pop();
					if (!Stack.IsEmpty())
					{
						WalkNodes.Add(Stack.Last().Key);
					}
				}
			}

			if (WalkNodes.Num() >= 3)
			{
				WrapperCell = MakeShared<FCell>(TempConstraints.ToSharedRef());
				WrapperCell->Nodes = MoveTemp(WalkNodes);
				WrapperCell->Polygon.Reserve(WrapperCell->Nodes.Num());
				WrapperCell->Data.Bounds = FBox(ForceInit);
				WrapperCell->Data.Centroid = FVector::ZeroVector;
				WrapperCell->Bounds2D = FBox2D(ForceInit);

				TSet<int32> UniqueNodes;
				for (const int32 NodeIdx : WrapperCell->Nodes)
				{
					// ProjectedPositions is node-indexed, access directly via NodeIdx
					const FVector2D& Point2D = (*ProjectedPositions)[NodeIdx];
					WrapperCell->Polygon.Add(Point2D);
					WrapperCell->Bounds2D += Point2D;

					if (!UniqueNodes.Contains(NodeIdx))
					{
						UniqueNodes.Add(NodeIdx);
						const FVector Pos = Cluster->GetPos(NodeIdx);
						WrapperCell->Data.Bounds += Pos;
						WrapperCell->Data.Centroid += Pos;
					}
				}

				WrapperCell->Data.Centroid /= UniqueNodes.Num();
				WrapperCell->Data.bIsClosedLoop = true;
				WrapperCell->Data.bIsConvex = false;

				double Perimeter = 0;
				for (int32 i = 0; i < WrapperCell->Polygon.Num() - 1; ++i)
				{
					Perimeter += FVector2D::Distance(WrapperCell->Polygon[i], WrapperCell->Polygon[i + 1]);
				}
				WrapperCell->Data.Perimeter = Perimeter;
				WrapperCell->Data.Area = 0;
				WrapperCell->Data.Compactness = 0;

				WrapperCell->bBuiltSuccessfully = true;
			}
		}

		if (WrapperCell)
		{
			IsUniqueCellHash(WrapperCell);
		}
	}

	void FCellConstraints::BuildWrapperCell(const TSharedRef<FCluster>& InCluster, const FPCGExGeo2DProjectionDetails& ProjectionDetails)
	{
		// Build or get shared enumerator, then delegate to the overload that uses it
		GetOrBuildEnumerator(InCluster, ProjectionDetails);
		BuildWrapperCell(SharedThis(this));
	}

	void FCellConstraints::Cleanup()
	{
		WrapperCell = nullptr;
		Enumerator = nullptr;
	}

	uint64 FCell::GetCellHash()
	{
		if (CellHash != 0) { return CellHash; }
		CellHash = CityHash64(reinterpret_cast<const char*>(Nodes.GetData()), Nodes.Num() * sizeof(int32));
		return CellHash;
	}

	void FCell::PostProcessPoints(UPCGBasePointData* InMutablePoints)
	{
	}
}
