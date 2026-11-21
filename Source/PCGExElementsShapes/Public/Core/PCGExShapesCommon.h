// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExShapesCommon.generated.h"

UENUM()
enum class EPCGExShapeOutputMode : uint8
{
	PerDataset = 0 UMETA(DisplayName = "Per Dataset", ToolTip="Merge all shapes into the original dataset"),
	PerSeed    = 1 UMETA(DisplayName = "Per Seed", ToolTip="Create a new output per shape seed point"),
	PerShape   = 2 UMETA(DisplayName = "Per Shape", ToolTip="Create a new output per individual shape"),
};

UENUM()
enum class EPCGExShapePointLookAt : uint8
{
	None = 0 UMETA(DisplayName = "None", ToolTip="Point look at will be as per 'canon' shape definition"),
	Seed = 1 UMETA(DisplayName = "Seed", ToolTip="Look At Seed"),
};

UENUM()
enum class EPCGExShapeBoundsSource : uint8
{
	Fit      = 0 UMETA(DisplayName = "Fit", ToolTip="Compute bounds from point spacing so adjacent points don't overlap"),
	Constant = 1 UMETA(DisplayName = "Constant", ToolTip="Use a constant extent value"),
};

namespace PCGExShapes::Labels
{
	const FName OutputShapeBuilderLabel = TEXT("Shape Builder");
	const FName SourceShapeBuildersLabel = TEXT("Shape Builders");
}
