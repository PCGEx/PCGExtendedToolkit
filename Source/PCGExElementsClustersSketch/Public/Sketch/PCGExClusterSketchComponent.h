// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchStyle.h"

#include "PCGExClusterSketchComponent.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** One drawn segment, both endpoints in COMPONENT space (rides the component transform live). */
struct FPCGExClusterSketchVisualLine
{
	FVector A = FVector::ZeroVector;
	FVector B = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Thickness = 1.0f;
};

/** One drawn vertex, in COMPONENT space. */
struct FPCGExClusterSketchVisualPoint
{
	FVector Location = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Size = 8.0f;
};

/** Game-thread snapshot handed to the scene proxy: the resolved sketch, flattened to draw primitives
 *  so the render thread never touches the model (which the editor mutates freely). */
struct FPCGExClusterSketchVisualSnapshot
{
	TArray<FPCGExClusterSketchVisualLine> Lines;
	TArray<FPCGExClusterSketchVisualPoint> Points;

	/** Component-space bounds of everything above; drives CalcBounds. */
	FBox LocalBounds = FBox(ForceInit);

	bool IsEmpty() const { return Lines.IsEmpty() && Points.IsEmpty(); }
};

/**
 * What an editing host is currently showing on a sketch. Pushed by the host every frame; the component
 * only rebuilds when it actually changes.
 */
struct FPCGExClusterSketchEditState
{
	/** True while a host is editing this sketch: the mesh layer switches to EDIT colours and the
	 *  component stops drawing its own immediate-mode fallback (the host draws state chrome instead). */
	bool bActive = false;

	TSet<int32> SelectedVertices;
	TSet<int32> SelectedEdges;

	/** Hovered element, so the mesh layer can express hover itself rather than leaving it to an
	 *  immediate-mode overlay. INDEX_NONE when nothing is hovered. */
	int32 HoveredVertex = INDEX_NONE;
	int32 HoveredEdge = INDEX_NONE;

	/** Host's delete modifier is held: hover reads as "this goes away". */
	bool bDeleteIntent = false;

	/** Hypothetical vertices a crossing offers -- unmaterialized, so they draw in the PHANTOM style.
	 *  Clicking one materializes it (host-side); the component only shows them. */
	TArray<FVector> GhostLocations;
	int32 HoveredGhost = INDEX_NONE;

	/** Edges taking part in a crossing: legitimate geometry, tinted to advertise the offer. */
	TSet<int32> CrossingEdges;

	/** Host's geometry revision. A drag moves vertices every frame without a completed-operation
	 *  notify, so without this the mesh layer would only catch up when some UNRELATED state field
	 *  happened to change -- a graph frozen mid-drag, snapping into place on release. */
	int32 ModelRevision = INDEX_NONE;

	static bool SetsEqual(const TSet<int32>& A, const TSet<int32>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const int32 Value : A)
		{
			if (!B.Contains(Value))
			{
				return false;
			}
		}
		return true;
	}

	bool operator==(const FPCGExClusterSketchEditState& Other) const
	{
		if (bActive != Other.bActive
			|| ModelRevision != Other.ModelRevision
			|| HoveredVertex != Other.HoveredVertex
			|| HoveredEdge != Other.HoveredEdge
			|| HoveredGhost != Other.HoveredGhost
			|| bDeleteIntent != Other.bDeleteIntent
			|| GhostLocations.Num() != Other.GhostLocations.Num())
		{
			return false;
		}
		for (int32 i = 0; i < GhostLocations.Num(); ++i)
		{
			if (!GhostLocations[i].Equals(Other.GhostLocations[i]))
			{
				return false;
			}
		}
		return SetsEqual(SelectedVertices, Other.SelectedVertices)
			&& SetsEqual(SelectedEdges, Other.SelectedEdges)
			&& SetsEqual(CrossingEdges, Other.CrossingEdges);
	}

	bool operator!=(const FPCGExClusterSketchEditState& Other) const { return !(*this == Other); }
};

/**
 * A Cluster Sketch living ON an actor: authored inline, or instancing a Cluster Sketch asset.
 *
 * Mirrors the FRuntimeFloatCurve bargain -- inline data by default, with an optional asset reference
 * that takes over wholesale. While an asset drives the component, the inline payload is untouched and
 * READ-ONLY (GetMutableModel returns null, so every authoring path no-ops); "Inline Sketch Asset"
 * copies the asset payload in and clears the reference to start diverging. Editing a referenced asset
 * through an instance would rewrite topology shared by every other instance in the level.
 *
 * The component transform is the sketch's model->world frame, so a printed cluster lands where the
 * component sits -- and the visual rides that transform live.
 */
UCLASS(BlueprintType, ClassGroup = (PCGEx), meta = (BlueprintSpawnableComponent, DisplayName = "PCGEx Cluster Sketch"),
	HideCategories = (Physics, Collision, Lighting, Rendering, Tags, Activation, Cooking, AssetUserData, Navigation, Mobility, LOD, HLOD))
class PCGEXELEMENTSCLUSTERSSKETCH_API UPCGExClusterSketchComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPCGExClusterSketchComponent();

	/** When set, this component INSTANCES the asset: its payload replaces the inline one wholesale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch")
	TObjectPtr<UPCGExClusterSketch> SketchAsset;

	UPROPERTY(EditAnywhere, Category = "Cluster Sketch", meta = (EditCondition = "SketchAsset == nullptr"))
	FPCGExClusterSketchModel InlineModel;

	/** Snap-lattice model the inline sketch is authored against. None = free-form. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Cluster Sketch", meta = (EditCondition = "SketchAsset == nullptr"))
	TObjectPtr<UPCGExClusterSnapProvider> InlineSnapProvider;

	/** Print-time attribute decorators for the inline sketch, run in order. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Cluster Sketch", meta = (EditCondition = "SketchAsset == nullptr"))
	TArray<TObjectPtr<UPCGExClusterSketchDecorator>> InlineDecorators;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch|Display")
	bool bShowSketch = true;

	/** Draw the snap-lattice axes at the basis origin -- a live "the snap model is on" cue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch|Display")
	bool bShowBasis = true;

	/**
	 * Colors are the only per-sketch style: meshes, materials and sizes are project-wide
	 * (PCGEx | Cluster Sketch settings), so every sketch shares one geometry vocabulary and only its
	 * expression varies.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch|Display")
	bool bOverrideColors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch|Display", meta = (EditCondition = "bOverrideColors"))
	FLinearColor VertexColor = FLinearColor(0.9f, 0.9f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster Sketch|Display", meta = (EditCondition = "bOverrideColors"))
	FLinearColor EdgeColor = FLinearColor(0.55f, 0.55f, 0.6f);

	bool IsUsingAsset() const { return SketchAsset != nullptr; }

	/** The payload in force -- the asset's when one is referenced, the inline one otherwise. */
	const FPCGExClusterSketchModel& GetModel() const;
	const UPCGExClusterSnapProvider* GetSnapProvider() const;
	TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> GetDecorators() const;

	/** Null while an asset drives this component -- the read-only rule, enforced at the API. */
	FPCGExClusterSketchModel* GetMutableModel();

	bool BuildBasis(FPCGExLatticeBasis& OutBasis) const;
	void CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const;

	/** Print the payload in force. Same contract as UPCGExClusterSketch::Print. */
	TSharedPtr<PCGExGraphs::FGraphBuilder> Print(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
		const FPCGExGraphBuilderDetails* InBuilderDetails,
		bool bQuiet = false,
		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled = nullptr) const;

#if WITH_EDITOR
	/** Rebuild the draw snapshot from the payload in force and repaint. Call after mutating the model
	 *  through anything other than the details panel (the edit controller, script). */
	void RefreshSketchVisual();

	const FPCGExClusterSketchVisualSnapshot& GetVisualSnapshot() const { return VisualSnapshot; }

	/**
	 * Hand the component what the editing host is showing. The MESH layer keeps drawing (recoloured to
	 * the edit palette) so a sketch never changes shape just because it was selected; the component's
	 * own immediate-mode fallback stands down because the HOST draws it instead, together with the state
	 * chrome it needs (hover, ghosts, affordances).
	 *
	 * The per-kind fallback rule is preserved end to end: a kind with a mesh draws as meshes, a kind
	 * without one falls back to immediate mode -- in preview that is the component drawing, in edit it
	 * is the host, and the host skips exactly the kinds the mesh layer already covers.
	 * Cheap to call every frame: unchanged state is a no-op.
	 */
	void SetEditState(const FPCGExClusterSketchEditState& InState);

	/** Per KIND, whether the mesh layer is covering it right now (depends on the active style, which
	 *  differs between preview and edit) -- hosts skip drawing exactly what is already drawn. */
	bool DrawsVerticesAsMesh() const;
	bool DrawsEdgesAsMesh() const;
	/** True when the hovered element is wrapped by a real outline mesh, so hosts skip hover chrome. */
	bool DrawsHoverAsMesh() const;
	/** True when crossing ghosts render as phantom-style meshes, so hosts skip drawing ghost markers. */
	bool DrawsGhostsAsMesh() const;
#endif

	//~ Begin UPrimitiveComponent
#if WITH_EDITOR
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
#endif
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	/** The instanced-mesh children are outered to the OWNER, and USceneComponent::DestroyComponent only
	 *  re-parents children -- without this they survive as registered orphans, still drawing. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~ End UPrimitiveComponent

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	/** Coord/location coherence for the INLINE payload -- the same rule the asset host applies, so a
	 *  sketch behaves identically whichever host carries it. Called by the inline snap provider. */
	void EDITOR_OnSnapProviderChanged();
	void EDITOR_SyncBoundVertices(bool bResnapFromLocation);

	/**
	 * Save the payload IN FORCE as a NEW Cluster Sketch asset (standard save dialog) and reference it.
	 * The round trip to "Inline Sketch Asset": externalize a sketch authored in the level, or fork the
	 * referenced one. Instanced subobjects are duplicated into the asset, never shared with this
	 * component. No-op if the user cancels.
	 */
	UFUNCTION(CallInEditor, Category = "Cluster Sketch", DisplayName = "Save To Asset")
	void SaveToAsset();

	/** Copy the referenced asset's payload inline and clear the reference -- the way to start diverging
	 *  from a shared sketch without touching it. */
	UFUNCTION(CallInEditor, Category = "Cluster Sketch", DisplayName = "Inline Sketch Asset")
	void InlineSketchAsset();
#endif

private:
#if WITH_EDITOR
	void BuildVisualSnapshot();
	/** Rebuild the instanced-mesh preview (one ISM per element kind). Kinds with no mesh configured
	 *  fall back to the immediate-mode snapshot instead. */
	void RebuildInstances();
	/** Hover and selection pass: they change on every mouse move, and re-ADDING instances for a state
	 *  flip would churn the whole array. Rewrites colour and transform in place instead -- transform
	 *  too, because both states also grow the element. */
	void UpdateInstanceAppearance();
	void RefreshHoverOutline();
	void RefreshGhosts();
	/** Recompute what the delete gesture would take. Derived from the edit state and the model, so it is
	 *  refreshed at the top of every repaint rather than stored by a host. */
	void RefreshDoomedSets();

	/** Resolved local-space location of every vertex, coord-derived where lattice-bound -- the same rule
	 *  the print path applies, so what is authored is what prints. */
	void ResolveVertexLocations(TArray<FVector>& OutLocations) const;

	UInstancedStaticMeshComponent* EnsureInstances(TObjectPtr<UInstancedStaticMeshComponent>& InOut, const TSoftObjectPtr<UStaticMesh>& InMesh, const TSoftObjectPtr<UMaterialInterface>& InMaterial);

	/** Styles in force, which differ between preview and edit. */
	const FPCGExSketchElementStyle& ResolveVertexStyle() const;
	const FPCGExSketchElementStyle& ResolveGhostStyle() const;
	const FPCGExSketchElementStyle& ResolveEdgeStyle() const;
	FLinearColor ResolveVertexColor(int32 ModelIndex) const;
	FLinearColor ResolveEdgeColor(int32 ModelIndex) const;
	/** Hover and selection also GROW an element. Hover wins when both apply, matching the colour rule. */
	double ResolveVertexSizeScale(int32 ModelIndex) const;
	double ResolveEdgeSizeScale(int32 ModelIndex) const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> VertexInstances;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> EdgeInstances;

	/** One instance, wrapping whatever is hovered. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> HoverOutlineInstances;

	/** Hypothetical vertices offered at crossings -- unmaterialized, so they draw in the PHANTOM style.
	 *  Indexed by EditState.GhostLocations, not by the model: they have no model index yet. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GhostInstances;

	/** What alt-hovering the current vertex would delete -- the vertex, its edges, and any side-effect
	 *  vertex orphaned by the removal. Empty unless the delete modifier is held over a vertex. */
	TSet<int32> DoomedVertices;
	TSet<int32> DoomedEdges;

	/** instance -> model index, so the appearance pass knows what each instance represents. */
	TArray<int32> VertexInstanceToModel;
	TArray<int32> EdgeInstanceToModel;

	FPCGExClusterSketchVisualSnapshot VisualSnapshot;

	/** See SetEditState. */
	FPCGExClusterSketchEditState EditState;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/** Editing the REFERENCED asset must repaint every instance of it -- this is the other half of the
	 *  asset's NotifyObjectChanged calls. */
	FDelegateHandle OnObjectPropertyChangedHandle;

	/** Style settings are read at BUILD time (a mesh swap has to reach existing instances), so a change
	 *  there has to push a repaint rather than wait to be noticed. */
	FDelegateHandle OnStyleChangedHandle;
#endif
};
