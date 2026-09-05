// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <vector>

#include "CoreMinimal.h"
#include "PCGExH.h"
#include "CompGeom/Delaunay2.h"
#include "CompGeom/Delaunay3.h"

struct FPCGExGeo2DProjectionDetails;

namespace PCGExMath::Geo
{
	constexpr static int32 MTX[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};

	struct PCGEXCORE_API FDelaunaySite2
	{
		int32 Vtx[3];
		int32 Neighbors[3];
		int32 Id = -1;
		bool bOnHull = true;

		FDelaunaySite2(const UE::Geometry::FIndex3i& InVtx, const UE::Geometry::FIndex3i& InAdjacency, const int32 InId = -1);
		FDelaunaySite2(const int32 A, const int32 B, const int32 C, const int32 InId = -1);

		bool ContainsEdge(const uint64 Edge) const;
		uint64 GetSharedEdge(const FDelaunaySite2* Other) const;
		void PushAdjacency(const int32 SiteId);

		FORCEINLINE uint64 AB() const
		{
			return PCGEx::H64U(Vtx[0], Vtx[1]);
		}

		FORCEINLINE uint64 BC() const
		{
			return PCGEx::H64U(Vtx[1], Vtx[2]);
		}

		FORCEINLINE uint64 AC() const
		{
			return PCGEx::H64U(Vtx[0], Vtx[2]);
		}
	};

	class PCGEXCORE_API TDelaunay2
	{
	public:
		TArray<FDelaunaySite2> Sites;

		TSet<uint64> DelaunayEdges;
		TSet<int32> DelaunayHull;
		bool IsValid = false;

		mutable FRWLock ProcessLock;

		TDelaunay2() = default;
		~TDelaunay2();

	protected:
		void Clear();

		bool ProcessDelaunator(const std::vector<double>& Coords, const bool bComputeDelaunayEdges, const bool bComputeHull);
		bool ProcessFallback(const TArray<FVector2D>& ProjectedPositions, const bool bComputeDelaunayEdges, const bool bComputeHull);

	public:
		/**
		 * Triangulate the given positions, projected onto a working plane.
		 * Sites (triangles + adjacency + per-site hull flag) are always built.
		 * @param bComputeDelaunayEdges Populate the DelaunayEdges set (unique undirected vtx pairs). Skip when only Sites/adjacency are needed.
		 * @param bComputeHull Populate the DelaunayHull vertex set.
		 */
		bool Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, const bool bComputeDelaunayEdges = true, const bool bComputeHull = true);

		/** Same as Process, but positions are already projected onto the working plane (X/Y planar, Z carried along) -- skips the internal projection pass. */
		bool ProcessProjected(const TArrayView<FVector>& ProjectedPositions, const bool bComputeDelaunayEdges = true, const bool bComputeHull = true);

		void RemoveLongestEdges(const TArrayView<FVector>& Positions);
		void RemoveLongestEdges(const TArrayView<FVector>& Positions, TSet<uint64>& LongestEdges);

		void GetMergedSites(const int32 SiteIndex, const TSet<uint64>& EdgeConnectors, TSet<int32>& OutMerged, TSet<uint64>& OutUEdges, TBitArray<>& VisitedSites);
	};

	struct PCGEXCORE_API FDelaunaySite3
	{
		uint32 Faces[4];
		int32 Vtx[4];
		int32 Id = -1;
		int8 bOnHull = 0;

		explicit FDelaunaySite3(const FIntVector4& InVtx, const int32 InId = -1);

		void ComputeFaces();
	};

	class PCGEXCORE_API TDelaunay3
	{
	public:
		TArray<FDelaunaySite3> Sites;

		TSet<uint64> DelaunayEdges;
		TSet<int32> DelaunayHull;
		TMap<uint32, uint64> Adjacency;

		bool IsValid = false;

		mutable FRWLock ProcessLock;

		TDelaunay3() = default;

		~TDelaunay3();

	protected:
		void Clear();

	public:
		template <bool bComputeAdjacency = false, bool bComputeHull = false>
		bool Process(const TArrayView<FVector>& Positions)
		{
			Clear();
			if (Positions.IsEmpty() || Positions.Num() <= 3)
			{
				return false;
			}

			UE::Geometry::FDelaunay3 Tetrahedralization;

			if (!Tetrahedralization.Triangulate(Positions))
			{
				Clear();
				return false;
			}

			IsValid = true;

			TArray<FIntVector4> Tetrahedra = Tetrahedralization.GetTetrahedra();

			const int32 NumSites = Tetrahedra.Num();
			const int32 NumReserve = NumSites * 3;

			DelaunayEdges.Reserve(NumReserve);

			TSet<uint32> FacesUsage;
			if constexpr (bComputeAdjacency)
			{
				Adjacency.Reserve(NumSites * 4);
			}
			if constexpr (bComputeHull)
			{
				FacesUsage.Reserve(NumSites);
			}

			Sites.SetNumUninitialized(NumSites);

			for (int i = 0; i < NumSites; i++)
			{
				Sites[i] = FDelaunaySite3(Tetrahedra[i], i);
				FDelaunaySite3& Site = Sites[i];

				for (int a = 0; a < 4; a++)
				{
					for (int b = a + 1; b < 4; b++)
					{
						DelaunayEdges.Add(PCGEx::H64U(Site.Vtx[a], Site.Vtx[b]));
					}
				}

				if constexpr (bComputeHull || bComputeAdjacency)
				{
					Site.ComputeFaces();
				}

				if constexpr (bComputeHull && bComputeAdjacency)
				{
					for (int f = 0; f < 4; f++)
					{
						const uint32 FH = Site.Faces[f];

						bool bAlreadySet = false;
						FacesUsage.Add(FH, &bAlreadySet);
						if (bAlreadySet)
						{
							FacesUsage.Remove(FH);
						}

						if (const uint64* AH = Adjacency.Find(FH))
						{
							Adjacency.Add(FH, PCGEx::NH64(i, PCGEx::NH64B(*AH)));
						}
						else
						{
							Adjacency.Add(FH, PCGEx::NH64(-1, i));
						}
					}
				}
				else if constexpr (bComputeHull)
				{
					for (int f = 0; f < 4; f++)
					{
						const uint32 FH = Site.Faces[f];
						bool bAlreadySet = false;
						FacesUsage.Add(FH, &bAlreadySet);
						if (bAlreadySet)
						{
							FacesUsage.Remove(FH);
						}
					}
				}
				else if constexpr (bComputeAdjacency)
				{
					for (int f = 0; f < 4; f++)
					{
						const uint32 FH = Site.Faces[f];
						if (const uint64* AH = Adjacency.Find(FH))
						{
							Adjacency.Add(FH, PCGEx::NH64(i, PCGEx::NH64B(*AH)));
						}
						else
						{
							Adjacency.Add(FH, PCGEx::NH64(-1, i));
						}
					}
				}
			}

			if constexpr (bComputeHull)
			{
				for (FDelaunaySite3& Site : Sites)
				{
					for (int f = 0; f < 4; f++)
					{
						if (FacesUsage.Contains(Site.Faces[f]))
						{
							for (int fi = 0; fi < 3; fi++)
							{
								DelaunayHull.Add(Site.Vtx[MTX[f][fi]]);
							}
							Site.bOnHull = true;
						}
					}
				}
			}

			FacesUsage.Empty();
			Tetrahedra.Empty();

			return IsValid;
		}

		void RemoveLongestEdges(const TArrayView<FVector>& Positions);
		void RemoveLongestEdges(const TArrayView<FVector>& Positions, TSet<uint64>& LongestEdges);
	};
}
