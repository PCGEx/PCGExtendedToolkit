// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSnapProvider.h"

#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchComponent.h"

#pragma region UPCGExClusterSnapProvider

#if WITH_EDITOR
namespace PCGExClusterSnapProvider
{
	/** A provider's host must be reached or its edits never re-derive the vertices they moved. The asset
	 *  is an ancestor; an inline provider's component is not -- only its payload is. */
	void NotifyHost(UObject* InProvider)
	{
		if (UPCGExClusterSketch* Asset = InProvider->GetTypedOuter<UPCGExClusterSketch>())
		{
			Asset->EDITOR_OnSnapProviderChanged();
			return;
		}

		const UPCGExClusterSketchPayload* Payload = InProvider->GetTypedOuter<UPCGExClusterSketchPayload>();
		if (UPCGExClusterSketchComponent* Component = Payload ? Payload->FindOwningComponent() : nullptr)
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
