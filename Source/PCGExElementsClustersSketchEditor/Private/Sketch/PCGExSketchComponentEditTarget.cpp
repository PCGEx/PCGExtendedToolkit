// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchComponentEditTarget.h"

#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "Sketch/PCGExClusterSketchComponent.h"

FPCGExSketchComponentEditTarget::FPCGExSketchComponentEditTarget(UPCGExClusterSketchComponent* InComponent)
	: Component(InComponent)
{
}

FPCGExClusterSketchModel* FPCGExSketchComponentEditTarget::GetModel()
{
	// PURE READ. TSharedPtr::operator-> is non-const even on a const pointer, so Slate attributes reach
	// this overload every prepass -- a side effect here fires on merely displaying the panel.
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

bool FPCGExSketchComponentEditTarget::CanEdit() const
{
	// Matches GetMutableModel: only an instance's own payload is writable.
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned && Pinned->HasInlineSketch();
}

void FPCGExSketchComponentEditTarget::BeginAuthoring()
{
	IPCGExSketchEditTarget::BeginAuthoring();

	// The model lives in the payload, a SEPARATE UObject, and a transaction record stores only a
	// reference to one -- so the payload has to be transacted in its own right.
	if (const UPCGExClusterSketchComponent* Pinned = Component.Get())
	{
		if (UPCGExClusterSketchPayload* Payload = Pinned->InlinePayload)
		{
			Payload->Modify();
		}
	}
}

FText FPCGExSketchComponentEditTarget::GetReadOnlyReason() const
{
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	if (Pinned && Pinned->IsUsingAsset())
	{
		return NSLOCTEXT("PCGExSketchEditTarget", "ReadOnlyAsset", "Read-only: this component instances a Cluster Sketch asset. Edit the asset, or use Create Inline Sketch to fork it onto this instance.");
	}
	return NSLOCTEXT("PCGExSketchEditTarget", "ReadOnlyNoInline", "This component has no inline sketch. Use Create Inline Sketch on the component to author one.");
}

UObject* FPCGExSketchComponentEditTarget::GetTransactionObject()
{
	return Component.Get();
}

UObject* FPCGExSketchComponentEditTarget::GetDetailsObject()
{
	const UPCGExClusterSketchComponent* Pinned = Component.Get();
	return Pinned ? Pinned->InlinePayload.Get() : nullptr;
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

		// Authoring writes the model directly, so nothing downstream ever sees a property event -- which
		// is why undo reaches consumers this path does not. Completed operations only; a drag bumps
		// ModelRevision instead, so this never fires per frame.
		PCGExEditor::NotifyObjectChanged(Pinned);
	}
}
