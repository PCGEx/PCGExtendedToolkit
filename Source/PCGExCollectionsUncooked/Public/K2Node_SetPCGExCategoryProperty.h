// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node_PCGExPropertyBase.h"

#include "K2Node_SetPCGExCategoryProperty.generated.h"

class FBlueprintActionDatabaseRegistrar;

/** Writes a category's custom property override (minting the row on demand) and reads back the resolved value. See UK2Node_PCGExPropertyBase. */
UCLASS(meta=(DisplayName="Set Category Property", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/collection-accessor-nodes"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_SetPCGExCategoryProperty : public UK2Node_PCGExPropertyBase
{
	GENERATED_BODY()

public:
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

protected:
	virtual bool IsSetNode() const override
	{
		return true;
	}

	virtual EScope GetScope() const override
	{
		return EScope::Category;
	}
};
