// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Tools/UAssetEditor.h"

#include "PCGExClusterSketchEditor.generated.h"

class UPCGExClusterSketch;

/**
 * UAssetEditor wrapper for the Cluster Sketch editor: owns the edited asset and spawns the toolkit.
 * Kept alive by UAssetEditorSubsystem's OwnedAssetEditors from Initialize until the toolkit closes.
 */
UCLASS(Transient)
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API UPCGExClusterSketchEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	/** Store the sketch and run UAssetEditor::Initialize (registers, creates + inits the toolkit). */
	void Initialize(UPCGExClusterSketch* InSketch);

	virtual void GetObjectsToEdit(TArray<UObject*>& OutObjects) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

	UPCGExClusterSketch* GetSketch() const
	{
		return Sketch;
	}

protected:
	UPROPERTY()
	TObjectPtr<UPCGExClusterSketch> Sketch;
};
