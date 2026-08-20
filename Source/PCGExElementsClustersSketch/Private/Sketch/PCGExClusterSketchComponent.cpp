// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneView.h"
#include "Sketch/PCGExClusterSketchPrint.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#endif

namespace PCGExClusterSketchComponent
{
	const FLinearColor BasisColor = FLinearColor(0.35f, 0.5f, 0.9f, 0.6f);

	/** Bounds fallback so an empty sketch still has a pickable, non-degenerate footprint. */
	constexpr double EmptyBoundsExtent = 50.0;

	class FSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FSceneProxy(const UPCGExClusterSketchComponent* Component)
			: FPrimitiveSceneProxy(Component)
			  , Snapshot(Component->GetVisualSnapshot())
		{
			bWillEverBeLit = false;
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			// Component space rides the live transform, so moving the actor moves the sketch without a
			// re-snapshot.
			const FMatrix& LocalToWorldMatrix = GetLocalToWorld();

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1 << ViewIndex)))
				{
					continue;
				}

				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				for (const FPCGExClusterSketchVisualLine& Line : Snapshot.Lines)
				{
					PDI->DrawLine(LocalToWorldMatrix.TransformPosition(Line.A), LocalToWorldMatrix.TransformPosition(Line.B), Line.Color, SDPG_World, Line.Thickness);
				}
				for (const FPCGExClusterSketchVisualPoint& Point : Snapshot.Points)
				{
					PDI->DrawPoint(LocalToWorldMatrix.TransformPosition(Point.Location), Point.Color, Point.Size, SDPG_World);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bVelocityRelevance = false;
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

		uint32 GetAllocatedSize() const
		{
			return FPrimitiveSceneProxy::GetAllocatedSize() + Snapshot.Lines.GetAllocatedSize() + Snapshot.Points.GetAllocatedSize();
		}

	private:
		FPCGExClusterSketchVisualSnapshot Snapshot;
	};
}

UPCGExClusterSketchComponent::UPCGExClusterSketchComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bHiddenInGame = true; // authoring aid, not gameplay geometry
	CastShadow = false;
}

const FPCGExClusterSketchModel& UPCGExClusterSketchComponent::GetModel() const
{
	return SketchAsset ? SketchAsset->Model : InlineModel;
}

const UPCGExClusterSnapProvider* UPCGExClusterSketchComponent::GetSnapProvider() const
{
	return SketchAsset ? SketchAsset->SnapProvider.Get() : InlineSnapProvider.Get();
}

TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> UPCGExClusterSketchComponent::GetDecorators() const
{
	// Explicit on both branches: the asset's array is reached mutably through TObjectPtr::operator->,
	// so a ternary would have to reconcile two different view constnesses.
	if (SketchAsset)
	{
		return TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>>(SketchAsset->Decorators);
	}
	return TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>>(InlineDecorators);
}

FPCGExClusterSketchModel* UPCGExClusterSketchComponent::GetMutableModel()
{
	// Read-only while an asset drives this component: authoring here would rewrite topology shared by
	// every other instance referencing it. Inline the asset first.
	return SketchAsset ? nullptr : &InlineModel;
}

bool UPCGExClusterSketchComponent::BuildBasis(FPCGExLatticeBasis& OutBasis) const
{
	const UPCGExClusterSnapProvider* Provider = GetSnapProvider();
	return Provider ? Provider->BuildBasis(OutBasis) : false;
}

void UPCGExClusterSketchComponent::CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const
{
	if (SketchAsset)
	{
		SketchAsset->CollectAssetDependencies(OutPaths);
		return;
	}
	if (InlineSnapProvider)
	{
		InlineSnapProvider->CollectAssetDependencies(OutPaths);
	}
	for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : InlineDecorators)
	{
		if (Decorator && Decorator->bEnabled)
		{
			Decorator->CollectAssetDependencies(OutPaths);
		}
	}
}

TSharedPtr<PCGExGraphs::FGraphBuilder> UPCGExClusterSketchComponent::Print(
	FPCGExContext* InContext,
	const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
	const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
	const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
	const FPCGExGraphBuilderDetails* InBuilderDetails,
	const bool bQuiet,
	TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled) const
{
	return PCGExSketch::PrintResolved(
		InContext, GetModel(), GetSnapProvider(), GetDecorators(),
		InVtxIO, InTaskManager, InPrintContext, InBuilderDetails, bQuiet, MoveTemp(OnCompiled));
}

const FPCGExSketchElementStyle& UPCGExClusterSketchComponent::ResolveVertexStyle() const
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	return EditState.bActive ? Style->EditVertexIdle : Style->PreviewVertex;
}

const FPCGExSketchElementStyle& UPCGExClusterSketchComponent::ResolveGhostStyle() const
{
	// Ghosts exist only while editing; preview never has crossings on offer.
	return UPCGExClusterSketchStyleSettings::Get()->EditVertexPhantom;
}

const FPCGExSketchElementStyle& UPCGExClusterSketchComponent::ResolveEdgeStyle() const
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	return EditState.bActive ? Style->EditEdge : Style->PreviewEdge;
}

// Coverage answers from what the mesh layer ACTUALLY built, never from whether a path is configured:
// a style path that does not resolve (its plugin disabled, the asset deleted) would otherwise claim
// coverage that the immediate-mode fallback then stands down for, and the element vanishes entirely.
bool UPCGExClusterSketchComponent::DrawsVerticesAsMesh() const
{
	return bShowSketch && VertexInstances && VertexInstances->GetStaticMesh() != nullptr;
}

bool UPCGExClusterSketchComponent::DrawsEdgesAsMesh() const
{
	return bShowSketch && EdgeInstances && EdgeInstances->GetStaticMesh() != nullptr;
}

bool UPCGExClusterSketchComponent::DrawsHoverAsMesh() const
{
	return bShowSketch && EditState.bActive && HoverOutlineInstances && HoverOutlineInstances->GetStaticMesh() != nullptr;
}

bool UPCGExClusterSketchComponent::DrawsGhostsAsMesh() const
{
	return bShowSketch && EditState.bActive && GhostInstances && GhostInstances->GetStaticMesh() != nullptr;
}

FLinearColor UPCGExClusterSketchComponent::ResolveVertexColor(const int32 ModelIndex) const
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	const FPCGExClusterSketchModel& Model = GetModel();

	if (!EditState.bActive)
	{
		return bOverrideColors ? VertexColor : Style->PreviewVertex.Color;
	}

	// Everything the delete gesture would take reads as doomed, not just what the cursor is on.
	if (DoomedVertices.Contains(ModelIndex))
	{
		return Style->DeleteIntentColor;
	}
	// Hover outranks selection, EXCEPT where the hover outline mesh is already saying "hovered" on its
	// own -- a selected vertex must not stop looking selected just because the cursor is over it. With
	// no outline configured, colour is hover's only voice and it takes precedence back.
	const bool bSelected = EditState.SelectedVertices.Contains(ModelIndex);
	if (EditState.HoveredVertex == ModelIndex && !(bSelected && DrawsHoverAsMesh()))
	{
		return Style->HoverColor;
	}
	if (bSelected)
	{
		return Style->SelectedColor;
	}
	if (Model.Vertices.IsValidIndex(ModelIndex))
	{
#if WITH_EDITORONLY_DATA
		if (Model.Vertices[ModelIndex].bSideEffect) { return Style->EditVertexSideEffectColor; }
#endif
		if (Model.Vertices[ModelIndex].bLatticeBound) { return Style->EditVertexBoundColor; }
	}
	return Style->EditVertexIdle.Color;
}

FLinearColor UPCGExClusterSketchComponent::ResolveEdgeColor(const int32 ModelIndex) const
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();

	if (!EditState.bActive)
	{
		return bOverrideColors ? EdgeColor : Style->PreviewEdge.Color;
	}
	if (EditState.HoveredEdge == ModelIndex)
	{
		return EditState.bDeleteIntent ? Style->DeleteIntentColor : Style->HoverColor;
	}
	if (DoomedEdges.Contains(ModelIndex))
	{
		return Style->DeleteIntentColor;
	}
	if (EditState.SelectedEdges.Contains(ModelIndex))
	{
		return Style->SelectedColor;
	}
	// An edge taking part in a crossing wears the phantom colour: the offer reads as one gesture with
	// the ghost sitting on it.
	if (EditState.CrossingEdges.Contains(ModelIndex))
	{
		return Style->EditVertexPhantom.Color;
	}
	return Style->EditEdge.Color;
}

double UPCGExClusterSketchComponent::ResolveVertexSizeScale(const int32 ModelIndex) const
{
	if (!EditState.bActive) { return 1.0; }
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	// Same precedence as the colour, so size and colour always describe the SAME state.
	const bool bSelected = EditState.SelectedVertices.Contains(ModelIndex);
	if (EditState.HoveredVertex == ModelIndex && !(bSelected && DrawsHoverAsMesh())) { return 1.0 + Style->HoverSizeBonus; }
	if (bSelected) { return 1.0 + Style->SelectedSizeBonus; }
	return 1.0;
}

double UPCGExClusterSketchComponent::ResolveEdgeSizeScale(const int32 ModelIndex) const
{
	if (!EditState.bActive) { return 1.0; }
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	if (EditState.HoveredEdge == ModelIndex) { return 1.0 + Style->HoverSizeBonus; }
	if (EditState.SelectedEdges.Contains(ModelIndex)) { return 1.0 + Style->SelectedSizeBonus; }
	return 1.0;
}

void UPCGExClusterSketchComponent::BuildVisualSnapshot()
{
	VisualSnapshot = FPCGExClusterSketchVisualSnapshot();

	if (!bShowSketch)
	{
		return;
	}

	// While a host edits this sketch it draws every immediate-mode element itself (it has the ghost and
	// affordance state this component cannot see). The mesh layer keeps drawing regardless -- and so do
	// BOUNDS, which drive culling and Focus Selected for as long as the actor exists.
	const bool bHostDrawsOverlay = EditState.bActive;

	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	const FPCGExClusterSketchModel& Model = GetModel();

	FPCGExLatticeBasis Basis;
	const bool bHasBasis = BuildBasis(Basis);
	const FPCGExLatticeBasis* BasisPtr = bHasBasis ? &Basis : nullptr;

	// Per-kind fallback: only a kind WITHOUT a mesh falls back to immediate mode, so the two paths
	// never draw the same element twice.
	const bool bVertexFallback = !bHostDrawsOverlay && !DrawsVerticesAsMesh();
	const bool bEdgeFallback = !bHostDrawsOverlay && !DrawsEdgesAsMesh();

	TArray<FVector> Locations;
	Locations.SetNumUninitialized(Model.Vertices.Num());
	for (int32 i = 0; i < Model.Vertices.Num(); ++i)
	{
		Locations[i] = FPCGExClusterSketchModel::ResolvedLocation(Model.Vertices[i], BasisPtr);
		VisualSnapshot.LocalBounds += Locations[i];
		if (bVertexFallback)
		{
			VisualSnapshot.Points.Add({Locations[i], ResolveVertexColor(i), static_cast<float>(Style->PreviewVertex.Size)});
		}
	}

	if (bEdgeFallback)
	{
		VisualSnapshot.Lines.Reserve(Model.Edges.Num());
		for (int32 e = 0; e < Model.Edges.Num(); ++e)
		{
			const FPCGExClusterSketchEdge& E = Model.Edges[e];
			// Dormant edges (endpoint not materialized) do not draw here -- the sketch editor is where
			// they are surfaced and repaired.
			if (!Locations.IsValidIndex(E.A) || !Locations.IsValidIndex(E.B))
			{
				continue;
			}
			VisualSnapshot.Lines.Add({Locations[E.A], Locations[E.B], ResolveEdgeColor(e), Style->PreviewEdge.FallbackThickness});
		}
	}

	if (bShowBasis && bHasBasis)
	{
		for (int32 k = 0; k < Basis.NumAxes; ++k)
		{
			const FVector AxisEnd = Basis.Origin + Basis.AxisVecs[k];
			if (!bHostDrawsOverlay)
			{
				VisualSnapshot.Lines.Add({Basis.Origin, AxisEnd, Style->BasisColor, 0.5f});
			}
			VisualSnapshot.LocalBounds += AxisEnd;
		}
		VisualSnapshot.LocalBounds += Basis.Origin;
	}
}

UInstancedStaticMeshComponent* UPCGExClusterSketchComponent::EnsureInstances(TObjectPtr<UInstancedStaticMeshComponent>& InOut, const TSoftObjectPtr<UStaticMesh>& InMesh, const TSoftObjectPtr<UMaterialInterface>& InMaterial)
{
	UStaticMesh* Mesh = InMesh.LoadSynchronous();
	if (!Mesh || !GetOwner())
	{
		if (InOut)
		{
			InOut->ClearInstances();
			InOut->SetStaticMesh(nullptr);
		}
		return nullptr;
	}

	if (!InOut)
	{
		// Transient by construction: a purely visual child must never be saved into the level, nor
		// survive a duplicate as a second copy.
		InOut = NewObject<UInstancedStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		InOut->SetupAttachment(this);
		InOut->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		InOut->SetGenerateOverlapEvents(false);
		InOut->CastShadow = false;
		InOut->bHiddenInGame = true;
		InOut->NumCustomDataFloats = 3; // RGB, so one material serves every sketch
		InOut->RegisterComponent();
	}

	// While a host is editing, the editor's own additive selection highlight fights the edit palette --
	// the same reason brush editing does not show it. UPrimitiveComponent::ShouldRenderSelected() is
	// gated on bSelectable, so clearing it suppresses highlight AND hit-testing in one move; the sketch
	// stays clickable in PREVIEW, where selecting its actor is exactly how you reach it.
	const bool bWantsSelectable = !EditState.bActive;
	if (InOut->bSelectable != bWantsSelectable)
	{
		InOut->bSelectable = bWantsSelectable;
		InOut->MarkRenderStateDirty();
	}

	if (InOut->GetStaticMesh() != Mesh)
	{
		InOut->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Material = InMaterial.LoadSynchronous())
	{
		InOut->SetMaterial(0, Material);
	}
	return InOut;
}

void UPCGExClusterSketchComponent::ResolveVertexLocations(TArray<FVector>& OutLocations) const
{
	const FPCGExClusterSketchModel& Model = GetModel();

	FPCGExLatticeBasis Basis;
	const bool bHasBasis = BuildBasis(Basis);
	const FPCGExLatticeBasis* BasisPtr = bHasBasis ? &Basis : nullptr;

	OutLocations.SetNumUninitialized(Model.Vertices.Num());
	for (int32 i = 0; i < Model.Vertices.Num(); ++i)
	{
		OutLocations[i] = FPCGExClusterSketchModel::ResolvedLocation(Model.Vertices[i], BasisPtr);
	}
}

void UPCGExClusterSketchComponent::RefreshDoomedSets()
{
	DoomedVertices.Reset();
	DoomedEdges.Reset();
	if (!EditState.bActive || !EditState.bDeleteIntent || EditState.HoveredVertex == INDEX_NONE)
	{
		return;
	}
	GetModel().GatherVertexRemovalCascade(EditState.HoveredVertex, DoomedVertices, DoomedEdges);
}

void UPCGExClusterSketchComponent::RebuildInstances()
{
	RefreshDoomedSets();

	const FPCGExSketchElementStyle& VertexStyle = ResolveVertexStyle();
	const FPCGExSketchElementStyle& EdgeStyle = ResolveEdgeStyle();

	UInstancedStaticMeshComponent* VertexISM = EnsureInstances(VertexInstances, VertexStyle.Mesh, VertexStyle.Material);
	UInstancedStaticMeshComponent* EdgeISM = EnsureInstances(EdgeInstances, EdgeStyle.Mesh, EdgeStyle.Material);

	VertexInstanceToModel.Reset();
	EdgeInstanceToModel.Reset();

	const FPCGExClusterSketchModel& Model = GetModel();

	TArray<FVector> Locations;
	ResolveVertexLocations(Locations);

	if (VertexISM) { VertexISM->ClearInstances(); }
	if (EdgeISM) { EdgeISM->ClearInstances(); }

	if (VertexISM && bShowSketch)
	{
		const FBoxSphereBounds MeshBounds = VertexISM->GetStaticMesh()->GetBounds();
		for (int32 i = 0; i < Locations.Num(); ++i)
		{
			const int32 Index = VertexISM->AddInstance(
				PCGExSketchStyle::MakePointInstanceTransform(MeshBounds, Locations[i], VertexStyle.Size * ResolveVertexSizeScale(i)));
			VertexInstanceToModel.Add(i);

			const FLinearColor Color = ResolveVertexColor(i);
			VertexISM->SetCustomDataValue(Index, 0, Color.R);
			VertexISM->SetCustomDataValue(Index, 1, Color.G);
			VertexISM->SetCustomDataValue(Index, 2, Color.B, true);
		}
	}

	if (EdgeISM && bShowSketch)
	{
		const FBoxSphereBounds MeshBounds = EdgeISM->GetStaticMesh()->GetBounds();
		for (int32 e = 0; e < Model.Edges.Num(); ++e)
		{
			const FPCGExClusterSketchEdge& E = Model.Edges[e];
			if (!Locations.IsValidIndex(E.A) || !Locations.IsValidIndex(E.B))
			{
				continue;
			}
			const FVector A = Locations[E.A];
			const FVector B = Locations[E.B];
			if (FVector::DistSquared(A, B) <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}
			const int32 Index = EdgeISM->AddInstance(
				PCGExSketchStyle::MakeSegmentInstanceTransform(MeshBounds, EdgeStyle.LengthAxis, A, B, EdgeStyle.Size * ResolveEdgeSizeScale(e)));
			EdgeInstanceToModel.Add(e);

			const FLinearColor Color = ResolveEdgeColor(e);
			EdgeISM->SetCustomDataValue(Index, 0, Color.R);
			EdgeISM->SetCustomDataValue(Index, 1, Color.G);
			EdgeISM->SetCustomDataValue(Index, 2, Color.B, true);
		}
	}

	RefreshGhosts();
	RefreshHoverOutline();
}

void UPCGExClusterSketchComponent::UpdateInstanceAppearance()
{
	RefreshDoomedSets();

	const FPCGExClusterSketchModel& Model = GetModel();

	// In-place updates cannot express a changed instance COUNT. Self-healing rather than trusting the
	// host to have rebuilt first: a mismatch means the model gained or lost vertices.
	const int32 ExpectedVertexInstances = (VertexInstances && VertexInstances->GetStaticMesh() && bShowSketch) ? Model.Vertices.Num() : 0;
	if (VertexInstanceToModel.Num() != ExpectedVertexInstances)
	{
		RebuildInstances();
		return;
	}

	TArray<FVector> Locations;
	ResolveVertexLocations(Locations);

	if (VertexInstances && VertexInstances->GetStaticMesh())
	{
		const FPCGExSketchElementStyle& VertexStyle = ResolveVertexStyle();
		const FBoxSphereBounds MeshBounds = VertexInstances->GetStaticMesh()->GetBounds();
		for (int32 i = 0; i < VertexInstanceToModel.Num(); ++i)
		{
			const int32 ModelIndex = VertexInstanceToModel[i];
			if (!Locations.IsValidIndex(ModelIndex)) { continue; }
			const bool bLast = i == VertexInstanceToModel.Num() - 1;
			VertexInstances->UpdateInstanceTransform(
				i, PCGExSketchStyle::MakePointInstanceTransform(MeshBounds, Locations[ModelIndex], VertexStyle.Size * ResolveVertexSizeScale(ModelIndex)), false, bLast);

			const FLinearColor Color = ResolveVertexColor(ModelIndex);
			VertexInstances->SetCustomDataValue(i, 0, Color.R);
			VertexInstances->SetCustomDataValue(i, 1, Color.G);
			VertexInstances->SetCustomDataValue(i, 2, Color.B, bLast);
		}
	}

	if (EdgeInstances && EdgeInstances->GetStaticMesh())
	{
		const FPCGExSketchElementStyle& EdgeStyle = ResolveEdgeStyle();
		const FBoxSphereBounds MeshBounds = EdgeInstances->GetStaticMesh()->GetBounds();
		for (int32 i = 0; i < EdgeInstanceToModel.Num(); ++i)
		{
			const int32 ModelIndex = EdgeInstanceToModel[i];
			if (!Model.Edges.IsValidIndex(ModelIndex)) { continue; }
			const FPCGExClusterSketchEdge& E = Model.Edges[ModelIndex];
			if (!Locations.IsValidIndex(E.A) || !Locations.IsValidIndex(E.B)) { continue; }

			const bool bLast = i == EdgeInstanceToModel.Num() - 1;
			EdgeInstances->UpdateInstanceTransform(
				i, PCGExSketchStyle::MakeSegmentInstanceTransform(MeshBounds, EdgeStyle.LengthAxis, Locations[E.A], Locations[E.B], EdgeStyle.Size * ResolveEdgeSizeScale(ModelIndex)), false, bLast);

			const FLinearColor Color = ResolveEdgeColor(ModelIndex);
			EdgeInstances->SetCustomDataValue(i, 0, Color.R);
			EdgeInstances->SetCustomDataValue(i, 1, Color.G);
			EdgeInstances->SetCustomDataValue(i, 2, Color.B, bLast);
		}
	}

	RefreshGhosts();
	RefreshHoverOutline();
}

void UPCGExClusterSketchComponent::RefreshGhosts()
{
	const FPCGExSketchElementStyle& GhostStyle = ResolveGhostStyle();
	UInstancedStaticMeshComponent* GhostISM = EnsureInstances(GhostInstances, GhostStyle.Mesh, GhostStyle.Material);
	if (!GhostISM)
	{
		return; // no ghost mesh configured: the host draws them in immediate mode instead
	}

	GhostISM->ClearInstances();
	if (!bShowSketch || !EditState.bActive)
	{
		return;
	}

	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	const FBoxSphereBounds MeshBounds = GhostISM->GetStaticMesh()->GetBounds();

	for (int32 c = 0; c < EditState.GhostLocations.Num(); ++c)
	{
		const bool bHovered = EditState.HoveredGhost == c;
		const double SizeScale = bHovered ? 1.0 + Style->HoverSizeBonus : 1.0;
		const int32 Index = GhostISM->AddInstance(
			PCGExSketchStyle::MakePointInstanceTransform(MeshBounds, EditState.GhostLocations[c], GhostStyle.Size * SizeScale));

		// A ghost is never selected -- it is not real yet -- so hover is its only state.
		const FLinearColor Color = bHovered ? Style->HoverColor : GhostStyle.Color;
		GhostISM->SetCustomDataValue(Index, 0, Color.R);
		GhostISM->SetCustomDataValue(Index, 1, Color.G);
		GhostISM->SetCustomDataValue(Index, 2, Color.B, true);
	}
}

void UPCGExClusterSketchComponent::RefreshHoverOutline()
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();

	UInstancedStaticMeshComponent* OutlineISM = EnsureInstances(HoverOutlineInstances, Style->HoverOutlineMesh, Style->HoverOutlineMaterial);
	if (!OutlineISM)
	{
		return;
	}

	OutlineISM->ClearInstances();

	// Only point-like elements get the outline: an inverted-normal shell only reads as a rim around a
	// compact shape, and a hovered EDGE already recolours along its whole length.
	if (!bShowSketch || !EditState.bActive)
	{
		return;
	}

	const bool bGhost = EditState.HoveredGhost != INDEX_NONE && EditState.GhostLocations.IsValidIndex(EditState.HoveredGhost);
	FVector Location = FVector::ZeroVector;
	double Radius = 0.0;

	if (bGhost)
	{
		Location = EditState.GhostLocations[EditState.HoveredGhost];
		Radius = ResolveGhostStyle().Size;
	}
	else
	{
		const FPCGExClusterSketchModel& Model = GetModel();
		if (!Model.Vertices.IsValidIndex(EditState.HoveredVertex))
		{
			return;
		}

		FPCGExLatticeBasis Basis;
		const bool bHasBasis = BuildBasis(Basis);
		Location = FPCGExClusterSketchModel::ResolvedLocation(Model.Vertices[EditState.HoveredVertex], bHasBasis ? &Basis : nullptr);
		Radius = ResolveVertexStyle().Size * ResolveVertexSizeScale(EditState.HoveredVertex);
	}

	const int32 Index = OutlineISM->AddInstance(
		PCGExSketchStyle::MakePointInstanceTransform(OutlineISM->GetStaticMesh()->GetBounds(), Location, Radius * Style->HoverOutlineScale));

	const FLinearColor Color = EditState.bDeleteIntent ? Style->DeleteIntentColor : Style->HoverColor;
	OutlineISM->SetCustomDataValue(Index, 0, Color.R);
	OutlineISM->SetCustomDataValue(Index, 1, Color.G);
	OutlineISM->SetCustomDataValue(Index, 2, Color.B, true);
}

void UPCGExClusterSketchComponent::RefreshSketchVisual()
{
	// Instances FIRST: the snapshot's per-kind fallback asks which kinds the mesh layer actually built.
	RebuildInstances();
	BuildVisualSnapshot();
	UpdateBounds();
	MarkRenderStateDirty();
}

void UPCGExClusterSketchComponent::SetEditState(const FPCGExClusterSketchEditState& InState)
{
	if (EditState == InState)
	{
		return; // pushed every frame by hosts; only real changes cost anything
	}

	// Entering or leaving edit swaps MESHES, which needs a full rebuild; hover and selection only tint,
	// and re-adding every instance for a tint would rebuild every transform for nothing.
	const bool bModeChanged = EditState.bActive != InState.bActive;
	EditState = InState;

	if (bModeChanged)
	{
		RefreshSketchVisual();
	}
	else
	{
		UpdateInstanceAppearance();
	}
}

FPrimitiveSceneProxy* UPCGExClusterSketchComponent::CreateSceneProxy()
{
	if (VisualSnapshot.IsEmpty())
	{
		return nullptr;
	}
	return new PCGExClusterSketchComponent::FSceneProxy(this);
}

FBoxSphereBounds UPCGExClusterSketchComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!VisualSnapshot.LocalBounds.IsValid)
	{
		return FBoxSphereBounds(FBox(FVector(-PCGExClusterSketchComponent::EmptyBoundsExtent), FVector(PCGExClusterSketchComponent::EmptyBoundsExtent))).TransformBy(LocalToWorld);
	}
	// Padded so points, whose screen size has no world extent, never frustum-cull at the edge.
	return FBoxSphereBounds(VisualSnapshot.LocalBounds.ExpandBy(PCGExClusterSketchComponent::EmptyBoundsExtent)).TransformBy(LocalToWorld);
}

void UPCGExClusterSketchComponent::OnRegister()
{
	// BEFORE Super: registration builds the render state, and CreateSceneProxy returns null on an
	// empty snapshot -- so a sketch would stay invisible until something else dirtied it.
	BuildVisualSnapshot();
	Super::OnRegister();

	// AFTER Super, in contrast: the mesh layer lives in CHILD components, which can only be created and
	// registered once this one is itself registered and has a world. Without this a sketch stayed
	// invisible on level load until something happened to call RefreshSketchVisual.
	RebuildInstances();

	// Re-run now that the mesh layer exists: the pre-Super pass could not know which kinds it covers.
	BuildVisualSnapshot();
	UpdateBounds();
	MarkRenderStateDirty();

#if WITH_EDITOR
	// Editing the REFERENCED asset repaints every instance of it -- the other half of the asset's
	// NotifyObjectChanged calls (and of the property editor's own broadcasts).
	if (!OnObjectPropertyChangedHandle.IsValid())
	{
		OnObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddWeakLambda(
			this, [this](UObject* Object, FPropertyChangedEvent&)
			{
				if (Object && SketchAsset && Object == SketchAsset)
				{
					RefreshSketchVisual();
				}
			});
	}

	// Style is read at BUILD time (a mesh swap has to reach existing instances), so a settings edit has
	// to push a repaint -- nothing else would notice it.
	if (!OnStyleChangedHandle.IsValid())
	{
		OnStyleChangedHandle = UPCGExClusterSketchStyleSettings::OnChanged().AddWeakLambda(this, [this] { RefreshSketchVisual(); });
	}
#endif
}

void UPCGExClusterSketchComponent::OnUnregister()
{
#if WITH_EDITOR
	if (OnObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPropertyChangedHandle);
		OnObjectPropertyChangedHandle.Reset();
	}
	if (OnStyleChangedHandle.IsValid())
	{
		UPCGExClusterSketchStyleSettings::OnChanged().Remove(OnStyleChangedHandle);
		OnStyleChangedHandle.Reset();
	}
#endif

	Super::OnUnregister();
}

void UPCGExClusterSketchComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	for (TObjectPtr<UInstancedStaticMeshComponent>* Slot : {&VertexInstances, &EdgeInstances, &GhostInstances, &HoverOutlineInstances})
	{
		if (UInstancedStaticMeshComponent* Child = Slot->Get())
		{
			Child->DestroyComponent();
		}
		*Slot = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UPCGExClusterSketchComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (MemberName == GET_MEMBER_NAME_CHECKED(UPCGExClusterSketchComponent, InlineModel))
	{
		// Hand-editing a vertex adopts it, and the coord/location pair must stay coherent -- the same
		// rule the asset host applies, or a typed-in location on a bound vertex is silently dead data.
		const int32 EditedVertex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FPCGExClusterSketchModel, Vertices));
		if (EditedVertex != INDEX_NONE)
		{
			InlineModel.MarkVertexAuthored(EditedVertex);
		}

		const FName LeafName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		const bool bCoordEdit = LeafName == GET_MEMBER_NAME_CHECKED(FPCGExClusterSketchVertex, LatticeCoord);
		EDITOR_SyncBoundVertices(!bCoordEdit);
	}

	// Any property here can move the drawing: payload, override, provider params, display settings.
	RefreshSketchVisual();
}

void UPCGExClusterSketchComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (!IsValidChecked(this) || IsTemplate())
	{
		return;
	}

	EDITOR_SyncBoundVertices(false);
	RefreshSketchVisual();
}

void UPCGExClusterSketchComponent::EDITOR_OnSnapProviderChanged()
{
	EDITOR_SyncBoundVertices(false);
	RefreshSketchVisual();
}

void UPCGExClusterSketchComponent::EDITOR_SyncBoundVertices(const bool bResnapFromLocation)
{
	// Only the INLINE payload is ours to correct: a referenced asset owns its own coherence.
	FPCGExClusterSketchModel* Mutable = GetMutableModel();
	FPCGExLatticeBasis Basis;
	if (!Mutable || !BuildBasis(Basis))
	{
		return;
	}
	Mutable->SyncBoundVertices(Basis, bResnapFromLocation);
}

void UPCGExClusterSketchComponent::InlineSketchAsset()
{
	if (!SketchAsset)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PCGExClusterSketchComponent", "InlineSketchAsset", "Inline Cluster Sketch Asset"));
	Modify();

	InlineModel = SketchAsset->Model;

	// Instanced subobjects must be OURS, not shared with the asset -- duplicate rather than assign.
	InlineSnapProvider = SketchAsset->SnapProvider
		                     ? DuplicateObject<UPCGExClusterSnapProvider>(SketchAsset->SnapProvider, this)
		                     : nullptr;

	InlineDecorators.Reset(SketchAsset->Decorators.Num());
	for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : SketchAsset->Decorators)
	{
		InlineDecorators.Add(Decorator ? DuplicateObject<UPCGExClusterSketchDecorator>(Decorator, this) : nullptr);
	}

	SketchAsset = nullptr;

	RefreshSketchVisual();
	PostEditChange();
	PCGExEditor::NotifyObjectChanged(this);
}
#endif

#if WITH_EDITOR
void UPCGExClusterSketchComponent::SaveToAsset()
{
	// The dialog + package creation live in the editor module; runtime reaches them through the bridge,
	// which is a plain null in cooked builds.
	if (!PCGExSketch::GSaveSketchAsAssetFn)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const FString DefaultName = Owner ? FString::Printf(TEXT("CS_%s"), *Owner->GetActorNameOrLabel()) : TEXT("CS_ClusterSketch");

	UPCGExClusterSketch* NewAsset = PCGExSketch::GSaveSketchAsAssetFn(GetModel(), GetSnapProvider(), GetDecorators(), DefaultName);
	if (!NewAsset)
	{
		return; // cancelled
	}

	// Referencing the new asset AFTER it is filled: the component becomes an instance of what it just
	// externalized, and its inline payload is preserved (read-only) behind the reference.
	const FScopedTransaction Transaction(NSLOCTEXT("PCGExClusterSketchComponent", "SaveToAsset", "Save Cluster Sketch To Asset"));
	Modify();
	SketchAsset = NewAsset;

	RefreshSketchVisual();
	PostEditChange();
	PCGExEditor::NotifyObjectChanged(this);
}
#endif
