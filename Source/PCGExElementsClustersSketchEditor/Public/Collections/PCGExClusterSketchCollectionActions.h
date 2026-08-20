// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExDataAssetFactory.h"
#include "AssetRegistry/AssetData.h"
#include "Collections/PCGExClusterSketchCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"

#include "PCGExClusterSketchCollectionActions.generated.h"

namespace PCGExClusterSketchCollectionActions
{
	/**
	 * Registers the ClusterSketch editor type and its tile picker.
	 *
	 * MUST be called from StartupModule, for the same reason as PCGExSketch::RegisterCollectionType:
	 * PCGExCollectionsEditor flushes FCollectionEditorTypeRegistry in ITS StartupModule, which runs
	 * before this module's static init, so a static auto-registrar would resolve StaticClass() during
	 * static initialization and crash.
	 */
	void RegisterEditorType();
};

UCLASS()
class UPCGExClusterSketchCollectionFactory : public UPCGExDataAssetFactoryBase
{
	GENERATED_BODY()

public:
	UPCGExClusterSketchCollectionFactory()
	{
		SupportedClass = UPCGExClusterSketchCollection::StaticClass();
	}
};

UCLASS()
class UAssetDefinition_PCGExClusterSketchCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExClusterSketchCollection::StaticClass();
	}
};
