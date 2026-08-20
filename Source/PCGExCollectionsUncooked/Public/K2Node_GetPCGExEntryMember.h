// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node_PCGExMemberBase.h"

#include "K2Node_GetPCGExEntryMember.generated.h"

class FBlueprintActionDatabaseRegistrar;

/** Reads a reflected member from a collection entry by raw index (pure). See UK2Node_PCGExMemberBase. */
UCLASS(meta=(DisplayName="Get Entry Member", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/get-entry-member"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_GetPCGExEntryMember : public UK2Node_PCGExMemberBase
{
	GENERATED_BODY()

public:
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

protected:
	virtual bool IsSetNode() const override
	{
		return false;
	}

	virtual bool IsEntryScoped() const override
	{
		return true;
	}

	virtual FName GetWildcardFunctionName() const override;
	virtual FName GetSoftObjectFunctionName() const override;
	virtual FName GetSoftClassFunctionName() const override;
	virtual FText GetBaseTitle() const override;
};
