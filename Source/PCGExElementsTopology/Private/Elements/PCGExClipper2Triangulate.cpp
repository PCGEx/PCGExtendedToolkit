// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClipper2Triangulate.h"

#include "PCGComponent.h"
#include "UDynamicMesh.h"
#include "Clipper2Lib/clipper.h"
#include "Clipper2Lib/clipper.triangulation.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "GeometryScript/MeshRepairFunctions.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2TriangulateElement"
#define PCGEX_NAMESPACE Clipper2Triangulate

TArray<FPCGPinProperties> UPCGExClipper2TriangulateSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::MeshOutputLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(Clipper2Triangulate)

FPCGExGeo2DProjectionDetails UPCGExClipper2TriangulateSettings::GetProjectionDetails() const
{
	return ProjectionDetails;
}

uint64 FPCGExClipper2TriangulateContext::HashPoint(int64 X, int64 Y)
{
	return PCGEx::H64(
		static_cast<uint32>(X & 0xFFFFFFFF),
		static_cast<uint32>(Y & 0xFFFFFFFF));
}

void FPCGExClipper2TriangulateContext::AddStagedOutput(UPCGDynamicMeshData* MeshData, const TSet<FString>& Tags, int32 OrderIndex)
{
	FScopeLock Lock(&StagedOutputsLock);
	StagedOutputs.Emplace(MeshData, Tags, OrderIndex);
}

void FPCGExClipper2TriangulateContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	const UPCGExClipper2TriangulateSettings* Settings = GetInputSettings<UPCGExClipper2TriangulateSettings>();

	if (!Group->IsValid())
	{
		return;
	}
	if (Group->SubjectPaths.empty())
	{
		return;
	}

	// Local data for this group - no shared state
	TArray<FPCGExTriangulationVertex> VertexPool;
	TMap<uint64, int32> VertexMap;
	TArray<FIntVector> Triangles;

	const double InvScale = 1.0 / static_cast<double>(Settings->Precision);

	// Estimate and reserve
	int32 EstimatedPoints = 0;
	for (const int32 SubjectIdx : Group->SubjectIndices)
	{
		if (SubjectIdx < AllOpData->Paths.Num())
		{
			EstimatedPoints += AllOpData->Paths[SubjectIdx].size();
		}
	}
	VertexPool.Reserve(EstimatedPoints);
	VertexMap.Reserve(EstimatedPoints);
	Triangles.Reserve(EstimatedPoints * 2);

	// Build vertex pool from this group's paths
	for (const int32 SubjectIdx : Group->SubjectIndices)
	{
		if (SubjectIdx >= AllOpData->Paths.Num())
		{
			continue;
		}

		const auto& Path = AllOpData->Paths[SubjectIdx];
		const auto& Facade = AllOpData->Facades[SubjectIdx];
		const auto& Projection = AllOpData->Projections[SubjectIdx];

		TConstPCGValueRange<FTransform> Transforms = Facade->Source->GetIn()->GetConstTransformValueRange();
		TConstPCGValueRange<FVector4> Colors = Facade->Source->GetIn()->GetConstColorValueRange();

		for (size_t i = 0; i < Path.size(); ++i)
		{
			const auto& Pt = Path[i];
			const uint64 Hash = HashPoint(Pt.x, Pt.y);

			// Skip if already in pool
			if (VertexMap.Contains(Hash))
			{
				continue;
			}

			// Decode source info from Z
			uint32 PointIdx, SourceIdx;
			PCGEx::H64(static_cast<uint64>(Pt.z), PointIdx, SourceIdx);

			FPCGExTriangulationVertex Vertex;
			Vertex.ClipperX = Pt.x;
			Vertex.ClipperY = Pt.y;
			Vertex.SourceDataIndex = static_cast<int32>(SourceIdx);
			Vertex.SourcePointIndex = static_cast<int32>(PointIdx);

			// Get position from source or unproject
			if (PointIdx < static_cast<uint32>(Transforms.Num()))
			{
				Vertex.Position = Transforms[PointIdx].GetLocation();
				Vertex.Color = Colors[PointIdx];
			}
			else
			{
				// Unproject from Clipper2 coordinates
				Vertex.Position = Projection.Unproject(
					FVector(
						static_cast<double>(Pt.x) * InvScale,
						static_cast<double>(Pt.y) * InvScale,
						0.0));
				Vertex.Color = FVector4(1, 1, 1, 1);
			}

			const int32 PoolIndex = VertexPool.Num();
			VertexMap.Add(Hash, PoolIndex);
			VertexPool.Add(MoveTemp(Vertex));
		}
	}

	// Perform triangulation with ZCallback to preserve origin data through the internal Union
	PCGExClipper2Lib::Paths64 TrianglePaths;
	PCGExClipper2Lib::TriangulateResult Result =
		PCGExClipper2Lib::TriangulateWithHoles(
			Group->SubjectPaths,
			TrianglePaths,
			PCGExClipper2::ConvertFillRule(Settings->FillRule),
			Settings->bUseDelaunay,
			Group->CreateZCallback()
			);

	if (Result != PCGExClipper2Lib::TriangulateResult::success)
	{
		switch (Result)
		{
		case PCGExClipper2Lib::TriangulateResult::fail:
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Triangulation failed."));
			break;
		case PCGExClipper2Lib::TriangulateResult::no_polygons:
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("No valid polygons for triangulation."));
			break;
		case PCGExClipper2Lib::TriangulateResult::paths_intersect:
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Paths intersect - triangulation requires non-intersecting paths. Consider using Boolean Union first."));
			break;
		default:
			break;
		}
		return;
	}

	// Helper lambda to find or create vertex index
	auto FindOrCreateVertexIndex = [&](const PCGExClipper2Lib::Point64& Pt) -> int32
	{
		const uint64 Hash = HashPoint(Pt.x, Pt.y);
		if (const int32* Found = VertexMap.Find(Hash))
		{
			return *Found;
		}

		// Not found - check if this is an intersection point with blend info
		uint32 PointIdx, SourceIdx;
		PCGEx::H64(static_cast<uint64>(Pt.z), PointIdx, SourceIdx);

		FPCGExTriangulationVertex Vertex;
		Vertex.ClipperX = Pt.x;
		Vertex.ClipperY = Pt.y;

		if (PointIdx == PCGExClipper2::INTERSECTION_MARKER)
		{
			// Intersection point - interpolate from blend info
			if (const PCGExClipper2::FIntersectionBlendInfo* BlendInfo = Group->GetIntersectionBlendInfo(Pt.x, Pt.y))
			{
				// Interpolate position from the 4 contributing source points
				auto GetSourcePos = [&](uint32 SrcIdx, uint32 PtIdx, FVector& OutPos, FVector4& OutColor) -> bool
				{
					const int32 ArrayIdx = static_cast<int32>(SrcIdx);
					if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num())
					{
						return false;
					}

					const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[ArrayIdx];
					TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
					TConstPCGValueRange<FVector4> SrcColors = SrcFacade->Source->GetIn()->GetConstColorValueRange();

					if (static_cast<int32>(PtIdx) >= SrcTransforms.Num())
					{
						return false;
					}
					OutPos = SrcTransforms[PtIdx].GetLocation();
					OutColor = SrcColors[PtIdx];
					return true;
				};

				FVector E1BotPos, E1TopPos, E2BotPos, E2TopPos;
				FVector4 E1BotCol, E1TopCol, E2BotCol, E2TopCol;

				bool bHas1B = GetSourcePos(BlendInfo->E1BotSourceIdx, BlendInfo->E1BotPointIdx, E1BotPos, E1BotCol);
				bool bHas1T = GetSourcePos(BlendInfo->E1TopSourceIdx, BlendInfo->E1TopPointIdx, E1TopPos, E1TopCol);
				bool bHas2B = GetSourcePos(BlendInfo->E2BotSourceIdx, BlendInfo->E2BotPointIdx, E2BotPos, E2BotCol);
				bool bHas2T = GetSourcePos(BlendInfo->E2TopSourceIdx, BlendInfo->E2TopPointIdx, E2TopPos, E2TopCol);

				FVector E1Pos = bHas1B ? (bHas1T ? FMath::Lerp(E1BotPos, E1TopPos, BlendInfo->E1Alpha) : E1BotPos) : E1TopPos;
				FVector E2Pos = bHas2B ? (bHas2T ? FMath::Lerp(E2BotPos, E2TopPos, BlendInfo->E2Alpha) : E2BotPos) : E2TopPos;

				FVector4 E1Col = bHas1B ? (bHas1T ? FMath::Lerp(E1BotCol, E1TopCol, BlendInfo->E1Alpha) : E1BotCol) : E1TopCol;
				FVector4 E2Col = bHas2B ? (bHas2T ? FMath::Lerp(E2BotCol, E2TopCol, BlendInfo->E2Alpha) : E2BotCol) : E2TopCol;

				Vertex.Position = (E1Pos + E2Pos) * 0.5;
				Vertex.Color = (E1Col + E2Col) * 0.5;

				// Use first available source index
				Vertex.SourceDataIndex = static_cast<int32>(BlendInfo->E1BotSourceIdx);
				Vertex.SourcePointIndex = static_cast<int32>(BlendInfo->E1BotPointIdx);
			}
			else
			{
				// No blend info - fall back to unprojection
				const FPCGExGeo2DProjectionDetails& Projection = AllOpData->Projections.Num() > 0
					? AllOpData->Projections[0]
					: FPCGExGeo2DProjectionDetails();

				Vertex.Position = Projection.Unproject(
					FVector(
						static_cast<double>(Pt.x) * InvScale,
						static_cast<double>(Pt.y) * InvScale,
						0.0));
				Vertex.Color = FVector4(1, 1, 1, 1);
			}
		}
		else
		{
			// Regular point not found in map - try to restore from source
			Vertex.SourceDataIndex = static_cast<int32>(SourceIdx);
			Vertex.SourcePointIndex = static_cast<int32>(PointIdx);

			if (static_cast<int32>(SourceIdx) < AllOpData->Facades.Num())
			{
				const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[SourceIdx];
				TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
				TConstPCGValueRange<FVector4> SrcColors = SrcFacade->Source->GetIn()->GetConstColorValueRange();

				if (static_cast<int32>(PointIdx) < SrcTransforms.Num())
				{
					Vertex.Position = SrcTransforms[PointIdx].GetLocation();
					Vertex.Color = SrcColors[PointIdx];
				}
				else
				{
					const FPCGExGeo2DProjectionDetails& Projection = AllOpData->Projections[SourceIdx];
					Vertex.Position = Projection.Unproject(
						FVector(
							static_cast<double>(Pt.x) * InvScale,
							static_cast<double>(Pt.y) * InvScale,
							0.0));
					Vertex.Color = FVector4(1, 1, 1, 1);
				}
			}
			else
			{
				const FPCGExGeo2DProjectionDetails& Projection = AllOpData->Projections.Num() > 0
					? AllOpData->Projections[0]
					: FPCGExGeo2DProjectionDetails();

				Vertex.Position = Projection.Unproject(
					FVector(
						static_cast<double>(Pt.x) * InvScale,
						static_cast<double>(Pt.y) * InvScale,
						0.0));
				Vertex.Color = FVector4(1, 1, 1, 1);
			}
		}

		const int32 PoolIndex = VertexPool.Num();
		VertexMap.Add(Hash, PoolIndex);
		VertexPool.Add(MoveTemp(Vertex));
		return PoolIndex;
	};

	// Convert triangle paths to indexed triangles
	for (const auto& TriPath : TrianglePaths)
	{
		if (TriPath.size() != 3)
		{
			continue;
		}

		const int32 V0 = FindOrCreateVertexIndex(TriPath[0]);
		const int32 V1 = FindOrCreateVertexIndex(TriPath[1]);
		const int32 V2 = FindOrCreateVertexIndex(TriPath[2]);

		// Skip degenerate triangles
		if (V0 == V1 || V1 == V2 || V2 == V0)
		{
			continue;
		}

		Triangles.Add(FIntVector(V0, V1, V2));
	}

	if (VertexPool.IsEmpty() || Triangles.IsEmpty())
	{
		return;
	}

	// Create mesh objects
	TObjectPtr<UPCGDynamicMeshData> MeshData = ManagedObjects->New<UPCGDynamicMeshData>();
	if (!MeshData)
	{
		return;
	}

	TObjectPtr<UDynamicMesh> Mesh = ManagedObjects->New<UDynamicMesh>();
	Mesh->InitializeMesh();

	MeshData->Initialize(Mesh, true);
	Mesh = MeshData->GetMutableDynamicMesh();

	if (UMaterialInterface* Material = Settings->Topology.Material.Get())
	{
		MeshData->SetMaterials({Material});
	}

	// Get component transform for inverse transform
	FTransform Transform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, this);

	const int32 NumVertices = VertexPool.Num();
	const int32 NumTriangles = Triangles.Num();

	// Build source tracking arrays for UV writing
	TArray<int32> SourceDataIndices;
	TArray<int32> SourcePointIndices;
	SourceDataIndices.SetNum(NumVertices);
	SourcePointIndices.SetNum(NumVertices);

	for (int32 i = 0; i < NumVertices; ++i)
	{
		SourceDataIndices[i] = VertexPool[i].SourceDataIndex;
		SourcePointIndices[i] = VertexPool[i].SourcePointIndex;
	}

	// Arrays to collect for UV processing
	TArray<int32> VertexIDs;
	TArray<int32> ColorElemIDs;
	TArray<int32> TriangleIDs;

	VertexIDs.SetNum(NumVertices);
	ColorElemIDs.SetNum(NumVertices);
	TriangleIDs.Reserve(NumTriangles);

	Mesh->EditMesh([&](FDynamicMesh3& InMesh)
	{
		// Enable attributes
		InMesh.EnableAttributes();
		InMesh.Attributes()->EnablePrimaryColors();
		InMesh.Attributes()->EnableMaterialID();

		UE::Geometry::FDynamicMeshColorOverlay* Colors = InMesh.Attributes()->PrimaryColors();
		UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

		// Add vertices
		for (int32 i = 0; i < NumVertices; i++)
		{
			const FPCGExTriangulationVertex& Vtx = VertexPool[i];

			// Add vertex position (transformed to local space)
			VertexIDs[i] = InMesh.AppendVertex(Transform.InverseTransformPosition(Vtx.Position));

			// Prepare color
			ColorElemIDs[i] = Colors->AppendElement(FVector4f(Vtx.Color));
		}

		// Add triangles
		for (int32 i = 0; i < NumTriangles; i++)
		{
			const FIntVector& Tri = Triangles[i];

			const int32 TriID = InMesh.AppendTriangle(
				VertexIDs[Tri.X],
				VertexIDs[Tri.Y],
				VertexIDs[Tri.Z]);

			if (TriID >= 0)
			{
				MaterialID->SetValue(TriID, 0);
				Colors->SetTriangle(
					TriID,
					UE::Geometry::FIndex3i(
						ColorElemIDs[Tri.X],
						ColorElemIDs[Tri.Y],
						ColorElemIDs[Tri.Z]));
				TriangleIDs.Add(TriID);
			}
		}

		// Write UVs using multi-facade lookup
		Settings->Topology.UVChannels.Write(
			TriangleIDs,
			VertexIDs,
			SourceDataIndices,
			SourcePointIndices,
			AllOpData->Facades,
			InMesh);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);

	// Attempt repair if requested
	if (Settings->bAttemptRepair)
	{
		UGeometryScriptLibrary_MeshRepairFunctions::RepairMeshDegenerateGeometry(Mesh, Settings->RepairDegenerate);
	}

	// Post-process mesh
	Settings->Topology.PostProcessMesh(Mesh);

	// Add to staged outputs for deterministic ordering
	TSet<FString> Tags;
	if (Group->GroupTags)
	{
		Tags = Group->GroupTags->Flatten();
	}

	AddStagedOutput(MeshData, Tags, Group->GroupIndex);
}

void FPCGExClipper2TriangulateElement::OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Triangulate)

	// Sort by OrderIndex for deterministic output
	Context->StagedOutputs.Sort([](const FPCGExStagedMeshOutput& A, const FPCGExStagedMeshOutput& B)
	{
		return A.OrderIndex < B.OrderIndex;
	});

	// Stage outputs in order
	for (const FPCGExStagedMeshOutput& Output : Context->StagedOutputs)
	{
		if (Output.MeshData)
		{
			Context->StageOutput(
				Output.MeshData,
				PCGExTopology::MeshOutputLabel,
				PCGExData::EStaging::Managed,
				Output.Tags);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
