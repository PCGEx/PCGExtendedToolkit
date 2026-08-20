// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchFactories.h"

#include "Sketch/PCGExClusterSketchEditor.h"

EAssetCommandResult UAssetDefinition_PCGExClusterSketch::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UPCGExClusterSketch* Sketch : OpenArgs.LoadObjects<UPCGExClusterSketch>())
	{
		// The subsystem's RegisterUAssetEditor (called in Initialize) holds the strong reference.
		UPCGExClusterSketchEditor* Editor = NewObject<UPCGExClusterSketchEditor>();
		Editor->Initialize(Sketch);
	}
	return EAssetCommandResult::Handled;
}
