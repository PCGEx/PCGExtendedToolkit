// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExDataAssetFactory.h"
#include "Collections/PCGExVariantCollection.h"
#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"

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

UCLASS()
class UAssetDefinition_PCGExVariantCollection : public UAssetDefinition_PCGExCollectionBase
{
	GENERATED_BODY()

public:
	virtual TSoftClassPtr<UObject> GetAssetClass() const override
	{
		return UPCGExVariantCollection::StaticClass();
	}
};
