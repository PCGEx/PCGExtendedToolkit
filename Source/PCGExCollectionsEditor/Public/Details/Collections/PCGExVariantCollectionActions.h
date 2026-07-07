// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExVariantCollection.h"

#include "PCGExVariantCollectionActions.generated.h"

UCLASS()
class UPCGExVariantCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExVariantCollectionFactory()
	{
		SupportedClass = UPCGExVariantCollection::StaticClass();
	}
};

// Variant collections are asset-only: they surface via the factory's "Create Asset" menu and
// their own editor, but opt out of the content-browser "Create from selection" flow entirely
// (no source-asset detection / CreateCollectionFrom). Same shape as PCGDataAsset collections.
UCLASS()
class UAssetDefinition_PCGExVariantCollection : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override
	{
		return INVTEXT("Variant Collection");
	}

	virtual FLinearColor GetAssetColor() const override
	{
		return FLinearColor(FColor(200, 120, 220));
	}

	virtual FText GetAssetDescription(const FAssetData& AssetData) const override
	{
		return INVTEXT("Per-entry overrides for one or more source collections, for end-of-pipeline asset swapping (biomes, themes).");
	}

	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExVariantCollection::StaticClass();
	}

	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = {FAssetCategoryPath(INVTEXT("PCGEx")) / INVTEXT("Collections")};
		return Categories;
	}

	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
