// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sketch/PCGExClusterSketchDecorator.h"
#include "Sketch/PCGExClusterSketchModel.h"
#include "Sketch/PCGExClusterSnapProvider.h"

#include "PCGExClusterSketch.generated.h"

struct FPCGExContext;
struct FPCGExClusterSketchPrintContext;
struct FPCGExGraphBuilderDetails;

namespace PCGExData
{
	class FPointIO;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExSketch
{
	/**
	 * Editor-only bridge: create a NEW Cluster Sketch asset through the standard save dialog, seeded
	 * with the given payload (instanced subobjects are DUPLICATED into the new asset, never shared with
	 * the source). Set by PCGExElementsClustersSketchEditor at startup, so a runtime host can offer "save this to an
	 * asset" without depending on editor modules.
	 *
	 * Deliberately not editor-gated -- it is a plain null pointer in cooked builds, so callers null-check.
	 * @return the created asset, or null if the user cancelled the dialog.
	 */
	using FSaveSketchAsAssetFn = TFunction<UPCGExClusterSketch*(
		const FPCGExClusterSketchModel& InModel,
		const UPCGExClusterSnapProvider* InSnapProvider,
		TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> InDecorators,
		const FString& InDefaultAssetName)>;

	PCGEXELEMENTSCLUSTERSSKETCH_API extern FSaveSketchAsAssetFn GSaveSketchAsAssetFn;
}

/**
 * A hand-authored, spawnable cluster: the sketch model (vertices, edges, and the authored data tier
 * annotating them), an optional snap provider, and print-time decorators. Print-on-demand by design --
 * the asset holds NO baked point data and no derived state; a consumer prints a live Vtx/Edges pair from
 * the model at execute time and duplicates it per target.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGEx")
class PCGEXELEMENTSCLUSTERSSKETCH_API UPCGExClusterSketch : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGExClusterSketchModel Model;

	/** Snap-lattice model this sketch is authored against. None = free-form (bound vertices then fall
	 *  back to their cached locations, with a print-time warning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Settings)
	TObjectPtr<UPCGExClusterSnapProvider> SnapProvider;

	/** Print-time attribute decorators, run in order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Settings)
	TArray<TObjectPtr<UPCGExClusterSketchDecorator>> Decorators;

	/** Basis from the snap provider; false when there is no provider or it forms no usable lattice. */
	bool BuildBasis(FPCGExLatticeBasis& OutBasis) const;

	/** Model extent through this sketch's OWN basis -- the same resolution the print path applies, so
	 *  staged bounds match what a consumer actually gets. */
	FBox GetBounds() const;

	/** Union of the provider's and enabled decorators' soft dependencies -- load these before printing. */
	void CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const;

	/**
	 * Print this sketch into InVtxIO and launch its cluster compile: the asset assembles the print
	 * request from its OWN snap provider and decorators, so a consumer just hands over an IO and gets
	 * the finished Vtx/Edges pair. Consumers printing a loose model (a component's inline sketch) call
	 * PCGExSketch::PrintClusterSketch directly instead.
	 *
	 * @param OnCompiled fires from the compile end callback, after the print context's VtxIndexMap is
	 *        filled, with the success flag.
	 * @return the builder, or null when the sketch prints nothing (caller drops the vtx IO).
	 */
	TSharedPtr<PCGExGraphs::FGraphBuilder> Print(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
		const FPCGExGraphBuilderDetails* InBuilderDetails,
		bool bQuiet = false,
		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled = nullptr) const;

#if WITH_EDITOR
	/**
	 * Coord/position coherence (the sketch's one editing rule): a bound vertex's location is derived from
	 * its coord. Editing the COORD re-derives the location (coord wins); any other model edit re-snaps
	 * coords from locations first, so a hand-edited transform can never dangle off-lattice.
	 *
	 * ALSO the authored tier's one legal sync site: FPCGExPropertyOverrides::SyncToSchema matches identity
	 * only under WITH_EDITOR and wipes to schema defaults otherwise, so the sync may never be reached from
	 * load, from the print path, or from any model mutation.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Undo restores model+coords wholesale (no derived state on this asset); the sync is a cheap
	 *  idempotent belt-and-braces for provider-only transactions. */
	virtual void PostEditUndo() override;

	/** Provider params moved the lattice: coords stay authoritative, bound locations re-derive. */
	void EDITOR_OnSnapProviderChanged();

	/** See PostEditChangeProperty. No-op without a usable basis. */
	void EDITOR_SyncBoundVertices(bool bResnapFromLocation);
#endif
};
