// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExVariantCollectionActions.h"

#include "Collections/PCGExVariantCollection.h"
#include "Details/Collections/PCGExVariantCollectionEditor.h"

EAssetCommandResult UAssetDefinition_PCGExVariantCollection::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UPCGExVariantCollection* Collection : OpenArgs.LoadObjects<UPCGExVariantCollection>())
	{
		TSharedRef<FPCGExVariantCollectionEditor> Editor = MakeShared<FPCGExVariantCollectionEditor>();
		Editor->InitEditor(Collection, OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost);
	}
	return EAssetCommandResult::Handled;
}
