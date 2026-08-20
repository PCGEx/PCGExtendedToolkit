// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExGraphsEditor.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/Engine.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "Misc/CoreDelegates.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchFactories.h"
#include "Sketch/PCGExClusterSketchThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UObject/UObjectGlobals.h"

namespace PCGExGraphsEditor
{
	/** FCoreDelegates::OnPostEngineInit became an accessor in 5.8, where the member itself is deprecated. */
	FSimpleMulticastDelegate& OnPostEngineInit()
	{
#if PCGEX_ENGINE_VERSION >= 508
		return FCoreDelegates::GetOnPostEngineInit();
#else
		return FCoreDelegates::OnPostEngineInit;
#endif
	}
}

void FPCGExGraphsEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();
	// Factories and asset definitions self-register through the AssetDefinition subsystem.

	// Topology thumbnails for sketch assets. GEngine != null means engine init is done and
	// UThumbnailManager is safe to touch; otherwise defer to PostEngineInit.
	if (GEngine)
	{
		RegisterThumbnailRenderer();
	}
	else
	{
		OnPostEngineInitHandle = PCGExGraphsEditor::OnPostEngineInit().AddRaw(this, &FPCGExGraphsEditorModule::RegisterThumbnailRenderer);
	}

	// Cross-module bridge: the sketch HOSTS are runtime (asset, component), but creating an asset needs
	// the save dialog + package machinery, which only exists here.
	PCGExSketch::GSaveSketchAsAssetFn = [](
		const FPCGExClusterSketchModel& InModel,
		const UPCGExClusterSnapProvider* InSnapProvider,
		const TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> InDecorators,
		const FString& InDefaultAssetName) -> UPCGExClusterSketch*
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UPCGExClusterSketchFactory* Factory = NewObject<UPCGExClusterSketchFactory>();
		UPCGExClusterSketch* NewAsset = Cast<UPCGExClusterSketch>(
			AssetTools.CreateAssetWithDialog(InDefaultAssetName, TEXT("/Game"), UPCGExClusterSketch::StaticClass(), Factory));

		if (!NewAsset)
		{
			return nullptr; // cancelled
		}

		NewAsset->Model = InModel;

		// Duplicated, never shared: the source keeps its own instanced subobjects, and the asset owns
		// copies outered to itself (a shared subobject would serialize into whichever package won).
		NewAsset->SnapProvider = InSnapProvider ? DuplicateObject<UPCGExClusterSnapProvider>(InSnapProvider, NewAsset) : nullptr;
		NewAsset->Decorators.Reset(InDecorators.Num());
		for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : InDecorators)
		{
			NewAsset->Decorators.Add(Decorator ? DuplicateObject<UPCGExClusterSketchDecorator>(Decorator, NewAsset) : nullptr);
		}

		// The factory created it empty; the payload landed after, so the package must be dirtied here.
		NewAsset->MarkPackageDirty();
		PCGExEditor::NotifyObjectChanged(NewAsset);
		return NewAsset;
	};
}

void FPCGExGraphsEditorModule::RegisterThumbnailRenderer()
{
	UThumbnailManager::Get().RegisterCustomRenderer(UPCGExClusterSketch::StaticClass(), UPCGExClusterSketchThumbnailRenderer::StaticClass());
	bThumbnailRendererRegistered = true;
}

void FPCGExGraphsEditorModule::ShutdownModule()
{
	PCGExSketch::GSaveSketchAsAssetFn = nullptr;

	PCGExGraphsEditor::OnPostEngineInit().Remove(OnPostEngineInitHandle);

	if (bThumbnailRendererRegistered && UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPCGExClusterSketch::StaticClass());
	}

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExGraphsEditorModule, PCGExGraphsEditor)
