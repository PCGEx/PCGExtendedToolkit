// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyCage.h"

#include "PCGExValencyCageCustom.generated.h"

/**
 * A Valency cage with no built-in containment detection.
 * Designed for Blueprint subclassing — override IsActorInside and ContainsPoint
 * in the Blueprint event graph to implement custom containment logic.
 *
 * Default containment returns false (no assets detected).
 */
UCLASS(Blueprintable, meta = (DisplayName = "Valency Cage (Custom)"))
class PCGEXELEMENTSVALENCYEDITOR_API APCGExValencyCageCustom : public APCGExValencyCage
{
	GENERATED_BODY()

public:
	APCGExValencyCageCustom();

	//~ Begin APCGExValencyCageBase Interface
	virtual FString GetCageDisplayName() const override;
	//~ End APCGExValencyCageBase Interface
};
