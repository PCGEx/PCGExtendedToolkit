// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node_PCGExMemberBase.h"

#include "K2Node_SetPCGExCollectionMember.generated.h"

class FBlueprintActionDatabaseRegistrar;

/** Writes a reflected member on the collection object itself (impure). See UK2Node_PCGExMemberBase. */
UCLASS(meta=(DisplayName="Set Collection Member", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/set-collection-member"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_SetPCGExCollectionMember : public UK2Node_PCGExMemberBase
{
	GENERATED_BODY()

public:
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

protected:
	virtual bool IsSetNode() const override
	{
		return true;
	}

	virtual bool IsEntryScoped() const override
	{
		return false;
	}

	virtual FName GetWildcardFunctionName() const override;
	virtual FName GetSoftObjectFunctionName() const override;
	virtual FName GetSoftClassFunctionName() const override;
	virtual FText GetBaseTitle() const override;
};
