// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "PCGExDataAssetFactory.h"
#include "Data/Bitmasks/PCGExBitmaskCollection.h"

#include "PCGExBitmaskCollectionFactory.generated.h"

UCLASS()
class UPCGExBitmaskCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExBitmaskCollectionFactory()
	{
		SupportedClass = UPCGExBitmaskCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExBitmaskCollection : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override
	{
		return INVTEXT("Bitmask Collection");
	}

	virtual FLinearColor GetAssetColor() const override
	{
		return FLinearColor(FColor(195, 0, 40));
	}

	virtual FText GetAssetDescription(const FAssetData& AssetData) const override
	{
		return INVTEXT("A library of named bitmask values for tagging and adjacency testing.");
	}

	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExBitmaskCollection::StaticClass();
	}

	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = {FAssetCategoryPath(INVTEXT("PCGEx")) / INVTEXT("Core")};
		return Categories;
	}
};
