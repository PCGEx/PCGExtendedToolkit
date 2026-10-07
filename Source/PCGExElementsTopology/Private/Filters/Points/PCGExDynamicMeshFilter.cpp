// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExDynamicMeshFilter.h"

#include "PCGExTopology.h"
#include "UDynamicMesh.h"
#include "Core/PCGExMT.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Math/OBB/PCGExOBBTests.h"
#include "Spatial/FastWinding.h"

#define LOCTEXT_NAMESPACE "PCGExDynamicMeshFilterDefinition"
#define PCGEX_NAMESPACE PCGExDynamicMeshFilterDefinition

#pragma region UPCGExDynamicMeshFilterFactory

bool UPCGExDynamicMeshFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	bExpansionConstant = !Config.NeedsBox() || Config.Expansion.CanSupportDataOnly();
	return true;
}

bool UPCGExDynamicMeshFilterFactory::DomainCheck()
{
	return !Config.NeedsBox() || Config.Expansion.CanSupportDataOnly();
}

bool UPCGExDynamicMeshFilterFactory::SupportsProxyEvaluation() const
{
	return bExpansionConstant;
}

bool UPCGExDynamicMeshFilterFactory::SupportsCollectionEvaluation() const
{
	return bOnlyUseDataDomain;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExDynamicMeshFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FDynamicMeshFilter>(this);
}

PCGExFactories::EPreparationResult UPCGExDynamicMeshFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGExTopology::Labels::SourceMeshLabel);

	CachedMeshes.Reset();
	for (const FPCGTaggedData& TaggedData : Inputs)
	{
		const UPCGDynamicMeshData* MeshData = Cast<UPCGDynamicMeshData>(TaggedData.Data);
		if (!MeshData || !MeshData->GetDynamicMesh())
		{
			continue;
		}

		bool bHasTriangles = false;
		MeshData->GetDynamicMesh()->ProcessMesh([&bHasTriangles](const UE::Geometry::FDynamicMesh3& Mesh)
		{
			bHasTriangles = Mesh.TriangleCount() > 0;
		});

		if (!bHasTriangles)
		{
			continue;
		}

		CachedMeshes.AddDefaulted_GetRef().Data = MeshData;
	}

	if (CachedMeshes.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing dynamic mesh data."))
		}
		return PCGExFactories::EPreparationResult::MissingData;
	}

	// Tree builds are the expensive part: one task per mesh. Bounds come from the built tree, so the
	// octree can only be assembled once every task has completed.
	PCGEX_ASYNC_GROUP_CHKD_RET(TaskManager, BuildMeshBVH, PCGExFactories::EPreparationResult::Fail)

	TWeakPtr<FPCGContextHandle> CtxHandle = InContext->GetWeakSelfHandle();

	BuildMeshBVH->OnCompleteCallback = [CtxHandle, this]()
	{
		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		FBox OctreeBounds(ForceInit);
		for (const PCGExPointFilter::FCachedDynamicMesh& Entry : CachedMeshes)
		{
			OctreeBounds += Entry.WorldBounds;
		}

		Octree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
		for (int32 i = 0; i < CachedMeshes.Num(); i++)
		{
			Octree->AddElement(PCGExOctree::FItem(i, CachedMeshes[i].WorldBounds));
		}
	};

	BuildMeshBVH->OnIterationCallback = [CtxHandle, this](const int32 Index, const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExDynamicMeshFilterFactory::BuildMeshBVH);

		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		PCGExPointFilter::FCachedDynamicMesh& Entry = CachedMeshes[Index];
		Entry.Data->GetDynamicMesh()->ProcessMesh([&Entry](const UE::Geometry::FDynamicMesh3& Mesh)
		{
			Entry.BVH.Spatial = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>();
			Entry.BVH.Spatial->SetMesh(&Mesh, true);
			Entry.BVH.FWNTree = MakeShared<UE::Geometry::TFastWindingTree<UE::Geometry::FDynamicMesh3>>(Entry.BVH.Spatial.Get(), true);
			Entry.WorldBounds = static_cast<FBox>(Entry.BVH.Spatial->GetBoundingBox());

			// Probe with the first valid vertex. The index space may have holes, so walk until one
			// is valid -- a range-for that breaks on its first element is -Wunreachable-code-loop-increment
			// under Clang -Werror.
			for (int32 VertexID = 0; VertexID < Mesh.MaxVertexID(); ++VertexID)
			{
				if (Mesh.IsVertex(VertexID))
				{
					Entry.ProbeVertex = Mesh.GetVertex(VertexID);
					break;
				}
			}
		});
	};

	BuildMeshBVH->StartIterations(CachedMeshes.Num(), 1);

	return Result;
}

void UPCGExDynamicMeshFilterFactory::BeginDestroy()
{
	CachedMeshes.Empty();
	Octree.Reset();
	Super::BeginDestroy();
}

#pragma endregion

#pragma region FDynamicMeshFilter

bool PCGExPointFilter::FDynamicMeshFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	CachedMeshes = &TypedFilterFactory->CachedMeshes;
	Octree = TypedFilterFactory->Octree.Get();
	if (!CachedMeshes || CachedMeshes->IsEmpty() || !Octree)
	{
		return false;
	}

	const FPCGExDynamicMeshFilterConfig& Cfg = TypedFilterFactory->Config;
	CheckType = Cfg.CheckType;
	BoundsSource = Cfg.BoundsSource;
	WindingIsoThreshold = Cfg.WindingIsoThreshold;
	bInvert = Cfg.bInvert;
	bNeedsBox = Cfg.NeedsBox();

	if (bNeedsBox)
	{
		if (!InitSettingValue(Expansion, Cfg.Expansion.GetValueSetting(PCGEX_QUIET_HANDLING), InPointDataFacade))
		{
			return false;
		}
	}

	return true;
}

bool PCGExPointFilter::FDynamicMeshFilter::TestQuery(const FMeshQuery& Query, const PCGExOctree::FItemOctree& InOctree, const TArray<FCachedDynamicMesh>& InMeshes, const EPCGExMeshCheckType InCheckType, const double InWindingIsoThreshold, const bool bInInvert)
{
	// A position outside a mesh's bounding box sees the whole mesh within one half-space, so its winding
	// number is below 0.5 and it can never be inside. The AABB check is the cheap reject before the tree walk.
	auto PositionInside = [&](const FCachedDynamicMesh& Mesh) -> bool
	{
		return Mesh.WorldBounds.IsInsideOrOn(Query.Position) && Mesh.BVH.FWNTree->IsInside(Query.Position, InWindingIsoThreshold);
	};

	// True when the mesh surface passes through the box, or the mesh sits entirely within it.
	auto SurfaceCrossesBox = [&](const FCachedDynamicMesh& Mesh) -> bool
	{
		if (Query.WorldBounds.IsInside(Mesh.WorldBounds) && PCGExMath::OBB::PointInside(Query.Box, Mesh.ProbeVertex))
		{
			return true;
		}

		const UE::Geometry::FDynamicMesh3* MeshPtr = Mesh.BVH.Spatial->GetMesh();
		const UE::Geometry::FAxisAlignedBox3d QueryBounds(Query.WorldBounds);
		bool bHit = false;

		UE::Geometry::FDynamicMeshAABBTree3::FTreeTraversal Traversal;
		Traversal.NextBoxF = [&](const UE::Geometry::FAxisAlignedBox3d& NodeBox, int32) { return !bHit && NodeBox.Intersects(QueryBounds); };
		Traversal.NextTriangleF = [&](const int32 TriangleID)
		{
			if (bHit) { return; }
			FVector A, B, C;
			MeshPtr->GetTriVertices(TriangleID, A, B, C);
			bHit = PCGExMath::OBB::TriangleOverlap(Query.Box, A, B, C);
		};

		Mesh.BVH.Spatial->DoTraversal(Traversal);
		return bHit;
	};

	bool bFoundInside = false;
	bool bMatched = false;

	// WorldBounds is the position alone when no box was built, which makes 'Is Inside' a zero-extent query.
	InOctree.FindElementsWithBoundsTest(
		FBoxCenterAndExtent(Query.WorldBounds),
		[&](const PCGExOctree::FItem& Item)
		{
			if (bMatched)
			{
				return;
			}

			const FCachedDynamicMesh& Mesh = InMeshes[Item.Index];
			const bool bInside = PositionInside(Mesh);

			switch (InCheckType)
			{
			case EPCGExMeshCheckType::IsInside:
				bMatched = bInside;
				break;

			case EPCGExMeshCheckType::Intersects:
				bMatched = !bInside && SurfaceCrossesBox(Mesh);
				break;

			case EPCGExMeshCheckType::IsInsideOrIntersects:
				bMatched = bInside || SurfaceCrossesBox(Mesh);
				break;

			case EPCGExMeshCheckType::IsOutsideOrIntersects:
				if (bInside)
				{
					bFoundInside = true;
				}
				else
				{
					bMatched = SurfaceCrossesBox(Mesh);
				}
				break;

			case EPCGExMeshCheckType::IsFullyInside:
				bMatched = bInside && !SurfaceCrossesBox(Mesh);
				break;

			default:
				checkNoEntry();
				break;
			}
		});

	if (bMatched)
	{
		return !bInInvert;
	}

	// No mesh matched
	if (InCheckType == EPCGExMeshCheckType::IsOutsideOrIntersects)
	{
		return bFoundInside ? bInInvert : !bInInvert;
	}

	return bInInvert;
}

bool PCGExPointFilter::FDynamicMeshFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	FMeshQuery Query;
	Query.Build(Point, BoundsSource, bNeedsBox ? Expansion->Read(0) : 0.0, bNeedsBox);
	return TestQuery(Query, *Octree, *CachedMeshes, CheckType, WindingIsoThreshold, bInvert);
}

bool PCGExPointFilter::FDynamicMeshFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
	FMeshQuery Query;
	Query.Build(Point, BoundsSource, bNeedsBox ? Expansion->Read(PointIndex) : 0.0, bNeedsBox);
	return TestQuery(Query, *Octree, *CachedMeshes, CheckType, WindingIsoThreshold, bInvert);
}

bool PCGExPointFilter::FDynamicMeshFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	// Self-contained collection-level eval (see IFilter::Test(IO, ParentCollection) contract): the mesh
	// octree and trees are factory data built in Prepare(), independent of the per-point Init(), and the
	// expansion shorthand is read per-IO. Stays correct even when the per-point Init() failed.
	const TArray<FCachedDynamicMesh>& Meshes = TypedFilterFactory->CachedMeshes;
	const PCGExOctree::FItemOctree* MeshOctree = TypedFilterFactory->Octree.Get();
	if (Meshes.IsEmpty() || !MeshOctree)
	{
		PCGEX_QUIET_HANDLING_RET
	}

	const FPCGExDynamicMeshFilterConfig& Cfg = TypedFilterFactory->Config;
	const bool bWithBox = Cfg.NeedsBox();

	double ExpansionValue = 0;
	if (bWithBox && !Cfg.Expansion.TryReadDataValue(IO, ExpansionValue, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	PCGExData::FProxyPoint ProxyPoint;
	IO->GetDataAsProxyPoint(ProxyPoint);

	FMeshQuery Query;
	Query.Build(ProxyPoint, Cfg.BoundsSource, ExpansionValue, bWithBox);
	return TestQuery(Query, *MeshOctree, Meshes, Cfg.CheckType, Cfg.WindingIsoThreshold, Cfg.bInvert);
}

#pragma endregion

#pragma region UPCGExDynamicMeshFilterProviderSettings

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExDynamicMeshFilterProviderSettings::GetPreconfiguredInfo() const
{
	const TSet<EPCGExMeshCheckType> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExMeshCheckType>(ValuesToSkip, FTEXT("{0} (Dynamic Mesh)"));
}
#endif

void UPCGExDynamicMeshFilterProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExMeshCheckType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Config.CheckType = static_cast<EPCGExMeshCheckType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

TArray<FPCGPinProperties> UPCGExDynamicMeshFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_MESH(PCGExTopology::Labels::SourceMeshLabel, TEXT("Dynamic mesh data to test points against"), Required)
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(DynamicMesh)

#if WITH_EDITOR
FString UPCGExDynamicMeshFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("");
	switch (Config.CheckType)
	{
	default:
	case EPCGExMeshCheckType::IsInside:
		DisplayName = TEXT("Is Inside");
		break;
	case EPCGExMeshCheckType::Intersects:
		DisplayName = TEXT("Intersects");
		break;
	case EPCGExMeshCheckType::IsInsideOrIntersects:
		DisplayName = TEXT("Is Inside or Intersects");
		break;
	case EPCGExMeshCheckType::IsOutsideOrIntersects:
		DisplayName = TEXT("Is Outside or Intersects");
		break;
	case EPCGExMeshCheckType::IsFullyInside:
		DisplayName = TEXT("Is Fully Inside");
		break;
	}

	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
