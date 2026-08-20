// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchEditor.h"

#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchToolkit.h"

void UPCGExClusterSketchEditor::Initialize(UPCGExClusterSketch* InSketch)
{
	check(InSketch)
	Sketch = InSketch;
	UAssetEditor::Initialize();
}

void UPCGExClusterSketchEditor::GetObjectsToEdit(TArray<UObject*>& OutObjects)
{
	OutObjects.Add(Sketch);
}

TSharedPtr<FBaseAssetToolkit> UPCGExClusterSketchEditor::CreateToolkit()
{
	return MakeShared<FPCGExClusterSketchToolkit>(this);
}
