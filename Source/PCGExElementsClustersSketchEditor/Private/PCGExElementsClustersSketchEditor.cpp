// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClustersSketchEditor.h"

#include "AssetToolsModule.h"
#include "Collections/PCGExClusterSketchCollectionActions.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "IAssetTools.h"
#include "Engine/Engine.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "Misc/CoreDelegates.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchFactories.h"
#include "Sketch/PCGExClusterSketchThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UObject/UObjectGlobals.h"

namespace PCGExElementsClustersSketchEditor
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

void FPCGExElementsClustersSketchEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();
	// Factories and asset definitions self-register through the AssetDefinition subsystem.

	// Out-of-module collection type: PCGExCollectionsEditor flushed its registry in its own
	// StartupModule, so this cannot ride a static auto-registrar.
	PCGExClusterSketchCollectionActions::RegisterEditorType();

	// Topology thumbnails for sketch assets. GEngine != null means engine init is done and
	// UThumbnailManager is safe to touch; otherwise defer to PostEngineInit.
	if (GEngine)
	{
		RegisterThumbnailRenderer();
	}
	else
	{
		OnPostEngineInitHandle = PCGExElementsClustersSketchEditor::OnPostEngineInit().AddRaw(this, &FPCGExElementsClustersSketchEditorModule::RegisterThumbnailRenderer);
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

void FPCGExElementsClustersSketchEditorModule::RegisterThumbnailRenderer()
{
	UThumbnailManager::Get().RegisterCustomRenderer(UPCGExClusterSketch::StaticClass(), UPCGExClusterSketchThumbnailRenderer::StaticClass());
	bThumbnailRendererRegistered = true;
}

void FPCGExElementsClustersSketchEditorModule::ShutdownModule()
{
	PCGExSketch::GSaveSketchAsAssetFn = nullptr;

	// The registry outlives this module and holds closures compiled into its DLL.
	FCollectionEditorTypeRegistry::Get().Unregister(PCGExSketch::CollectionTypeId);

	PCGExElementsClustersSketchEditor::OnPostEngineInit().Remove(OnPostEngineInitHandle);

	if (bThumbnailRendererRegistered && UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPCGExClusterSketch::StaticClass());
	}

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersSketchEditorModule, PCGExElementsClustersSketchEditor)
