// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/K2Node_PCGExComponentPropertyBase.h"

#include "K2Node_GetPCGExProperty.generated.h"

/** Reads a named property from a UPCGExPropertyCollectionComponent (pure). See UK2Node_PCGExPropertyAccessBase. */
UCLASS()
class PCGEXPROPERTIESEDITOR_API UK2Node_GetPCGExProperty : public UK2Node_PCGExComponentPropertyBase
{
	GENERATED_BODY()

protected:
	virtual bool IsSetNode() const override
	{
		return false;
	}
};
