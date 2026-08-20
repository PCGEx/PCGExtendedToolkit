// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSnapProvider.h"

#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchComponent.h"

#pragma region UPCGExClusterSnapProvider

#if WITH_EDITOR
namespace PCGExClusterSnapProvider
{
	/** A provider is outered to whichever host owns it -- the asset or a component -- so both must be
	 *  reached, or an inline provider's edits never re-derive the vertices they moved. */
	void NotifyHost(UObject* InProvider)
	{
		if (UPCGExClusterSketch* Asset = InProvider->GetTypedOuter<UPCGExClusterSketch>())
		{
			Asset->EDITOR_OnSnapProviderChanged();
		}
		else if (UPCGExClusterSketchComponent* Component = InProvider->GetTypedOuter<UPCGExClusterSketchComponent>())
		{
			Component->EDITOR_OnSnapProviderChanged();
		}
	}
}

void UPCGExClusterSnapProvider::PostEditUndo()
{
	Super::PostEditUndo();

	// An undo that deletes this object reaches here already invalid.
	if (!IsValidChecked(this) || IsTemplate())
	{
		return;
	}

	PCGExClusterSnapProvider::NotifyHost(this);
}

void UPCGExClusterSnapProvider::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!IsValidChecked(this) || IsTemplate())
	{
		return;
	}

	PCGExClusterSnapProvider::NotifyHost(this);
}
#endif

#pragma endregion

#pragma region UPCGExClusterSnapProvider_UniformGrid

bool UPCGExClusterSnapProvider_UniformGrid::BuildBasis(FPCGExLatticeBasis& OutBasis) const
{
	TArray<FVector, TInlineAllocator<3>> Directions;
	if (bSpanX)
	{
		Directions.Add(FVector::XAxisVector);
	}
	if (bSpanY)
	{
		Directions.Add(FVector::YAxisVector);
	}
	if (bSpanZ)
	{
		Directions.Add(FVector::ZAxisVector);
	}

	TArray<double, TInlineAllocator<3>> LengthMultipliers;
	LengthMultipliers.Init(1.0, Directions.Num());

	return OutBasis.BuildFromSteps(Directions, LengthMultipliers, CellSize, Origin, Rotation.Quaternion());
}

#pragma endregion
