// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

#include "Collections/PCGExMeshCollection.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetData.h"

class UPackage;

namespace PCGExMeshCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets);
	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExMeshCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets,
		bool bIsNewCollection = false);
};

/** Asset type actions for mesh collections — registers editor and context menu actions. Reference implementation. */
class FPCGExMeshCollectionActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual FString GetObjectDisplayName(UObject* Object) const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
	virtual uint32 GetCategories() override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;

	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;
};
