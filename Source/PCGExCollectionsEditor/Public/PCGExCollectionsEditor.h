// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssemblyRoot/PCGExAssemblyRootEditorHost.h" // complete type: TUniquePtr member needs it wherever the module is destroyed
#include "PCGExEditorModuleInterface.h"
#include "PCGExMediatorRegistry.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UPackage;
class UPCGExAssetCollection;
class UPCGExPropertySchemaAsset;

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
	FDelegateHandle OnAnySchemaAssetChangedHandle;
	bool bThumbnailRendererRegistered = false;

	/** JSON authoring surface (PCGExMediator): the formats and bindings this module registers. */
	FPCGExMediatorDomain MediatorDomain;

	/** Select-as-unit latch tracker + viewport action bar for the stock assembly root. */
	TUniquePtr<FPCGExAssemblyRootEditorHost> AssemblyRootHost;

	// Coordinated external-package save (IPCGExExternalPackageProducer): packages queued by
	// OnPackageSaved, flushed once next tick (saving is illegal inside the save callback).
	TSet<TWeakObjectPtr<UPackage>> PendingExternalPackageSaves;
	bool bExternalSaveFlushScheduled = false;
	bool bIsSavingExternalPackages = false;

	/** Source packages a burst touched, for one loaded collection referencing them. */
	struct FPendingSourceRestage
	{
		/** Changed on disk: an entry re-stages only if its source digest moved off its baseline. */
		TSet<FName> Saved;
		/** Recompiled in memory (Blueprint reinstancing): the on-disk digest can't see it, so always. */
		TSet<FName> Reinstanced;
	};

	// Source-change restage, coalesced per burst: the two source triggers only queue here, and one
	// flush runs once the burst has gone quiet (frame and wall clock, see FlushPendingSourceRestages).
	TMap<TWeakObjectPtr<UPCGExAssetCollection>, FPendingSourceRestage> PendingSourceRestages;
	double FirstSourceRestageQueueTime = 0;
	double LastSourceRestageQueueTime = 0;
	uint64 LastSourceRestageQueueFrame = 0;
	bool bSavedSourcePending = false;
	bool bSourceRestageFlushScheduled = false;

	// Loaded collections whose stale check waits for the registry to stop gathering.
	TArray<TWeakObjectPtr<UPCGExAssetCollection>> PendingStaleChecks;
	bool bStaleCheckFlushScheduled = false;

	void OnFilesLoaded();

	// The three staleness triggers: source re-saved, Blueprint recompiled, collection loaded.
	void OnAssetUpdatedOnDisk(const FAssetData& AssetData);
	void OnObjectsReinstanced(const TMap<UObject*, UObject*>& OldToNewMap);
	void OnAssetLoaded(UObject* InObject);

	void QueueSourceRestage(const FAssetData& AssetData, bool bReinstanced);
	void FlushPendingSourceRestages();
	void RestagePendingSources();
	void FlushPendingStaleChecks();

	/** Reconciles every loaded collection importing the changed schema asset (any depth), so
	 *  propagation never depends on a details customization being alive to relay the broadcast. */
	void OnAnySchemaAssetChanged(UPCGExPropertySchemaAsset* Asset);

	void OnPackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext Context);
	void FlushPendingExternalPackageSaves();

	void RegisterThumbnailRenderer();
};
