// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchViewportClient.h"

#include "EditorModeManager.h"
#include "InputRouter.h"
#include "Sketch/PCGExClusterSketchComponent.h"
#include "Sketch/PCGExSketchDrawHelper.h"
#include "Sketch/PCGExSketchEditController.h"
#include "Sketch/PCGExSketchInputBinder.h"
#include "Tools/EdModeInteractiveToolsContext.h"

namespace PCGExClusterSketchViewportClient
{
	// Dark authoring backdrop. Sketch geometry draws unlit in the foreground, so the scene's job is
	// orientation, not lighting: the ground grid carries up/down and the backdrop stays out of the way.
	constexpr FLinearColor BackgroundColor = FLinearColor(0.016f, 0.018f, 0.022f);
	constexpr FColor GridColorAxis = FColor(96, 96, 104);
	constexpr FColor GridColorMajor = FColor(52, 52, 58);
	constexpr FColor GridColorMinor = FColor(30, 30, 34);
}

FPCGExClusterSketchViewportClient::FPCGExClusterSketchViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TSharedPtr<FPCGExSketchEditController>& InController, UPCGExClusterSketchComponent* InSketchComponent)
	: FEditorViewportClient(InModeTools, InPreviewScene)
	  , Controller(InController)
	  , OwnerModeTools(InModeTools)
	  , SketchComponent(InSketchComponent)
{
	// Continuous redraw: hover/drag affordances live in Draw, not in a scene component.
	SetRealtime(true);

	// The ground grid is the up/down cue against the dark backdrop (perspective uses the engine's
	// faded grid widget; these colors serve the orthographic views).
	EngineShowFlags.SetGrid(true);
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawBaseInfo = false; // no actors in this scene
	DrawHelper.GridColorAxis = PCGExClusterSketchViewportClient::GridColorAxis;
	DrawHelper.GridColorMajor = PCGExClusterSketchViewportClient::GridColorMajor;
	DrawHelper.GridColorMinor = PCGExClusterSketchViewportClient::GridColorMinor;

	// Host policy only: one fixed controller, always addressed.
	const TSharedPtr<FPCGExSketchEditController> Fixed = Controller;
	InputBinder = NewObject<UPCGExSketchInputBinder>();
	InputBinder->Initialize(
		[Fixed](const FRay&)
		{
			return Fixed;
		},
		[Fixed](TFunctionRef<void(FPCGExSketchEditController&)> InFn)
		{
			if (Fixed)
			{
				InFn(*Fixed);
			}
		});

	// The mode manager's tools context is constructed + activated with the manager itself, so the
	// router exists by the time any toolkit reaches viewport creation.
	InputBinder->RegisterWith(OwnerModeTools->GetInteractiveToolsContext()->InputRouter);
}

FPCGExClusterSketchViewportClient::~FPCGExClusterSketchViewportClient()
{
	// The mode manager (and its router) lives on the toolkit BASE class, so it outlives this client.
	if (OwnerModeTools && InputBinder)
	{
		if (UModeManagerInteractiveToolsContext* ToolsContext = OwnerModeTools->GetInteractiveToolsContext())
		{
			InputBinder->DeregisterFrom(ToolsContext->InputRouter);
		}
	}
}

FLinearColor FPCGExClusterSketchViewportClient::GetBackgroundColor() const
{
	return PCGExClusterSketchViewportClient::BackgroundColor;
}

void FPCGExClusterSketchViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	// Base first: it forwards to the mode tools / ITF render path.
	FEditorViewportClient::Draw(View, PDI);
	if (Controller)
	{
		// The same pass the in-level mode runs: push edit state into the mesh layer, then draw only what
		// the meshes do not cover.
		FPCGExSketchDrawHelper::DrawWithComponent(*Controller, SketchComponent.Get(), PDI);
	}
}

bool FPCGExClusterSketchViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Event == IE_Pressed && InputBinder && InputBinder->HandleKeyDown(EventArgs.Key))
	{
		return true;
	}
	return FEditorViewportClient::InputKey(EventArgs);
}

void FPCGExClusterSketchViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	if (InputBinder && Key == EKeys::LeftMouseButton && IsAltPressed())
	{
		const FViewportClick Click(&View, this, Key, Event, HitX, HitY);
		if (InputBinder->HandleAltClick(FRay(Click.GetOrigin(), Click.GetDirection())))
		{
			return;
		}
	}
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FPCGExClusterSketchViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(InputBinder);
}
