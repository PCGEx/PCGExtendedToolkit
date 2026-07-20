// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionAssetDefinitionBase.h"

#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"

const FCollectionEditorTypeInfo* UAssetDefinition_PCGExCollectionBase::ResolveTypeInfo() const
{
	if (!CachedTypeInfo)
	{
		CachedTypeInfo = FCollectionEditorTypeRegistry::Get().FindByCollectionClass(GetAssetClass().Get());
	}
	return CachedTypeInfo;
}

FText UAssetDefinition_PCGExCollectionBase::GetAssetDisplayName() const
{
	if (const FCollectionEditorTypeInfo* Info = ResolveTypeInfo())
	{
		return Info->DisplayName;
	}
	return Super::GetAssetDisplayName();
}

FLinearColor UAssetDefinition_PCGExCollectionBase::GetAssetColor() const
{
	if (const FCollectionEditorTypeInfo* Info = ResolveTypeInfo())
	{
		return Info->AssetColor;
	}
	return Super::GetAssetColor();
}

FText UAssetDefinition_PCGExCollectionBase::GetAssetDescription(const FAssetData& AssetData) const
{
	if (const FCollectionEditorTypeInfo* Info = ResolveTypeInfo())
	{
		return Info->AssetDescription;
	}
	return Super::GetAssetDescription(AssetData);
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_PCGExCollectionBase::GetAssetCategories() const
{
	static const auto Categories = {FAssetCategoryPath(INVTEXT("PCGEx")) / INVTEXT("Collections")};
	return Categories;
}

EAssetCommandResult UAssetDefinition_PCGExCollectionBase::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	const FCollectionEditorTypeInfo* Info = ResolveTypeInfo();
	if (!Info || !Info->OpenEditor)
	{
		return EAssetCommandResult::Unhandled;
	}

	for (UPCGExAssetCollection* Collection : OpenArgs.LoadObjects<UPCGExAssetCollection>())
	{
		if (!Collection)
		{
			continue;
		}
		Info->OpenEditor(Collection, OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost);
	}
	return EAssetCommandResult::Handled;
}
