// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "PCGExExternalPackageProducer.generated.h"

class UPackage;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPCGExExternalPackageProducer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Marks an asset whose rebuilds write generated content into EXTERNAL packages (e.g. the
 * PCGDataAsset collection machinery's shared/exported assets, the Valency bonding rules'
 * generated collection).
 *
 * Rebuilds dirty those packages but a user save targeting only the host asset would leave
 * them skewed on disk. The PCGExCollectionsEditor module listens to package saves: when a
 * saved package hosts an implementer, the implementer's dirty external packages are saved
 * alongside it (silent, checkout-aware -- same policy as OFPA external actors saving with
 * their map). Cook and other procedural saves never trigger this.
 *
 * Implementations should be cheap and read-only: report the LOADED packages your generated
 * external assets currently live in (accumulate into OutPackages, do not clear). Skip
 * nothing else -- the caller filters out the host's own package, the transient package and
 * anything not dirty.
 */
class PCGEXFOUNDATIONS_API IPCGExExternalPackageProducer
{
	GENERATED_BODY()

public:
	/**
	 * Editor-save coordination only: pure virtual in editor builds (implementers must
	 * override), empty default otherwise so the inheritance carries no obligation.
	 */
#if WITH_EDITOR
	virtual void EDITOR_GetExternalPackages(TSet<UPackage*>& OutPackages) const = 0;
#else
	virtual void EDITOR_GetExternalPackages(TSet<UPackage*>& OutPackages) const
	{
	}
#endif
};
