// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/External/PCGExMesh.h"

#include "CoreMinimal.h"
#include "PCGExH.h"
#include "StaticMeshResources.h"

#include "Core/PCGExContext.h"
#include "PCGParamData.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "Async/ParallelFor.h"
#include "Data/External/PCGExMeshImportDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"

namespace PCGExMesh
{
	class PCGEXCORE_API FExtractStaticMeshTask final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FExtractStaticMeshTask)

		explicit FExtractStaticMeshTask(const TSharedPtr<FGeoStaticMesh>& InGSM)
			: FTask(), GSM(InGSM)
		{
		}

		TSharedPtr<FGeoStaticMesh> GSM;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			GSM->ExtractMeshSynchronous();
		}
	};

	void DeclareGeoMeshImportInputs(const FPCGExGeoMeshImportDetails& InDetails, TArray<FPCGPinProperties>& PinProperties)
	{
		if (!InDetails.bImportUVs) { return; }

		{
			PCGEX_PIN_PARAMS(Labels::SourceUVImportRulesLabel, "Name/Channel output map. Attribute sets are expected to contain an FName attribute an int32 attribute.", Normal)
		}
	}

#pragma region Lookup

	namespace Internal
	{
		class IMeshLookup
		{
		protected:
			uint32 InternalIdx = 0;
			TArray<FVector>* Vertices = nullptr;
			TArray<int32>* RawIndices = nullptr;
			FVector HashTolerance = FVector(DefaultVertexMergeHashTolerance);
			bool bPreciseVertexMerge = true;

			/*
			 * This will be set false in the constructor if the hash tolerance is zero.
			 * That behavior is for future use in contexts other than Clusters, and the
			 * MeshToClusters node intentionally clamps the tolerance nonzero to prevent
			 * the behavior from triggering from that node.
			 */
			bool bEnableVertexMerge = true;

		public:
			virtual ~IMeshLookup() = default;
			/* Primary vertex hash */
			TMap<uint64, int32> Data;

			explicit IMeshLookup(
				const int32 Size,
				TArray<FVector>* InVertices,
				TArray<int32>* InRawIndices,
				const FVector& InHashTolerance = FVector(DefaultVertexMergeHashTolerance))
				: Vertices(InVertices), RawIndices(InRawIndices), HashTolerance(InHashTolerance)
			{
				/* Optimization to skip processing if tolerance is zero. */
				bEnableVertexMerge = HashTolerance.SizeSquared() > 0.0f;

				HashTolerance = bEnableVertexMerge ? PCGEx::SafeTolerance(InHashTolerance) : FVector(0.0f);
				Vertices->Reserve(Size);
				if (RawIndices) { RawIndices->Reserve(Size); }
			}

			virtual uint32 Add_GetIdx(const FVector& Position, const int32 RawIndex) = 0;

			/* Adds a vertex to the container and returns its index. */
			FORCEINLINE int32 AddVertex(const FVector& Position, const int32 RawIndex) const
			{
				const int32 Idx = Vertices->Emplace(Position);
				if (RawIndices) { RawIndices->Emplace(RawIndex); }
				return Idx;
			}

			FORCEINLINE int32 Num() const { return Data.Num(); }
		};

		template <bool bCollapse = true, bool bPrecise = true>
		class TMeshLookup : public IMeshLookup
		{
		public:
			explicit TMeshLookup(
				const int32 Size,
				TArray<FVector>* InVertices,
				TArray<int32>* InRawIndices,
				const FVector& InHashTolerance = FVector(DefaultVertexMergeHashTolerance))
				: IMeshLookup(Size, InVertices, InRawIndices, InHashTolerance)
			{
				if constexpr (bCollapse) { Data.Reserve(Size); }
			}

			virtual uint32 Add_GetIdx(const FVector& Position, const int32 RawIndex) override
			{
				// Spatial hash-based vertex deduplication.
				// Quantizes positions into grid cells of size HashTolerance and looks up by cell key.
				// The "precise" variant also checks a half-offset cell to catch vertices that
				// land near cell boundaries -- without this, two vertices 0.01 apart could hash
				// to different cells despite being within tolerance. Registering under both keys
				// ensures consistent deduplication regardless of which cell a future vertex hashes to.
				if constexpr (bCollapse)
				{
					const uint64 Key = PCGEx::SH3(Position, HashTolerance);

					// Check if exact cell has a match
					if (const int32* IdxPtr = Data.Find(Key)) { return *IdxPtr; }

					if constexpr (bPrecise)
					{
						// Check offset cell to catch vertices straddling cell boundaries
						const uint64 OffsetKey = PCGEx::SH3(Position + (0.5 * HashTolerance), HashTolerance);
						if (OffsetKey != Key)
						{
							if (const int32* IdxPtr = Data.Find(OffsetKey)) { return *IdxPtr; }
						}

						// Register under both keys so future vertices can find us from either cell
						const int32 Idx = AddVertex(Position, RawIndex);
						Data.Add(Key, Idx);
						if (OffsetKey != Key) { Data.Add(OffsetKey, Idx); }
						return Idx;
					}
					else
					{
						const int32 Idx = AddVertex(Position, RawIndex);
						Data.Add(Key, Idx);
						return Idx;
					}
				}
				else
				{
					return AddVertex(Position, RawIndex);
				}
			}
		};

		TUniquePtr<IMeshLookup> CreateMeshLookup(
			const int32 Size,
			TArray<FVector>* InVertices,
			TArray<int32>* InRawIndices,
			const FVector& InHashTolerance = FVector(DefaultVertexMergeHashTolerance),
			const bool bPreciseVertexMerge = true,
			const bool bMerge = true)
		{
			if (bPreciseVertexMerge)
			{
				if (bMerge) { return MakeUnique<TMeshLookup<true, true>>(Size, InVertices, InRawIndices, InHashTolerance); }
				return MakeUnique<TMeshLookup<false, true>>(Size, InVertices, InRawIndices, InHashTolerance);
			}

			if (bMerge) { return MakeUnique<TMeshLookup<true, false>>(Size, InVertices, InRawIndices, InHashTolerance); }
			return MakeUnique<TMeshLookup<false, false>>(Size, InVertices, InRawIndices, InHashTolerance);
		}
	}
#pragma endregion

	FMeshData::FMeshData(const UStaticMesh* InStaticMesh)
	{
		if (!InStaticMesh) { return; }

		const FStaticMeshRenderData* RenderData = InStaticMesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.IsEmpty()) { return; }

		const FStaticMeshLODResources* LODResource = &RenderData->LODResources[0];

		if (!LODResource) { return; }

		NumTexCoords = LODResource->GetNumTexCoords();
		Indices = LODResource->IndexBuffer.GetArrayView();
		if (Indices.Num() <= 0) { return; }

		Buffers = &LODResource->VertexBuffers;
		if (Buffers->ColorVertexBuffer.IsInitialized() && Buffers->ColorVertexBuffer.GetNumVertices() > 0) { Colors = &Buffers->ColorVertexBuffer; }

		Positions = &Buffers->PositionVertexBuffer;

		bIsValid = true;
	}

	void FGeoMesh::MakeDual()
	// Need triangulate first
	{
		// Convert mesh to its dual graph: each triangle becomes a vertex (at its centroid),
		// and edges connect adjacent triangles. The original vertex indices are stored in
		// the Triangle entries for later reference. RawIndices are replaced with negative
		// values (-(triIndex+1)) to distinguish dual vertices from original mesh vertices.
		if (Triangles.IsEmpty()) { return; }

		TArray<FVector> DualPositions;
		PCGExArrayHelpers::InitArray(DualPositions, Triangles.Num());

		Edges.Empty();

		for (int i = 0; i < Triangles.Num(); i++)
		{
			FIntVector3& Triangle = Triangles[i];
			DualPositions[i] = (Vertices[Triangle.X] + Vertices[Triangle.Y] + Vertices[Triangle.Z]) / 3;

			Triangle.X = RawIndices[Triangle.X];
			Triangle.Y = RawIndices[Triangle.Y];
			Triangle.Z = RawIndices[Triangle.Z];

			const FIntVector3& Adjacency = Tri_Adjacency[i];
			if (Adjacency.X != -1) { Edges.Add(PCGEx::H64U(i, Adjacency.X)); }
			if (Adjacency.Y != -1) { Edges.Add(PCGEx::H64U(i, Adjacency.Y)); }
			if (Adjacency.Z != -1) { Edges.Add(PCGEx::H64U(i, Adjacency.Z)); }
		}

		// Raw indices have been mutated and stored in triangles instead.
		RawIndices.SetNum(Triangles.Num());
		for (int i = 0; i < Triangles.Num(); i++) { RawIndices[i] = -(i + 1); }

		Vertices.Reset(DualPositions.Num());
		Vertices = MoveTemp(DualPositions);

		Tri_Adjacency.Empty();
	}

	void FGeoMesh::MakeHollowDual()
	// Need triangulate first
	{
		if (Triangles.IsEmpty()) { return; }

		const int32 StartIndex = Vertices.Num();
		const int32 NumTriangles = Triangles.Num();
		Vertices.SetNum(StartIndex + NumTriangles);
		RawIndices.SetNum(StartIndex + NumTriangles);

		Edges.Empty();

		for (int i = 0; i < NumTriangles; i++)
		{
			const FIntVector3& Triangle = Triangles[i];
			const int32 E = StartIndex + i;

			Vertices[E] = (Vertices[Triangle.X] + Vertices[Triangle.Y] + Vertices[Triangle.Z]) / 3;
			RawIndices[E] = -(i + 1);

			Edges.Add(PCGEx::H64U(E, Triangle.X));
			Edges.Add(PCGEx::H64U(E, Triangle.Y));
			Edges.Add(PCGEx::H64U(E, Triangle.Z));
		}

		Tri_Adjacency.Empty();
	}

	FGeoStaticMesh::FGeoStaticMesh(
		const TSoftObjectPtr<UStaticMesh>& InSoftStaticMesh,
		const FVector& InCWTolerance,
		const bool bInPreciseVertexMerge)
		: CWTolerance(InCWTolerance),
		  bPreciseVertexMerge(bInPreciseVertexMerge)
	{
		if (!InSoftStaticMesh.ToSoftObjectPath().IsValid()) { return; }
		if (!InSoftStaticMesh.Get()) { MeshHandle = PCGExHelpers::LoadBlocking_AnyThreadTpl(InSoftStaticMesh); }

		StaticMesh = InSoftStaticMesh.Get();
		if (!StaticMesh) { return; }

		StaticMesh->GetRenderData();
		bIsValid = true;
	}

	FGeoStaticMesh::FGeoStaticMesh(
		const FSoftObjectPath& InSoftStaticMesh,
		const FVector& InCWTolerance,
		const bool bInPreciseVertexMerge)
		: FGeoStaticMesh(TSoftObjectPtr<UStaticMesh>(InSoftStaticMesh),
		                 InCWTolerance, bInPreciseVertexMerge)
	{
	}

	FGeoStaticMesh::FGeoStaticMesh(
		const FString& InSoftStaticMesh,
		const FVector& InCWTolerance,
		const bool bInPreciseVertexMerge)
		: FGeoStaticMesh(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(InSoftStaticMesh)),
		                 InCWTolerance, bInPreciseVertexMerge)
	{
	}

	void FGeoStaticMesh::ExtractMeshSynchronous()
	{
		if (bIsLoaded) { return; }
		if (!bIsValid) { return; }

		RawData = FMeshData(StaticMesh);

		if (!RawData.bIsValid)
		{
			bIsValid = false;
			return;
		}

		const FPositionVertexBuffer& PositionBuffer = *RawData.Positions;
		const FIndexArrayView Indices = RawData.Indices;
		const int32 NumTriangles = RawData.NumTriangles();

		TUniquePtr<Internal::IMeshLookup> MeshLookup = Internal::CreateMeshLookup(PositionBuffer.GetNumVertices() / 3, &Vertices, &RawIndices, CWTolerance, bPreciseVertexMerge);
		Edges.Reserve(NumTriangles / 2);

		for (int i = 0; i < Indices.Num(); i += 3)
		{
			const int32 RawA = Indices[i];
			const int32 RawB = Indices[i + 1];
			const int32 RawC = Indices[i + 2];

			const uint32 A = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawA)), RawA);
			const uint32 B = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawB)), RawB);
			const uint32 C = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawC)), RawC);

			if (A != B) { Edges.Add(PCGEx::H64U(A, B)); }
			if (B != C) { Edges.Add(PCGEx::H64U(B, C)); }
			if (C != A) { Edges.Add(PCGEx::H64U(C, A)); }
		}

		bIsLoaded = true;
	}

	void FGeoStaticMesh::TriangulateMeshSynchronous()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGeoStaticMesh::TriangulateMeshSynchronous);

		if (bIsLoaded) { return; }
		if (!bIsValid) { return; }

		RawData = FMeshData(StaticMesh);

		if (!RawData.bIsValid)
		{
			bIsValid = false;
			return;
		}

		const FPositionVertexBuffer& PositionBuffer = *RawData.Positions;
		const FIndexArrayView Indices = RawData.Indices;
		const int32 NumTriangles = RawData.NumTriangles();

		Edges.Empty();

		TUniquePtr<Internal::IMeshLookup> MeshLookup = Internal::CreateMeshLookup(RawData.NumVertices() / 3, &Vertices, &RawIndices, CWTolerance, bPreciseVertexMerge);

		Triangles.Init(FIntVector3(-1), NumTriangles);
		Tri_Adjacency.Init(FIntVector3(-1), NumTriangles);

		TBitArray<> Tri_IsOnHull;
		Tri_IsOnHull.Init(true, NumTriangles);

		TMap<uint64, int32> EdgeMap;
		EdgeMap.Reserve(NumTriangles / 2);

		auto PushAdjacency = [&](const int32 Tri, const int32 OtherTri)
		{
			FIntVector3& Adjacency = Tri_Adjacency[Tri];
			for (int i = 0; i < 3; i++)
			{
				if (Adjacency[i] == -1)
				{
					Adjacency[i] = OtherTri;
					if (i == 2) { Tri_IsOnHull[Tri] = false; }
					break;
				}
			}
		};

		auto PushEdge = [&](const int32 Tri, const uint64 Edge)
		{
			bool bIsAlreadySet = false;
			Edges.Add(Edge, &bIsAlreadySet);
			if (bIsAlreadySet)
			{
				if (int32 OtherTri = -1; EdgeMap.RemoveAndCopyValue(Edge, OtherTri))
				{
					PushAdjacency(OtherTri, Tri);
					PushAdjacency(Tri, OtherTri);
				}
			}
			else
			{
				EdgeMap.Add(Edge, Tri);
			}
		};

		int32 Ti = 0;
		for (int i = 0; i < Indices.Num(); i += 3)
		{
			const int32 RawA = Indices[i];
			const int32 RawB = Indices[i + 1];
			const int32 RawC = Indices[i + 2];

			const uint32 A = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawA)), RawA);
			const uint32 B = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawB)), RawB);
			const uint32 C = MeshLookup->Add_GetIdx(FVector(PositionBuffer.VertexPosition(RawC)), RawC);

			if (A == B || B == C || C == A) { continue; }

			Triangles[Ti] = FIntVector3(A, B, C);

			PushEdge(Ti, PCGEx::H64U(A, B));
			PushEdge(Ti, PCGEx::H64U(B, C));
			PushEdge(Ti, PCGEx::H64U(A, C));

			Ti++;
		}

		Triangles.SetNum(Ti);
		if (Triangles.IsEmpty())
		{
			bIsValid = false;
			return;
		}

		for (int i = 0; i < Triangles.Num(); i++)
		{
			FIntVector3& Tri = Triangles[i];
			const int32 A = Tri[0];
			const int32 B = Tri[1];
			const int32 C = Tri[2];

			if (Tri_IsOnHull[i])
			{
				const uint64 AB = PCGEx::H64U(A, B);
				const uint64 BC = PCGEx::H64U(B, C);
				const uint64 AC = PCGEx::H64U(A, C);

				// Push edges that are still waiting to be matched
				if (EdgeMap.Contains(AB))
				{
					HullIndices.Add(A);
					HullIndices.Add(B);
					HullEdges.Add(AB);
				}

				if (EdgeMap.Contains(BC))
				{
					HullIndices.Add(B);
					HullIndices.Add(C);
					HullEdges.Add(BC);
				}

				if (EdgeMap.Contains(AC))
				{
					HullIndices.Add(A);
					HullIndices.Add(C);
					HullEdges.Add(AC);
				}
			}
		}

		bIsLoaded = true;
	}

	void FGeoStaticMesh::ExtractMeshAsync(PCGExMT::FTaskManager* TaskManager)
	{
		if (bIsLoaded) { return; }
		if (!bIsValid) { return; }

		PCGEX_LAUNCH(FExtractStaticMeshTask, SharedThis(this))
	}

	FGeoStaticMesh::~FGeoStaticMesh()
	{
		PCGExHelpers::SafeReleaseHandle(MeshHandle);
	}

	int32 FGeoStaticMeshMap::FindOrAdd(const FSoftObjectPath& InPath)
	{
		if (const int32* GSMPtr = Map.Find(InPath)) { return *GSMPtr; }

		PCGEX_MAKE_SHARED(GSM, FGeoStaticMesh, InPath, CWTolerance, bPreciseVertexMerge)
		if (!GSM->bIsValid) { return -1; }

		const int32 Index = GSMs.Add(GSM);
		GSM->DesiredTriangulationType = DesiredTriangulationType;
		Map.Add(InPath, Index);
		return Index;
	}
}
