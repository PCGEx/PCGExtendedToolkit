// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketch.h"

#include "Sketch/PCGExClusterSketchPrint.h"

namespace PCGExSketch
{
	FSaveSketchAsAssetFn GSaveSketchAsAssetFn;
}

bool UPCGExClusterSketch::BuildBasis(FPCGExLatticeBasis& OutBasis) const
{
	return SnapProvider ? SnapProvider->BuildBasis(OutBasis) : false;
}

FBox UPCGExClusterSketch::GetBounds() const
{
	FPCGExLatticeBasis Basis;
	return Model.GetBounds(BuildBasis(Basis) ? &Basis : nullptr);
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

		// A schema edit arrives as a Model change like any other -- MemberProperty is always the object's
		// own member -- so the gate is deliberately coarse. Idempotent, and reachable only from an editor
		// edit hook (here, or the panel's transacted write-back).
		Model.Data.EDITOR_SyncAll();
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
#endif
