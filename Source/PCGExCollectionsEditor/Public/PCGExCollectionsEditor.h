// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"
#include "UObject/SoftObjectPath.h"

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
	bool bThumbnailRendererRegistered = false;

	void OnFilesLoaded();

	// The three staleness triggers: source re-saved, Blueprint recompiled, collection loaded.
	void OnAssetUpdatedOnDisk(const FAssetData& AssetData);
	void OnObjectsReinstanced(const TMap<UObject*, UObject*>& OldToNewMap);
	void OnAssetLoaded(UObject* InObject);

	void RegisterThumbnailRenderer();
};
