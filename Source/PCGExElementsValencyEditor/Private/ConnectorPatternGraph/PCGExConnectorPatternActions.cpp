// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternActions.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternEditor.h"
#include "Core/PCGExConnectorPatternAsset.h"

#pragma region FPCGExConnectorPatternActions

FText FPCGExConnectorPatternActions::GetName() const
{
	return INVTEXT("PCGEx Valency | Connector Pattern");
}

FString FPCGExConnectorPatternActions::GetObjectDisplayName(UObject* Object) const
{
	return Object->GetName();
}

UClass* FPCGExConnectorPatternActions::GetSupportedClass() const
{
	return UPCGExConnectorPatternAsset::StaticClass();
}

FColor FPCGExConnectorPatternActions::GetTypeColor() const
{
	return FColor(180, 100, 220);
}

uint32 FPCGExConnectorPatternActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

bool FPCGExConnectorPatternActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return false;
}

void FPCGExConnectorPatternActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (UObject* Obj : InObjects)
	{
		if (UPCGExConnectorPatternAsset* Asset = Cast<UPCGExConnectorPatternAsset>(Obj))
		{
			TSharedRef<FPCGExConnectorPatternEditor> Editor = MakeShared<FPCGExConnectorPatternEditor>();
			Editor->InitEditor(Asset, EToolkitMode::Standalone, EditWithinLevelEditor);
		}
	}
}

#pragma endregion
