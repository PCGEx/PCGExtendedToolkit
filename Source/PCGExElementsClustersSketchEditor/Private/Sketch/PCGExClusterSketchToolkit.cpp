// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchToolkit.h"

#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchComponent.h"
#include "Sketch/PCGExClusterSketchEditor.h"
#include "Sketch/PCGExClusterSketchStyle.h"
#include "Sketch/PCGExClusterSketchViewportClient.h"
#include "Sketch/PCGExSketchEditController.h"
#include "Sketch/SPCGExSketchPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SScrollBox.h"

FPCGExClusterSketchToolkit::FPCGExClusterSketchToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit(InOwningAssetEditor)
{
	// The base ctor already built a layout under its own generic name; rebuild under a unique one so
	// this editor's saved tab state never collides with another FBaseAssetToolkit-derived editor's.
	StandaloneDefaultLayout = FTabManager::NewLayout(FName("PCGExClusterSketchEditor_Layout_v1"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			                             ->Split
			                             (
				                             FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				                                                       ->Split
				                                                       (
					                                                       FTabManager::NewStack()
					                                                       ->SetSizeCoefficient(0.75f)
					                                                       ->AddTab(ViewportTabID, ETabState::OpenedTab)
					                                                       ->SetHideTabWell(true)
					                                                       )
				                                                       ->Split
				                                                       (
					                                                       FTabManager::NewStack()
					                                                       ->SetSizeCoefficient(0.25f)
					                                                       ->AddTab(DetailsTabID, ETabState::OpenedTab)
					                                                       ->SetHideTabWell(true)
					                                                       )
				                             )
			);

	ObjectScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

	// Hide the HDRI backdrop sphere and the checkered floor: sketch geometry is unlit foreground
	// drawing, so both only wash out the view. The dark clear color + ground grid (viewport client)
	// carry orientation instead.
	//
	// bDirect = TRUE is load-bearing: the default path writes bShowEnvironment / bShowFloor into
	// UAssetViewerSettings, which is shared editor-wide and saved to ini -- it would change every
	// other asset editor's preview scene. Direct only touches OUR components.
	ObjectScene->SetEnvironmentVisibility(false, /*bDirect*/ true);
	ObjectScene->SetFloorVisibility(false, /*bDirect*/ true);
}

FPCGExClusterSketchToolkit::~FPCGExClusterSketchToolkit()
{
}

void FPCGExClusterSketchToolkit::CreateWidgets()
{
	// The controller must exist before the base creates the viewport client that hosts it.
	UPCGExClusterSketch* Sketch = nullptr;
	if (const UPCGExClusterSketchEditor* SketchEditor = Cast<UPCGExClusterSketchEditor>(OwningAssetEditor))
	{
		Sketch = SketchEditor->GetSketch();
	}
	EditTarget = MakeShared<FPCGExSketchAssetEditTarget>(Sketch);
	Controller = MakeShared<FPCGExSketchEditController>(EditTarget.ToSharedRef());

	// Before the base builds the viewport client, which is handed the component to render through.
	CreatePreviewSketch(Sketch);

	FBaseAssetToolkit::CreateWidgets();
}

void FPCGExClusterSketchToolkit::CreatePreviewSketch(UPCGExClusterSketch* InSketch)
{
	UWorld* PreviewWorld = ObjectScene ? ObjectScene->GetWorld() : nullptr;
	if (!PreviewWorld || !InSketch)
	{
		return;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnInfo.bNoFail = true;
	SpawnInfo.ObjectFlags = RF_Transient;

	AActor* Actor = PreviewWorld->SpawnActor<AActor>(SpawnInfo);
	if (!Actor)
	{
		return;
	}
	Actor->SetActorEnableCollision(false);

	UPCGExClusterSketchComponent* Component = NewObject<UPCGExClusterSketchComponent>(Actor, NAME_None, RF_Transient);
	// Referencing the asset makes the component READ-ONLY by its own rule -- exactly right here: every
	// mutation goes through the controller's asset target, and this is purely the renderer.
	Component->SetSketchAsset(InSketch);
	Actor->SetRootComponent(Component);
	Component->RegisterComponent();

	// Registration builds PREVIEW styling; declaring the edit state up front skips a frame of it.
	FPCGExClusterSketchEditState State;
	State.bActive = true;
	Component->SetEditState(State);

	PreviewActor = Actor;
	PreviewComponent = Component;
}

void FPCGExClusterSketchToolkit::EnsurePanelCreated()
{
	if (Panel.IsValid())
	{
		return;
	}

	FPCGExSketchPanelContext Context;
	Context.ResolveActiveController = [this]()
	{
		return Controller;
	};
	Context.ResolveSketchObject = [this]() -> UObject*
	{
		const UPCGExClusterSketchEditor* SketchEditor = Cast<UPCGExClusterSketchEditor>(OwningAssetEditor);
		return SketchEditor ? SketchEditor->GetSketch() : nullptr;
	};
	Context.RequestBodyRefresh = FSimpleDelegate::CreateLambda([this]()
	{
		if (ViewportClient)
		{
			ViewportClient->Invalidate();
		}
	});

	// No enumerator: this host edits exactly one sketch, so the panel's picker stays collapsed.
	SAssignNew(Panel, SPCGExSketchPanel, Context);
}

TSharedRef<SDockTab> FPCGExClusterSketchToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	EnsurePanelCreated();

	// The panel IS this editor's details surface -- it already reaches the asset's snap provider and
	// decorators, so the base's DetailsView would only mirror it a second time.
	return SNew(SDockTab)
		.Label(INVTEXT("Details"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Panel->MakeHeader()
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				// The mode host supplies this itself; here the toolkit owns it.
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Panel->MakeBody()
				]
			]
		];
}

void FPCGExClusterSketchToolkit::SetEditingObject(UObject* /*InObject*/)
{
	// Deliberately empty. CreateWidgets check()s DetailsView into existence, but this editor never shows
	// it -- left unpopulated it stops rebuilding an invisible layout on every asset change.
}

void FPCGExClusterSketchToolkit::CreateEditorModeManager()
{
	EditorModeManager = MakeShared<FAssetEditorModeManager>();
	// The mode manager is the authority on what world the ITF context operates in; without this,
	// anything ITF spawns lands in the level editor world.
	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(ObjectScene.Get());
}

TSharedPtr<FEditorViewportClient> FPCGExClusterSketchToolkit::CreateEditorViewportClient() const
{
	return MakeShared<FPCGExClusterSketchViewportClient>(EditorModeManager.Get(), ObjectScene.Get(), Controller, PreviewComponent.Get());
}

void FPCGExClusterSketchToolkit::PostInitAssetEditor()
{
	FBaseAssetToolkit::PostInitAssetEditor();

	// The viewport tab must be live for the client (and the ITF context behind it) to tick.
	if (!TabManager->FindExistingLiveTab(ViewportTabID))
	{
		TabManager->TryInvokeTab(ViewportTabID);
	}

	// Frame the sketch (or a sane default volume for an empty one).
	FBox Bounds(ForceInit);
	if (const UPCGExClusterSketchEditor* SketchEditor = Cast<UPCGExClusterSketchEditor>(OwningAssetEditor))
	{
		if (const UPCGExClusterSketch* Sketch = SketchEditor->GetSketch())
		{
			FPCGExLatticeBasis Basis;
			const bool bHasBasis = Sketch->BuildBasis(Basis);
			for (const FPCGExClusterSketchVertex& V : Sketch->Model.Vertices)
			{
				Bounds += (V.bLatticeBound && bHasBasis) ? Basis.CoordToWorld(V.LatticeCoord) : V.Transform.GetLocation();
			}
		}
	}
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero())
	{
		Bounds = FBox(FVector(-300.0), FVector(300.0));
	}
	// Rotation FIRST: FocusViewportOnBox backs the camera along the CURRENT view direction, so this is
	// what turns framing into the canonical three-quarter view instead of the flat default. Same angle
	// the thumbnail projects along, so opening an asset shows what its thumbnail promised.
	ViewportClient->SetViewRotation(UPCGExClusterSketchStyleSettings::Get()->DefaultViewRotation);
	ViewportClient->FocusViewportOnBox(Bounds.ExpandBy(Bounds.GetExtent() * 0.25));

	if (ViewportClient->Viewport)
	{
		// Focused up front, or the viewport never ticks until the user clicks inside it.
		ViewportClient->ReceivedFocus(ViewportClient->Viewport);
	}
}
