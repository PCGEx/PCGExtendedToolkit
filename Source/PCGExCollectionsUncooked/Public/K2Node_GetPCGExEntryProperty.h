// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node_PCGExPropertyBase.h"

#include "K2Node_GetPCGExEntryProperty.generated.h"

class FBlueprintActionDatabaseRegistrar;

/** Reads an entry's resolved custom property by raw index (pure). See UK2Node_PCGExPropertyBase. */
UCLASS(meta=(DisplayName="Get Entry Property", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/collection-accessor-nodes"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_GetPCGExEntryProperty : public UK2Node_PCGExPropertyBase
{
	GENERATED_BODY()

public:
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

protected:
	virtual bool IsSetNode() const override
	{
		return false;
	}

	virtual EScope GetScope() const override
	{
		return EScope::Entry;
	}
};
