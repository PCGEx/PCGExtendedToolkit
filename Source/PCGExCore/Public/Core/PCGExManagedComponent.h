// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGManagedResource.h"

#include "PCGExManagedComponent.generated.h"

/** Managed component that outlives the soft release opening a regeneration, for FPCGExContext::ReclaimManagedComponent to pick up. */
UCLASS()
class PCGEXCORE_API UPCGExManagedComponent : public UPCGManagedComponent
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedComponentBase interface
	virtual bool SupportsComponentReset() const override { return true; }

	// Nothing to reset: a reclaimed component is rebuilt in full by the node that attached it.
	virtual void ResetComponent() override
	{
	}

	//~End UPCGManagedComponentBase interface
};
