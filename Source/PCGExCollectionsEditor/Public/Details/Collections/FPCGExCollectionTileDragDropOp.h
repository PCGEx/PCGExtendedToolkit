// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

/**
 * Custom drag-drop operation for internal tile reordering within the collection grid view.
 * Carries the dragged entry indices and their source category.
 */
class FPCGExCollectionTileDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FPCGExCollectionTileDragDropOp, FDecoratedDragDropOp)

	/** Indices into the Entries array being dragged */
	TArray<int32> DraggedIndices;

	/** Category these entries originated from */
	FName SourceCategory;

	static TSharedRef<FPCGExCollectionTileDragDropOp> New(const TArray<int32>& InIndices, FName InSourceCategory)
	{
		TSharedRef<FPCGExCollectionTileDragDropOp> Op = MakeShareable(new FPCGExCollectionTileDragDropOp());
		Op->DraggedIndices = InIndices;
		Op->SourceCategory = InSourceCategory;
		Op->DefaultHoverText = FText::Format(
			INVTEXT("Move {0} {0}|plural(one=entry,other=entries)"),
			FText::AsNumber(InIndices.Num()));
		Op->CurrentHoverText = Op->DefaultHoverText;
		Op->Construct();
		return Op;
	}
};
