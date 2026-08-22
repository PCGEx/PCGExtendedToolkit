// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Sketch/PCGExSketchEditController.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UPCGExClusterSketchComponent;

/**
 * Edit target for a sketch living on an actor -- the level-mode counterpart of
 * FPCGExSketchAssetEditTarget. The controller, the gesture vocabulary and the drawing are identical
 * for both; only these six methods differ.
 *
 * Read-only while the component references an asset: GetModel() (mutable) forwards the component's
 * null, and every authoring path in the controller already no-ops on it.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchComponentEditTarget final : public IPCGExSketchEditTarget
{
public:
	explicit FPCGExSketchComponentEditTarget(UPCGExClusterSketchComponent* InComponent);

	UPCGExClusterSketchComponent* GetComponent() const
	{
		return Component.Get();
	}

	virtual FPCGExClusterSketchModel* GetModel() override;
	virtual const FPCGExClusterSketchModel* GetModel() const override;
	virtual const UPCGExClusterSnapProvider* GetSnapProvider() const override;
	virtual bool CanEdit() const override;
	virtual void BeginAuthoring() override;
	virtual FText GetReadOnlyReason() const override;
	virtual UObject* GetTransactionObject() override;
	virtual UObject* GetDetailsObject() override;
	/** The component transform IS the sketch frame, so rays convert into model space through it. */
	virtual FTransform GetLocalToWorld() const override;
	virtual void NotifyChanged() override;

private:
	TWeakObjectPtr<UPCGExClusterSketchComponent> Component;
};
