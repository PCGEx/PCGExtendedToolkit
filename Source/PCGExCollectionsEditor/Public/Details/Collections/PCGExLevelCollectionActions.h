// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Collections/PCGExLevelCollection.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetData.h"

class UPackage;

namespace PCGExLevelCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets);
	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExLevelCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets,
		bool bIsNewCollection = false);
};

/**
 *
 */
class FPCGExLevelCollectionActions : public FAssetTypeActions_Base
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
