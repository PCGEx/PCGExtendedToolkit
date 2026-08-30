// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClipper2Volume.h"

#include "Clipper2Lib/clipper.h"
#include "Core/PCGExClipper2Decomposition.h"

#include "Model.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Polys.h"
#include "Engine/TriggerVolume.h"
#include "GameFramework/Volume.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"

#if WITH_EDITOR
// Box render mesh for the persisted Primitive-mode collider (see BuildBoxRenderMesh / SpawnStagedVolumes).
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGManagedResource.h"
#include "PCGPin.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "Core/PCGExMT.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGVolumeData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/World.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Math/PCGExProjectionDetails.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2VolumeElement"
#define PCGEX_NAMESPACE Clipper2Volume

// Plain build data produced off-thread in Process(Group), consumed on the game thread in OutputWork.
struct FPCGExVolumeSpec
{
	TArray<FKConvexElem> ConvexElems;
	TArray<FPoly> BrushPolys;
	FBox LocalBounds = FBox(ForceInit); // actor-local AABB of the prism set; sizes the Primitive-mode collider's bounds + its (hidden) placeholder box render mesh.
	FTransform ActorTransform = FTransform::Identity;
	int32 GroupIndex = 0;
	int32 SourceFacadeIndex = INDEX_NONE; // AllOpData index of the group's representative path (for @Data forwarding).
};

// File-local helpers in a named namespace (Unity-build safe). Shared triangulation/decomposition lives in
// PCGExClipper2Decomposition; only the volume-specific prism tessellation is here.
namespace PCGExClipper2Volume
{
	// Convex pieces as simple collision, shared by both output modes. CTF_UseSimpleAsComplex makes the hulls answer
	// every query since there's no complex/per-tri mesh.
	void ConfigureConvexBodySetup(UBodySetup* BodySetup, TArray<FKConvexElem>&& ConvexElems)
	{
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BodySetup->bGenerateNonMirroredCollision = true;
		BodySetup->bGenerateMirroredCollision = false;
		BodySetup->AggGeom.ConvexElems = MoveTemp(ConvexElems);
		BodySetup->CreatePhysicsMeshes();
	}

#if WITH_EDITOR
	// A persisted Primitive-mode collider gets cooked, and a UStaticMesh with no MeshDescription fails the cook
	// ("Bad MeshDescription"). An EMPTY MeshDescription won't do either: FMeshDescriptionBulkData::SaveMeshDescription
	// drops empty descriptions, so it reads back invalid at cook. Give the mesh a real box matching the AABB -- the
	// component hides it (the convex BodySetup is the real collision), and it yields correct mesh bounds for free.
	void BuildBoxRenderMesh(UStaticMesh* Mesh, const FBox& Bounds)
	{
		Mesh->SetNumSourceModels(1);
		FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
		if (!MeshDesc)
		{
			return;
		}

		// Hidden placeholder: no lightmap UVs needed (and it avoids a degenerate-UV warning from the zero UVs).
		Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;

		MeshDesc->SetNumUVChannels(1);
		FStaticMeshAttributes Attributes(*MeshDesc);
		Attributes.Register();

		// One material slot so the single section maps to a valid material index (no missing-slot build warning).
		const FName SlotName(TEXT("Collision"));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, SlotName));

		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TPolygonGroupAttributesRef<FName> PolyGroupSlots = Attributes.GetPolygonGroupMaterialSlotNames();

		const FPolygonGroupID PolyGroup = MeshDesc->CreatePolygonGroup();
		PolyGroupSlots[PolyGroup] = SlotName;

		const FVector Min = Bounds.Min;
		const FVector Max = Bounds.Max;
		const FVector Corners[8] = {
			FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z),
			FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z),
			FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z),
			FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z)
		};

		FVertexID Vertices[8];
		for (int32 i = 0; i < 8; ++i)
		{
			Vertices[i] = MeshDesc->CreateVertex();
			Positions[Vertices[i]] = FVector3f(Corners[i]);
		}

		// 6 quads -> 12 triangles. Winding/normals are irrelevant (the component hides this mesh): it exists only
		// to give the cook valid render data + correct bounds; the build recomputes normals.
		const int32 Quads[6][4] = {
			{0, 1, 2, 3}, {7, 6, 5, 4}, {4, 5, 1, 0}, {5, 6, 2, 1}, {6, 7, 3, 2}, {7, 4, 0, 3}
		};
		for (int32 q = 0; q < 6; ++q)
		{
			const FVertexInstanceID I0 = MeshDesc->CreateVertexInstance(Vertices[Quads[q][0]]);
			const FVertexInstanceID I1 = MeshDesc->CreateVertexInstance(Vertices[Quads[q][1]]);
			const FVertexInstanceID I2 = MeshDesc->CreateVertexInstance(Vertices[Quads[q][2]]);
			const FVertexInstanceID I3 = MeshDesc->CreateVertexInstance(Vertices[Quads[q][3]]);
			const FVertexInstanceID TriA[3] = {I0, I1, I2};
			const FVertexInstanceID TriB[3] = {I0, I2, I3};
			MeshDesc->CreateTriangle(PolyGroup, TriA);
			MeshDesc->CreateTriangle(PolyGroup, TriB);
		}

		Mesh->CommitMeshDescription(0);

		// Build render data now: a runtime-created mesh doesn't build until asked, so without this the asset (and
		// the spawned component) reads 0 tris/verts until the cook builds it. Silent to avoid message-log spam.
		// Build() leaves the convex BodySetup untouched (only BuildFromMeshDescriptions mutates AggGeom).
		Mesh->Build(true);
	}
#endif

	// Build the side/cap polys of a vertical prism (local space) for editor wireframe + bounds.
	void AddPrismPolys(const TArray<FVector>& Bottoms, const TArray<FVector>& Tops, TArray<FPoly>& OutPolys)
	{
		const int32 N = Bottoms.Num();
		if (N < 3)
		{
			return;
		}

		// Bottom cap (reversed winding so it faces down/outward).
		{
			FPoly Poly;
			Poly.Init();
			for (int32 i = N - 1; i >= 0; --i)
			{
				Poly.Vertices.Add(FVector3f(Bottoms[i]));
			}
			Poly.Base = FVector3f(Bottoms[0]);
			if (Poly.CalcNormal(true) == 0)
			{
				OutPolys.Add(Poly);
			}
		}
		{
			FPoly Poly;
			Poly.Init();
			for (int32 i = 0; i < N; ++i)
			{
				Poly.Vertices.Add(FVector3f(Tops[i]));
			}
			Poly.Base = FVector3f(Tops[0]);
			if (Poly.CalcNormal(true) == 0)
			{
				OutPolys.Add(Poly);
			}
		}
		for (int32 i = 0; i < N; ++i)
		{
			const int32 J = (i + 1) % N;
			FPoly Poly;
			Poly.Init();
			Poly.Vertices.Add(FVector3f(Bottoms[i]));
			Poly.Vertices.Add(FVector3f(Bottoms[J]));
			Poly.Vertices.Add(FVector3f(Tops[J]));
			Poly.Vertices.Add(FVector3f(Tops[i]));
			Poly.Base = FVector3f(Bottoms[i]);
			if (Poly.CalcNormal(true) == 0)
			{
				OutPolys.Add(Poly);
			}
		}
	}
}

#pragma region UPCGExClipper2VolumeSettings

UPCGExClipper2VolumeSettings::UPCGExClipper2VolumeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VolumeClass = ATriggerVolume::StaticClass();
	PrimitiveActorClass = AActor::StaticClass();
	CollisionBody.SetCollisionProfileName(FName("BlockAll"));

	// Default to Auto grouping: an outer footprint + its nested rings form one volume (rings become holes),
	// unrelated footprints stay separate. Dropdown exposed so users can pick Separate or Merged.
	bExposeGroupingPolicy = true;
	MainInputGroupingPolicy = EPCGExGroupingPolicy::Auto;

	// Hide inherited path-output-only parameters (blending, carry-over, open-path, simplify).
	bExposePathOutputProperties = false;
}

FPCGExGeo2DProjectionDetails UPCGExClipper2VolumeSettings::GetProjectionDetails() const
{
	return ProjectionDetails;
}

TArray<FPCGPinProperties> UPCGExClipper2VolumeSettings::OutputPinProperties() const
{
	// One spatial data per spawned actor. The actor reference and the source path's @Data attributes ride along in
	// the output's @Data domain, so downstream graphs can address/filter the outputs like the source paths.
	TArray<FPCGPinProperties> PinProperties;
	if (OutputMode == EPCGExClipper2VolumeOutputMode::Primitive)
	{
		PCGEX_PIN_PRIMITIVES(PCGPinConstants::DefaultOutputLabel, TEXT("Simple static-mesh colliders, one per spawned actor."), Normal)
	}
	else
	{
		PCGEX_PIN_VOLUMES(PCGPinConstants::DefaultOutputLabel, TEXT("Volume data created from spawned actors, one per spawned volume."), Normal)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(Clipper2Volume)

#pragma endregion

#pragma region FPCGExClipper2VolumeContext

void FPCGExClipper2VolumeContext::AddStagedVolume(const TSharedPtr<FPCGExVolumeSpec>& Spec)
{
	FScopeLock Lock(&StagedVolumesLock);
	StagedVolumes.Add(Spec);
}

void FPCGExClipper2VolumeContext::SpawnStagedVolumes()
{
	// Undo/redo can cancel the generation before this game-thread spawn runs. Bail rather than spawn actors into a
	// torn-down context -- the cancelled generation's output is discarded and any partial actors are cleaned up by PCG.
	if (IsWorkCancelled())
	{
		return;
	}

	// Backstop for the narrow race where a package save / GC begins after the output dispatch's check but before this
	// marshaled task runs (it can be pumped from SavePackage's render flush). Creating/finding UObjects is illegal
	// then; skip rather than crash. The common case is already deferred in FPCGExClipper2ProcessorElement::AdvanceWork.
	if (PCGExMT::IsObjectWorkBlocked())
	{
		return;
	}

	const UPCGExClipper2VolumeSettings* Settings = GetInputSettings<UPCGExClipper2VolumeSettings>();

	// Each spawned volume's actor reference is written into the output volume data's @Data domain under this name.
	const FName AttrName = Settings->ActorReferenceAttributeName.IsNone() ? FName("ActorReference") : Settings->ActorReferenceAttributeName;

	UPCGComponent* MutableComponent = GetMutableComponent();
	if (!MutableComponent)
	{
		return;
	}

	UWorld* World = MutableComponent->GetWorld();
	if (!World)
	{
		return;
	}

	const UPCGComponent* Comp = GetComponent();
	const bool bIsPreview = Comp && Comp->IsInPreviewMode();
	const bool bTransientSpawn = PCGHelpers::IsRuntimeOrPIE() || bIsPreview;

	ULevel* TargetLevel = nullptr;
	if (const AActor* CompOwner = Comp ? Comp->GetOwner() : nullptr)
	{
		TargetLevel = CompOwner->GetLevel();
	}

	// Outliner grouping anchor: the component's target actor. AttachToParent names the folder after it (InFolder)
	// or attaches to it (Attached); a null anchor makes the call a safe no-op.
	AActor* const TargetActor = GetTargetActor(nullptr);

	StagedVolumes.Sort([](const TSharedPtr<FPCGExVolumeSpec>& A, const TSharedPtr<FPCGExVolumeSpec>& B)
	{
		return A->GroupIndex < B->GroupIndex;
	});

	// Forward every @Data-domain attribute from the source path onto its volume's @Data domain. Handler is built
	// once per source and cached (rebuilding per volume would re-scan the source's identities needlessly).
	const FPCGExForwardDetails ForwardDetails(true);
	TMap<int32, TSharedPtr<PCGExData::FDataForwardHandler>> HandlersBySource;

	for (const TSharedPtr<FPCGExVolumeSpec>& Spec : StagedVolumes)
	{
		// Re-check each iteration: actor spawn / RegisterComponent can pump the game thread and let a fast undo
		// cancel the generation mid-spawn. Stop touching context state the moment that happens.
		if (IsWorkCancelled())
		{
			return;
		}
		if (!Spec || Spec->ConvexElems.IsEmpty())
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		if (TargetLevel)
		{
			SpawnParams.OverrideLevel = TargetLevel;
		}
		if (bTransientSpawn)
		{
			SpawnParams.ObjectFlags |= RF_Transient | RF_NonPIEDuplicateTransient;
		}

		// Host actor + the spatial data wrapping its collision; built per OutputMode below.
		AActor* SpawnedActor = nullptr;
		UPCGSpatialData* OutData = nullptr;

		if (Settings->OutputMode == EPCGExClipper2VolumeOutputMode::Primitive)
		{
			TSubclassOf<AActor> ActorClass = Settings->PrimitiveActorClass;
			if (!ActorClass)
			{
				ActorClass = AActor::StaticClass();
			}
			AActor* Actor = World->SpawnActor<AActor>(ActorClass, Spec->ActorTransform, SpawnParams);
			if (!Actor)
			{
				continue;
			}
			
			// Simple-collision static mesh: our convex pieces as simple geometry. With no complex/per-tri mesh,
			// CTF_UseSimpleAsComplex makes the hulls answer every query (incl. line traces). The component hides the
			// mesh (below), so its only render data is the placeholder box the cook needs -- the BodySetup is the
			// real collision.
			UStaticMesh* Mesh = NewObject<UStaticMesh>(Actor, NAME_None, bTransientSpawn ? RF_Transient : RF_NoFlags);
			Mesh->CreateBodySetup();
			PCGExClipper2Volume::ConfigureConvexBodySetup(Mesh->GetBodySetup(), MoveTemp(Spec->ConvexElems));

			// Bounds for UPCGPrimitiveData (cached at Initialize) + the voxel sampler. Valid at spawn time before any
			// render build; on the persisted path the cook's CalculateExtendedBounds recomputes the same AABB from the
			// box render mesh below.
			Mesh->SetExtendedBounds(FBoxSphereBounds(Spec->LocalBounds));

			bool bHasRenderMesh = false;
#if WITH_EDITOR
			if (!bTransientSpawn)
			{
				// Persisted mesh gets cooked: give it a real (hidden) box so the cook builds valid render data
				// instead of failing with "Bad MeshDescription". See PCGExClipper2Volume::BuildBoxRenderMesh.
				PCGExClipper2Volume::BuildBoxRenderMesh(Mesh, Spec->LocalBounds);
				bHasRenderMesh = true;
			}
#endif
			if (!bHasRenderMesh)
			{
				// Render-data-less mesh (transient/runtime, never cooked): encode the AABB as bounds extensions so a
				// later CalculateExtendedBounds reproduces it instead of collapsing to a point.
				Mesh->SetNegativeBoundsExtension(-Spec->LocalBounds.Min);
				Mesh->SetPositiveBoundsExtension(Spec->LocalBounds.Max);
			}

			UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(Actor, NAME_None, bTransientSpawn ? RF_Transient : RF_NoFlags);
			const bool bActorHadRoot = Actor->GetRootComponent() != nullptr;
			if (bActorHadRoot)
			{
				MeshComp->SetupAttachment(Actor->GetRootComponent());
			}
			else
			{
				Actor->SetRootComponent(MeshComp);
			}
			MeshComp->SetStaticMesh(Mesh);

			// Collision-only volume: the placeholder render mesh must never draw. Set before registration so no
			// visible scene proxy is ever created; visibility doesn't affect collision.
			MeshComp->SetVisibility(false);

			// Apply the user's collision setup. bUseDefaultCollision must be off or UpdateCollisionFromStaticMesh
			// (run during registration) would overwrite it with the mesh asset's default profile.
			MeshComp->bUseDefaultCollision = false;
			MeshComp->BodyInstance.CopyBodyInstancePropertiesFrom(&Settings->CollisionBody);
			MeshComp->RegisterComponent();
			Actor->AddInstanceComponent(MeshComp);

			// A class with no native root (the default plain AActor) can't be placed by SpawnActor -- there's no root
			// to receive the transform -- so it lands at the origin. Position it now that our component is the root.
			// A class that brought its own root was already placed by SpawnActor, so leave its transform alone.
			if (!bActorHadRoot)
			{
				Actor->SetActorTransform(Spec->ActorTransform);
			}
			MeshComp->RecreatePhysicsState();

			// A baked footprint collider must never simulate, regardless of what the user's CollisionBody copied.
			MeshComp->SetSimulatePhysics(false);

			// UPCGPrimitiveData samples through OverlapComponent, so the body must already be registered + cooked
			// (Initialize caches the component's current bounds).
			UPCGPrimitiveData* PrimitiveData = ManagedObjects->New<UPCGPrimitiveData>();
			PrimitiveData->Initialize(MeshComp);

			SpawnedActor = Actor;
			OutData = PrimitiveData;
		}
		else
		{
			AVolume* Volume = World->SpawnActor<AVolume>(Settings->VolumeClass, Spec->ActorTransform, SpawnParams);
			if (!Volume)
			{
				continue;
			}

			UBrushComponent* BrushComp = Volume->GetBrushComponent();
			if (!BrushComp)
			{
				Volume->Destroy();
				continue;
			}

			// Collision body from our convex pieces (the actual trigger geometry -- no BSP).
			UBodySetup* BodySetup = NewObject<UBodySetup>(BrushComp);
			PCGExClipper2Volume::ConfigureConvexBodySetup(BodySetup, MoveTemp(Spec->ConvexElems));
			BrushComp->BrushBodySetup = BodySetup;

#if WITH_EDITOR
			// Brush model for editor wireframe + render/selection bounds (collision is independent of this).
			UModel* Model = NewObject<UModel>(BrushComp);
			Model->Initialize(nullptr, true);
			Model->Polys = NewObject<UPolys>(Model);
			Model->Polys->Element = MoveTemp(Spec->BrushPolys);
			Model->BuildBound();
			BrushComp->Brush = Model;
#endif

			if (Settings->bOverrideCollisionProfile)
			{
				BrushComp->SetCollisionProfileName(Settings->CollisionProfileName);
			}

			BrushComp->RecreatePhysicsState();
			BrushComp->MarkRenderStateDirty();

			UPCGVolumeData* VolumeData = ManagedObjects->New<UPCGVolumeData>();
			VolumeData->Initialize(Volume);

			SpawnedActor = Volume;
			OutData = VolumeData;
		}

		if (!SpawnedActor || !OutData)
		{
			continue;
		}
		
		AddNotifyActor(SpawnedActor);

		// Group the spawned actor under its Outliner folder (or attach it) instead of leaving it loose at the root.
		PCGHelpers::AttachToParent(SpawnedActor, TargetActor, Settings->AttachOptions, this);

		// Create + register the managed resource lazily on first spawn so partial work is still tracked.
		if (!ManagedActors)
		{
			ManagedActors = NewObject<UPCGManagedActors>(MutableComponent);
			ManagedActors->SetCrc(DependenciesCrc);
#if WITH_EDITOR
			ManagedActors->SetIsPreview(bIsPreview);
#endif
			MutableComponent->AddToManagedResources(ManagedActors);
		}

		PCGExCollections::FinalizeSpawnedActor(SpawnedActor, ManagedActors, bTransientSpawn);

		// One spatial data per spawned actor, emitted on the default output pin.
		FPCGTaggedData& OutTagged = OutputData.TaggedData.Emplace_GetRef();
		OutTagged.Pin = PCGPinConstants::DefaultOutputLabel;
		OutTagged.Data = OutData;

		// Carry the source path's tags + @Data attributes onto the output so downstream graphs can address/filter
		// the spawned actors the same way they would the originating paths.
		const int32 SrcIdx = Spec->SourceFacadeIndex;
		if (AllOpData && AllOpData->Facades.IsValidIndex(SrcIdx))
		{
			AllOpData->Facades[SrcIdx]->Source->Tags->DumpTo(OutTagged.Tags);

			if (!HandlersBySource.Contains(SrcIdx))
			{
				TSharedPtr<PCGExData::FDataForwardHandler> NewHandler = ForwardDetails.TryGetHandler(AllOpData->Facades[SrcIdx], false);
				if (NewHandler)
				{
					NewHandler->ValidateIdentities([](const PCGExData::FAttributeIdentity& Identity)
					{
						return Identity.InDataDomain();
					});
				}
				HandlersBySource.Add(SrcIdx, NewHandler); // cache the result (incl. null) so each source resolves once
			}
			if (const TSharedPtr<PCGExData::FDataForwardHandler>& Handler = HandlersBySource.FindChecked(SrcIdx))
			{
				Handler->Forward(0, OutData->Metadata);
			}
		}

		// Written last so the node's own actor reference wins any @Data name collision with a forwarded attribute.
		PCGExData::Helpers::SetDataValue<FSoftObjectPath>(OutData, AttrName, FSoftObjectPath(SpawnedActor));
	}
	
	ExecuteOnNotifyActors(Settings->PostProcessFunctionNames);
}

void FPCGExClipper2VolumeContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	const UPCGExClipper2VolumeSettings* Settings = GetInputSettings<UPCGExClipper2VolumeSettings>();

	// Triangulate + dedup vertex pool + Hertel-Mehlhorn merge (shared with Clipper2 : Decompose).
	const PCGExClipper2Decomposition::FDecomposeParams Params = PCGExClipper2Decomposition::MakeParams(Settings);

	PCGExClipper2Decomposition::FDecomposeResult Decomposition;
	if (!PCGExClipper2Decomposition::TryDecomposeGroup(Group, AllOpData, Params, Decomposition))
	{
		if (!Settings->bQuietTriangulationWarnings)
		{
			const FText WarningText = PCGExClipper2Decomposition::DescribeDecomposeFailure(
				Decomposition, LOCTEXT("VolumeSubject", "volume"), Settings->MaxConvexPieces);
			if (!WarningText.IsEmpty())
			{
				PCGE_LOG_C(Warning, GraphAndLog, this, WarningText);
			}
		}
		return;
	}

	// First subject's projection is the consistent frame for the whole volume.
	const int32 FrameSrcIdx = Group->SubjectIndices[0];
	const FPCGExGeo2DProjectionDetails& FrameProjection = AllOpData->Projections[FrameSrcIdx];

	const TArray<PCGExClipper2Decomposition::FFootprintVertex>& VertexPool = Decomposition.VertexPool;
	const TArray<TArray<int32>>& Pieces = Decomposition.Pieces;

	// Per-vertex extrusion height (volume-specific, not in the shared pool): read at each vertex's mapped
	// source point; non-source vertices contribute 0.
	TArray<double> Heights;
	Heights.SetNumUninitialized(VertexPool.Num());
	for (int32 i = 0; i < VertexPool.Num(); i++)
	{
		const PCGExClipper2Decomposition::FFootprintVertex& V = VertexPool[i];
		Heights[i] = (V.bHasSource && HeightValues.IsValidIndex(V.SourceIdx) && HeightValues[V.SourceIdx])
			? HeightValues[V.SourceIdx]->Read(V.SourcePointIdx)
			: 0.0;
	}

	// --- Global lowest projected Z (actor base) + footprint centroid (actor origin) ---
	double MinBaseZ = 0;
	bool bAnyBase = false;
	FVector2D Centroid = FVector2D::ZeroVector;
	for (const PCGExClipper2Decomposition::FFootprintVertex& V : VertexPool)
	{
		Centroid += V.Pos;
		if (V.bHasSource)
		{
			MinBaseZ = bAnyBase ? FMath::Min(MinBaseZ, V.ProjectedZ) : V.ProjectedZ;
			bAnyBase = true;
		}
	}
	Centroid /= VertexPool.Num();

	// --- Build one convex prism (+ wireframe polys) per convex piece, in actor-local space ---
	TSharedPtr<FPCGExVolumeSpec> Spec = MakeShared<FPCGExVolumeSpec>();
	Spec->GroupIndex = Group->GroupIndex;
	Spec->SourceFacadeIndex = FrameSrcIdx;
	Spec->ConvexElems.Reserve(Pieces.Num());

	for (const TArray<int32>& Piece : Pieces)
	{
		const int32 N = Piece.Num();
		if (N < 3)
		{
			continue;
		}

		// Extrusion height = max of the piece's point heights; floor = piece's own min/max/avg Z (unless Flat).
		// A flat-floored prism stays convex at any base Z.
		double TopHeight = 0;
		double PieceBaseZ = MinBaseZ;
		{
			double SumZ = 0;
			double MinZ = 0;
			double MaxZ = 0;
			int32 SourceCount = 0;
			for (const int32 Idx : Piece)
			{
				const PCGExClipper2Decomposition::FFootprintVertex& V = VertexPool[Idx];
				TopHeight = FMath::Max(TopHeight, Heights[Idx]);
				if (!V.bHasSource)
				{
					continue;
				}
				MinZ = SourceCount == 0 ? V.ProjectedZ : FMath::Min(MinZ, V.ProjectedZ);
				MaxZ = SourceCount == 0 ? V.ProjectedZ : FMath::Max(MaxZ, V.ProjectedZ);
				SumZ += V.ProjectedZ;
				++SourceCount;
			}

			if (SourceCount > 0)
			{
				switch (Settings->BaseMode)
				{
				case EPCGExVolumeBaseMode::Min:
					PieceBaseZ = MinZ;
					break;
				case EPCGExVolumeBaseMode::Max:
					PieceBaseZ = MaxZ;
					break;
				case EPCGExVolumeBaseMode::Average:
					PieceBaseZ = SumZ / static_cast<double>(SourceCount);
					break;
				default:
					break; // Flat -> keep global base plane
				}
			}
		}
		TopHeight = FMath::Max(TopHeight, Settings->MinThickness);
		const double BaseLocalZ = PieceBaseZ - MinBaseZ; // 0 in Flat mode

		// Volume mode needs the prism caps/sides for the editor brush model; Primitive mode needs only the AABB.
		// Build only what the active mode consumes.
		const bool bPrimitive = Settings->OutputMode == EPCGExClipper2VolumeOutputMode::Primitive;

		TArray<FVector> Bottoms;
		TArray<FVector> Tops;
		if (!bPrimitive)
		{
			Bottoms.Reserve(N);
			Tops.Reserve(N);
		}

		FKConvexElem Elem;
		Elem.VertexData.Reserve(N * 2);

		for (const int32 Idx : Piece)
		{
			const FVector2D& P = VertexPool[Idx].Pos;
			const FVector Bottom(P.X - Centroid.X, P.Y - Centroid.Y, BaseLocalZ);
			const FVector Top(P.X - Centroid.X, P.Y - Centroid.Y, BaseLocalZ + TopHeight);
			Elem.VertexData.Add(Bottom);
			Elem.VertexData.Add(Top);
			if (bPrimitive)
			{
				Spec->LocalBounds += Bottom;
				Spec->LocalBounds += Top;
			}
			else
			{
				Bottoms.Add(Bottom);
				Tops.Add(Top);
			}
		}

		Elem.UpdateElemBox();
		Spec->ConvexElems.Add(MoveTemp(Elem));

		if (!bPrimitive)
		{
			PCGExClipper2Volume::AddPrismPolys(Bottoms, Tops, Spec->BrushPolys);
		}
	}

	if (Spec->ConvexElems.IsEmpty())
	{
		return;
	}

	// Actor frame: rotation = projection orientation, origin = footprint centroid on the base plane.
	const FVector WorldOrigin = FrameProjection.Unproject(FVector(Centroid.X, Centroid.Y, MinBaseZ));
	Spec->ActorTransform = FTransform(FrameProjection.ProjectionQuat, WorldOrigin);

	AddStagedVolume(Spec);
}

#pragma endregion

#pragma region FPCGExClipper2VolumeElement

bool FPCGExClipper2VolumeElement::PostBoot(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Volume)

	// One height reader per source path (Facade->Idx aligns with AllOpData order).
	const int32 NumFacades = Context->AllOpData->Num();
	Context->HeightValues.SetNum(NumFacades);

	for (int32 i = 0; i < NumFacades; i++)
	{
		const TSharedPtr<PCGExData::FFacade>& Facade = Context->AllOpData->Facades[i];
		TSharedPtr<PCGExDetails::TSettingValue<double>> HeightSetting = Settings->Height.GetValueSetting();
		if (!HeightSetting->Init(Facade))
		{
			return false;
		}
		Context->HeightValues[i] = HeightSetting;
	}

	// CRC for the managed-resource record (no incremental reuse -- resources respawn each generation).
	GetDependenciesCrc(FPCGGetDependenciesCrcParams(&InContext->InputData, Settings, nullptr), Context->DependenciesCrc);

	return FPCGExClipper2ProcessorElement::PostBoot(InContext);
}

void FPCGExClipper2VolumeElement::OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Volume)

	// Don't begin spawning for a generation that's already been cancelled (e.g. by undo/redo). SpawnStagedVolumes
	// re-checks on the game thread, but skipping the marshal here avoids the round-trip entirely.
	if (Context->IsWorkCancelled())
	{
		return;
	}

	// Actor spawning, physics cooking and managed-resource registration must run on the game thread (inline if
	// already there -- no deadlock).
	PCGExMT::ExecuteOnMainThreadAndWait([Context]()
	{
		Context->SpawnStagedVolumes();
	});
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
