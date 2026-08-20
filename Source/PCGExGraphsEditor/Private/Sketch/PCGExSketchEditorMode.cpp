// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchEditorMode.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "InputRouter.h"
#include "Selection.h"
#include "ToolContextInterfaces.h"
#include "GameFramework/Actor.h"
#include "Sketch/PCGExClusterSketchComponent.h"
#include "Sketch/PCGExSketchComponentEditTarget.h"
#include "Sketch/PCGExSketchDrawHelper.h"
#include "Sketch/PCGExSketchEditController.h"
#include "Sketch/PCGExSketchInputBinder.h"
#include "Tools/EdModeInteractiveToolsContext.h"

#define LOCTEXT_NAMESPACE "PCGExSketchEditorMode"

const FEditorModeID UPCGExSketchEditorMode::ModeID = TEXT("PCGExSketchEditorMode");

UPCGExSketchEditorMode::UPCGExSketchEditorMode()
{
	Info = FEditorModeInfo(
		ModeID,
		LOCTEXT("SketchModeName", "PCGEx | Cluster Sketch"),
		FSlateIcon(),
		true);
}

void UPCGExSketchEditorMode::Enter()
{
	Super::Enter();

	if (UEditorInteractiveToolsContext* ToolsContext = GetInteractiveToolsContext())
	{
		OnRenderHandle = ToolsContext->OnRender.AddUObject(this, &UPCGExSketchEditorMode::OnRenderCallback);
	}

	if (UEditorInteractiveToolsContext* EdModeToolsContext = GetInteractiveToolsContext(EToolsContextScope::EdMode))
	{
		// Stale-source guard: UEdMode::Exit can be deferred to a later tick, so deregister and null any
		// leftover source before re-registering (mirrors ULevelInstanceEditorMode).
		if (InputBinder)
		{
			InputBinder->DeregisterFrom(EdModeToolsContext->InputRouter);
			InputBinder = nullptr;
		}

		InputBinder = NewObject<UPCGExSketchInputBinder>(this);
		InputBinder->Initialize(
			[this](const FRay& WorldRay) { return ResolveController(WorldRay); },
			[this](TFunctionRef<void(FPCGExSketchEditController&)> InFn)
			{
				for (const FPCGExSketchModeBinding& Binding : Bindings)
				{
					if (Binding.Controller)
					{
						InFn(*Binding.Controller);
					}
				}
			});
		InputBinder->RegisterWith(EdModeToolsContext->InputRouter);
	}

	if (GEditor)
	{
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddUObject(this, &UPCGExSketchEditorMode::OnSelectionChanged);
	}

	// Selection events do not fire for what is ALREADY selected when the mode opens.
	RebuildBindings();
}

void UPCGExSketchEditorMode::Exit()
{
	if (GEditor && OnSelectionChangedHandle.IsValid())
	{
		GEditor->GetSelectedActors()->SelectionChangedEvent.Remove(OnSelectionChangedHandle);
		OnSelectionChangedHandle.Reset();
	}

	if (UEditorInteractiveToolsContext* ToolsContext = GetInteractiveToolsContext())
	{
		if (OnRenderHandle.IsValid())
		{
			ToolsContext->OnRender.Remove(OnRenderHandle);
			OnRenderHandle.Reset();
		}
	}

	if (InputBinder)
	{
		if (UEditorInteractiveToolsContext* EdModeToolsContext = GetInteractiveToolsContext(EToolsContextScope::EdMode))
		{
			InputBinder->DeregisterFrom(EdModeToolsContext->InputRouter);
		}
		InputBinder = nullptr;
	}

	ReleaseBindings();

	Super::Exit();
}

void UPCGExSketchEditorMode::OnSelectionChanged(UObject* Object)
{
	RebuildBindings();
}

void UPCGExSketchEditorMode::RebuildBindings()
{
	// Controllers own live editing state (vertex/edge selection, hover, cached crossings), so a
	// component that survives the selection change KEEPS its controller. Rebinding a fresh one would
	// silently drop the user's selection -- and Delete would then fall through to the level editor.
	TArray<FPCGExSketchModeBinding> Previous = MoveTemp(Bindings);
	Bindings.Reset();

	USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	if (!Selection)
	{
		ReleaseBindingsIn(Previous);
		return;
	}
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		const AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		TArray<UPCGExClusterSketchComponent*> Components;
		Actor->GetComponents<UPCGExClusterSketchComponent>(Components);
		for (UPCGExClusterSketchComponent* Component : Components)
		{
			const int32 Existing = Previous.IndexOfByPredicate(
				[Component](const FPCGExSketchModeBinding& InBinding) { return InBinding.Component.Get() == Component; });

			if (Existing != INDEX_NONE)
			{
				// Already bound and already active: carry the controller (and its state) across.
				Bindings.Add(MoveTemp(Previous[Existing]));
				Previous.RemoveAt(Existing);
				continue;
			}

			FPCGExSketchModeBinding& Binding = Bindings.AddDefaulted_GetRef();
			Binding.Component = Component;
			Binding.Target = MakeShared<FPCGExSketchComponentEditTarget>(Component);
			Binding.Controller = MakeShared<FPCGExSketchEditController>(Binding.Target.ToSharedRef());

			// The component keeps its MESH layer (recoloured to the edit palette) and stands its
			// immediate-mode fallback down; this mode draws that plus the state chrome. A selected
			// sketch therefore never changes shape, only palette.
			FPCGExClusterSketchEditState State;
			State.bActive = true;
			Component->SetEditState(State);
		}
	}

	// Whatever is left went out of selection: hand it its passive visual back.
	ReleaseBindingsIn(Previous);
}

void UPCGExSketchEditorMode::ReleaseBindingsIn(const TArray<FPCGExSketchModeBinding>& InBindings)
{
	for (const FPCGExSketchModeBinding& Binding : InBindings)
	{
		if (UPCGExClusterSketchComponent* Component = Binding.Component.Get())
		{
			Component->SetEditState(FPCGExClusterSketchEditState());
		}
	}
}

void UPCGExSketchEditorMode::ReleaseBindings()
{
	ReleaseBindingsIn(Bindings);
	Bindings.Reset();
}

TSharedPtr<FPCGExSketchEditController> UPCGExSketchEditorMode::ResolveController(const FRay& WorldRay) const
{
	// 1. Nearest actual hit wins, so overlapping sketches disambiguate by depth.
	TSharedPtr<FPCGExSketchEditController> Best;
	double BestT = TNumericLimits<double>::Max();
	for (const FPCGExSketchModeBinding& Binding : Bindings)
	{
		if (!Binding.Controller)
		{
			continue;
		}
		const FPCGExSketchHit Hit = Binding.Controller->HitTest(WorldRay);
		if (Hit.IsHit() && Hit.RayT < BestT)
		{
			BestT = Hit.RayT;
			Best = Binding.Controller;
		}
	}
	if (Best)
	{
		return Best;
	}

	// 2. Nothing under the cursor: an empty-space gesture (Ctrl+click add) belongs to the sketch the
	// user is demonstrably working in.
	for (const FPCGExSketchModeBinding& Binding : Bindings)
	{
		if (Binding.Controller && Binding.Controller->HasSelection())
		{
			return Binding.Controller;
		}
	}

	// 3. Unambiguous by count, or nothing -- never guess between several idle sketches.
	return Bindings.Num() == 1 ? Bindings[0].Controller : nullptr;
}

void UPCGExSketchEditorMode::OnRenderCallback(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI ? RenderAPI->GetPrimitiveDrawInterface() : nullptr;
	if (!PDI)
	{
		return;
	}

	// The exact pass the standalone editor runs, once per editable sketch: it pushes edit state into
	// the component's mesh layer and draws only what the meshes do not cover.
	for (const FPCGExSketchModeBinding& Binding : Bindings)
	{
		if (Binding.Controller)
		{
			FPCGExSketchDrawHelper::DrawWithComponent(*Binding.Controller, Binding.Component.Get(), PDI);
		}
	}
}

bool UPCGExSketchEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	// Alt+LMB never reaches the ITF router (Alt starts camera tracking, which suppresses tools-context
	// routing), so the delete gesture rides this legacy path -- same reason, same call, as the
	// standalone editor's ProcessClick override.
	if (InputBinder && Click.GetKey() == EKeys::LeftMouseButton && Click.IsAltDown())
	{
		if (InputBinder->HandleAltClick(FRay(Click.GetOrigin(), Click.GetDirection())))
		{
			return true;
		}
	}
	return Super::HandleClick(InViewportClient, HitProxy, Click);
}

bool UPCGExSketchEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event == IE_Pressed && InputBinder && InputBinder->HandleKeyDown(Key))
	{
		return true;
	}
	return Super::InputKey(ViewportClient, Viewport, Key, Event);
}

#undef LOCTEXT_NAMESPACE
