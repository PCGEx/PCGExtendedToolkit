// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExValencyCageConnectorVisualizer.h"

#include "EditorViewportClient.h"
#include "Editor.h"
#include "SceneManagement.h"
#include "EditorModeManager.h"

#include "PCGExValencyEditorSettings.h"
#include "Selection.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Cages/PCGExValencyCageBase.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "EditorMode/PCGExValencyDrawHelper.h"

IMPLEMENT_HIT_PROXY(HPCGExConnectorHitProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HPCGExConstraintHandleHitProxy, HComponentVisProxy);

#pragma region FPCGExValencyCageConnectorVisualizer

void FPCGExValencyCageConnectorVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (!Component || !PDI)
	{
		return;
	}

	// When Valency mode is active, DrawCageConnectors in the render callback handles all drawing
	if (GLevelEditorModeTools().IsModeActive(UPCGExValencyCageEditorMode::ModeID))
	{
		return;
	}

	const UPCGExValencyCageConnectorComponent* ConnectorComp = Cast<UPCGExValencyCageConnectorComponent>(Component);
	if (!ConnectorComp)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = UPCGExValencyEditorSettings::Get();
	if (!Settings || !Settings->bShowConnectorVisualizers)
	{
		return;
	}

	const FTransform ConnectorTransform = ConnectorComp->GetComponentTransform();
	const FVector ConnectorLocation = ConnectorTransform.GetLocation();

	PDI->SetHitProxy(new HPCGExConnectorHitProxy(ConnectorComp));

	const FQuat Rotation = ConnectorTransform.GetRotation();
	const bool bSelected = ConnectorComp == UPCGExValencyCageEditorMode::GetSelectedConnector();

	if (!ConnectorComp->bEnabled)
	{
		FPCGExValencyDrawHelper::DrawDisabledConnector(PDI, ConnectorLocation, Rotation, Settings->ConnectorVisualizerSize, bSelected);
	}
	else
	{
		const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(ConnectorComp->GetOwner());
		const UPCGExValencyConnectorSet* ConnectorSet = OwnerCage ? OwnerCage->GetEffectiveConnectorSet() : nullptr;
		const FLinearColor Color = ConnectorComp->GetEffectiveDebugColor(ConnectorSet);
		FPCGExValencyDrawHelper::DrawConnectorShape(
			PDI, ConnectorLocation,
			Rotation.GetForwardVector(), Rotation.GetRightVector(), Rotation.GetUpVector(),
			ConnectorComp->Polarity, Settings->ConnectorVisualizerSize, Settings->ConnectorArrowLength, Color, bSelected);
	}

	PDI->SetHitProxy(nullptr);
}

bool FPCGExValencyCageConnectorVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (!VisProxy)
	{
		return false;
	}

	const HPCGExConnectorHitProxy* ConnectorProxy = HitProxyCast<HPCGExConnectorHitProxy>(VisProxy);
	if (!ConnectorProxy || !ConnectorProxy->Component.IsValid())
	{
		return false;
	}

	UPCGExValencyCageConnectorComponent* ConnectorComp = const_cast<UPCGExValencyCageConnectorComponent*>(
		Cast<UPCGExValencyCageConnectorComponent>(ConnectorProxy->Component.Get()));
	if (!ConnectorComp)
	{
		return false;
	}

	if (GEditor && ConnectorComp->GetOwner())
	{
		GEditor->SelectActor(ConnectorComp->GetOwner(), true, true);
		GEditor->SelectComponent(ConnectorComp, true, true);
	}

	return true;
}

bool FPCGExValencyCageConnectorVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	// Derive widget location from editor selection — clears when actor is deselected
	if (UPCGExValencyCageConnectorComponent* Connector = UPCGExValencyCageEditorMode::GetSelectedConnector())
	{
		// Only show widget if the owning actor is still selected
		AActor* OwnerActor = Connector->GetOwner();
		if (GEditor && OwnerActor && GEditor->GetSelectedActors()->IsSelected(OwnerActor))
		{
			OutLocation = Connector->GetComponentLocation();
			return true;
		}
	}
	return false;
}

bool FPCGExValencyCageConnectorVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	UPCGExValencyCageConnectorComponent* ConnectorComp = UPCGExValencyCageEditorMode::GetSelectedConnector();
	if (!ConnectorComp)
	{
		return false;
	}

	// Only allow manipulation if the owning actor is still selected
	AActor* OwnerActor = ConnectorComp->GetOwner();
	if (!GEditor || !OwnerActor || !GEditor->GetSelectedActors()->IsSelected(OwnerActor))
	{
		return false;
	}

	if (!DeltaTranslate.IsZero())
	{
		ConnectorComp->SetWorldLocation(ConnectorComp->GetComponentLocation() + DeltaTranslate);
	}

	if (!DeltaRotate.IsZero())
	{
		ConnectorComp->SetWorldRotation(ConnectorComp->GetComponentRotation() + DeltaRotate);
	}

	return true;
}

void FPCGExValencyCageConnectorVisualizer::EndEditing()
{
	// Selection-driven — nothing to clear
}

#pragma endregion
