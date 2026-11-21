// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/K2Node_PCGExPropertyAccessBase.h"

#include "K2Node_PCGExComponentPropertyBase.generated.h"

/**
 * The custom-property access nodes over a UPCGExPropertyCollectionComponent: Get / Set Property.
 * Leaves declare only direction. Backed by UPCGExPropertyBlueprintLibrary.
 */
UCLASS(Abstract)
class PCGEXPROPERTIESEDITOR_API UK2Node_PCGExComponentPropertyBase : public UK2Node_PCGExPropertyAccessBase
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UK2Node
	virtual FText GetMenuCategory() const override;

protected:
	// UK2Node_PCGExPropertyAccessBase
	virtual FName GetTargetPinName() const override;
	virtual UClass* GetTargetPinClass() const override;
	virtual UClass* GetLibraryClass() const override;
	virtual FName GetLibraryFunctionName(bool bSet, EFlavor Flavor) const override;
};
