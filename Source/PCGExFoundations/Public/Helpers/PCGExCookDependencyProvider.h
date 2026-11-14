// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExCookDependencyProvider.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPCGExCookDependencyProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Marks an asset that wants its soft references force-cooked alongside itself.
 *
 * The cooker only follows hard references by default; soft refs held by a UDataAsset
 * are skipped unless explicitly added to the cook plan. Implementers expose their
 * soft refs via GetCookDependencyAssetPaths; the PCGExtendedToolkit ModifyCook
 * delegate scans every mount point for implementers, force-cooks each implementer's
 * own package, then force-cooks every path the implementer reports.
 *
 * Implementations should be cheap and pure -- the method runs during the cook
 * commandlet, after the asset has been loaded, and must not mutate state.
 */
class PCGEXFOUNDATIONS_API IPCGExCookDependencyProvider
{
	GENERATED_BODY()

public:
	/**
	 * Append every soft path this asset wants cooked alongside itself.
	 * Implementations should accumulate into OutPaths (do not clear) so the
	 * caller can batch results across multiple providers.
	 *
	 * Cook-time only: pure virtual in editor builds (implementers must override),
	 * empty default in non-editor builds so the inheritance carries no obligation.
	 * Runtime callers shouldn't reach this -- the only sanctioned call site is
	 * the PCGExtendedToolkit ModifyCook delegate, which is itself WITH_EDITOR.
	 */
#if WITH_EDITOR
	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const = 0;
#else
	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const {}
#endif

	/** False opts this asset out of the force-cook: neither its package nor the paths it reports are added. */
	virtual bool IsCookedForRuntime() const { return true; }
};
