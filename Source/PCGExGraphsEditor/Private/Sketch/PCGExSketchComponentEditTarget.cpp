// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchComponentEditTarget.h"

#include "Sketch/PCGExClusterSketchComponent.h"

FPCGExSketchComponentEditTarget::FPCGExSketchComponentEditTarget(UPCGExClusterSketchComponent* InComponent)
	: Component(InComponent)
{
}

FPCGExClusterSketchModel* FPCGExSketchComponentEditTarget::GetModel()
{
	UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned ? Pinned->GetMutableModel() : nullptr;
}

const FPCGExClusterSketchModel* FPCGExSketchComponentEditTarget::GetModel() const
{
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned ? &Pinned->GetModel() : nullptr;
}

const UPCGExClusterSnapProvider* FPCGExSketchComponentEditTarget::GetSnapProvider() const
{
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned ? Pinned->GetSnapProvider() : nullptr;
}

UObject* FPCGExSketchComponentEditTarget::GetTransactionObject()
{
	return Component.Get();
}

FTransform FPCGExSketchComponentEditTarget::GetLocalToWorld() const
{
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned ? Pinned->GetComponentTransform() : FTransform::Identity;
}

void FPCGExSketchComponentEditTarget::NotifyChanged()
{
	if (UPCGExClusterSketchComponent* Pinned = Component.Get())
	{
		// Keeps the passive snapshot current for the moment mode suppression lifts; the mode itself
		// draws from the controller, so this is not what repaints during editing.
		Pinned->RefreshSketchVisual();
	}
}
