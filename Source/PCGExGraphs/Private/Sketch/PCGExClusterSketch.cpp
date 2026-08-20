// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketch.h"

#include "Sketch/PCGExClusterSketchPrint.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#endif

namespace PCGExSketch
{
	FSaveSketchAsAssetFn GSaveSketchAsAssetFn;
}

bool UPCGExClusterSketch::BuildBasis(FPCGExLatticeBasis& OutBasis) const
{
	return SnapProvider ? SnapProvider->BuildBasis(OutBasis) : false;
}

void UPCGExClusterSketch::CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const
{
	if (SnapProvider)
	{
		SnapProvider->CollectAssetDependencies(OutPaths);
	}
	for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : Decorators)
	{
		if (Decorator && Decorator->bEnabled)
		{
			Decorator->CollectAssetDependencies(OutPaths);
		}
	}
}

TSharedPtr<PCGExGraphs::FGraphBuilder> UPCGExClusterSketch::Print(
	FPCGExContext* InContext,
	const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
	const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
	const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
	const FPCGExGraphBuilderDetails* InBuilderDetails,
	const bool bQuiet,
	TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled) const
{
	return PCGExSketch::PrintResolved(
		InContext, Model, SnapProvider, Decorators,
		InVtxIO, InTaskManager, InPrintContext, InBuilderDetails, bQuiet, MoveTemp(OnCompiled));
}

#if WITH_EDITOR
void UPCGExClusterSketch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (MemberName == GET_MEMBER_NAME_CHECKED(UPCGExClusterSketch, SnapProvider))
	{
		EDITOR_OnSnapProviderChanged();
		return;
	}

	if (MemberName == GET_MEMBER_NAME_CHECKED(UPCGExClusterSketch, Model))
	{
		// Hand-editing a vertex adopts it: tool-inserted provenance survives only until the user
		// deliberately touches the vertex (here, or through a gesture in the editor).
		const int32 EditedVertex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FPCGExClusterSketchModel, Vertices));
		if (EditedVertex != INDEX_NONE)
		{
			Model.MarkVertexAuthored(EditedVertex);
		}

		// Coord edited -> the coord wins; anything else -> re-snap from location first. Both paths are
		// idempotent for already-coherent vertices, so over-triggering on unrelated model edits is free.
		const FName LeafName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		const bool bCoordEdit = LeafName == GET_MEMBER_NAME_CHECKED(FPCGExClusterSketchVertex, LatticeCoord);
		EDITOR_SyncBoundVertices(!bCoordEdit);
	}
}

void UPCGExClusterSketch::PostEditUndo()
{
	Super::PostEditUndo();

	if (!IsValidChecked(this) || IsTemplate())
	{
		return;
	}

	EDITOR_SyncBoundVertices(false);
}

void UPCGExClusterSketch::EDITOR_OnSnapProviderChanged()
{
	EDITOR_SyncBoundVertices(false);
}

void UPCGExClusterSketch::EDITOR_SyncBoundVertices(const bool bResnapFromLocation)
{
	FPCGExLatticeBasis Basis;
	if (!BuildBasis(Basis))
	{
		return;
	}
	Model.SyncBoundVertices(Basis, bResnapFromLocation);
}

void UPCGExClusterSketch::MergeCollocatedVertices()
{
	FPCGExLatticeBasis Basis;
	const bool bHasBasis = BuildBasis(Basis);

	const FScopedTransaction Transaction(NSLOCTEXT("PCGExClusterSketch", "MergeCollocated", "Merge Collocated Sketch Vertices"));
	Modify();

	// Every merge remaps indices, so rescan from scratch after each one; the guard bounds the loop by
	// the only thing it can shrink.
	bool bMergedAny = true;
	int32 Guard = Model.Vertices.Num() + 1;
	while (bMergedAny && Guard-- > 0)
	{
		bMergedAny = false;
		TMap<FVector, int32> FirstAtLocation;
		FirstAtLocation.Reserve(Model.Vertices.Num());
		for (int32 i = 0; i < Model.Vertices.Num(); ++i)
		{
			const FVector Location = FPCGExClusterSketchModel::ResolvedLocation(Model.Vertices[i], bHasBasis ? &Basis : nullptr);
			const FVector Key = PCGExSketch::QuantizedLocationKey(Location);
			if (const int32* First = FirstAtLocation.Find(Key))
			{
				Model.MergeVertices(i, *First);
				bMergedAny = true;
				break;
			}
			FirstAtLocation.Add(Key, i);
		}
	}

	// Merging retargets edges, which can leave them passing THROUGH vertices (the collinear D-onto-A
	// case) -- genuinely degenerate, so the cleanup must resolve it. Crossings it may also create are
	// left alone: they are legitimate geometry, offered as ghosts and materialized on demand.
	Model.SplitAllOverlappingEdges(bHasBasis ? &Basis : nullptr);
	// Merges can also strand tool residue.
	Model.RemoveOrphanSideEffectVertices();

	PostEditChange();
	// Programmatic mutation: nothing else broadcasts the pair PCG asset trackers listen to.
	PCGExEditor::NotifyObjectChanged(this);
}

void UPCGExClusterSketch::RemoveInvalidEdges()
{
	const FScopedTransaction Transaction(NSLOCTEXT("PCGExClusterSketch", "RemoveInvalidEdges", "Remove Invalid Sketch Edges"));
	Modify();
	Model.RemoveInvalidEdges();
	// Dropping edges can strand tool residue, same as every other edge-removing operation.
	Model.RemoveOrphanSideEffectVertices();
	PostEditChange();
	// Programmatic mutation: nothing else broadcasts the pair PCG asset trackers listen to.
	PCGExEditor::NotifyObjectChanged(this);
}

void UPCGExClusterSketch::SplitOverlappingEdges()
{
	FPCGExLatticeBasis Basis;
	const bool bHasBasis = BuildBasis(Basis);
	const FPCGExLatticeBasis* BasisPtr = bHasBasis ? &Basis : nullptr;

	const FScopedTransaction Transaction(NSLOCTEXT("PCGExClusterSketch", "SplitOverlappingEdges", "Split Overlapping Sketch Edges"));
	Modify();
	// Order is free: materializing a crossing enforces separation around the vertex it inserts, so the
	// second pass cannot leave containment residue behind for a third press to find.
	Model.SplitAllOverlappingEdges(BasisPtr);
	Model.InsertCrossingVertices(BasisPtr);
	PostEditChange();
	// Programmatic mutation: nothing else broadcasts the pair PCG asset trackers listen to.
	PCGExEditor::NotifyObjectChanged(this);
}
#endif
