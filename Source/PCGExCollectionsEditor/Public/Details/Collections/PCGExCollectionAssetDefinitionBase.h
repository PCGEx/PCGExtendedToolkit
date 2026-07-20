// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"

#include "PCGExCollectionAssetDefinitionBase.generated.h"

struct FCollectionEditorTypeInfo;

/**
 * Shared base for all UAssetDefinition_PCGEx*Collection concrete definitions.
 *
 * Subclasses override only GetAssetClass() to identify themselves; display name, color,
 * description, and OpenAssets behavior all resolve through the FCollectionEditorTypeRegistry
 * keyed by GetAssetClass(). The TypeInfo pointer is cached on the CDO after first resolution
 * since the registry is immutable post-ProcessPendingRegistrations.
 */
UCLASS(Abstract)
class PCGEXCOLLECTIONSEDITOR_API UAssetDefinition_PCGExCollectionBase : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;

private:
	const FCollectionEditorTypeInfo* ResolveTypeInfo() const;

	mutable const FCollectionEditorTypeInfo* CachedTypeInfo = nullptr;
};
