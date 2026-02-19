// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/**
 * Asset type actions for UPCGExConnectorPatternAsset.
 * Opens the graph-based pattern editor instead of the default property panel.
 */
class PCGEXELEMENTSVALENCYEDITOR_API FPCGExConnectorPatternActions : public FAssetTypeActions_Base
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
