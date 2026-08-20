// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "PCGExDataAssetFactory.h"
#include "Sketch/PCGExClusterSketch.h"

#include "PCGExClusterSketchFactories.generated.h"

UCLASS()
class UPCGExClusterSketchFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExClusterSketchFactory()
	{
		SupportedClass = UPCGExClusterSketch::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExClusterSketch : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override { return INVTEXT("Cluster Sketch"); }

	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.1f, 0.75f, 0.65f); }

	virtual FText GetAssetDescription(const FAssetData& AssetData) const override
	{
		return INVTEXT("A hand-authored, spawnable cluster: vertices + edges + annotation channels, printed to a live Vtx/Edges pair at execute time.");
	}

	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UPCGExClusterSketch::StaticClass(); }

	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = {FAssetCategoryPath(INVTEXT("PCGEx")) / INVTEXT("Core")};
		return Categories;
	}

	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
