// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UPackage;

class FPCGExCollectionsEditorModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual void RegisterMenuExtensions() override;

private:
	FDelegateHandle OnFilesLoadedHandle;
	FDelegateHandle OnAssetUpdatedOnDiskHandle;
	FDelegateHandle OnObjectsReinstancedHandle;
	FDelegateHandle OnAssetLoadedHandle;
	FDelegateHandle OnPostEngineInitHandle;
	FDelegateHandle OnPackageSavedHandle;
	bool bThumbnailRendererRegistered = false;

	// Coordinated external-package save (IPCGExExternalPackageProducer): packages queued by
	// OnPackageSaved, flushed once next tick (saving is illegal inside the save callback).
	TSet<TWeakObjectPtr<UPackage>> PendingExternalPackageSaves;
	bool bExternalSaveFlushScheduled = false;
	bool bIsSavingExternalPackages = false;

	void OnFilesLoaded();

	// The three staleness triggers: source re-saved, Blueprint recompiled, collection loaded.
	void OnAssetUpdatedOnDisk(const FAssetData& AssetData);
	void OnObjectsReinstanced(const TMap<UObject*, UObject*>& OldToNewMap);
	void OnAssetLoaded(UObject* InObject);

	void OnPackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext Context);
	void FlushPendingExternalPackageSaves();

	void RegisterThumbnailRenderer();
};
