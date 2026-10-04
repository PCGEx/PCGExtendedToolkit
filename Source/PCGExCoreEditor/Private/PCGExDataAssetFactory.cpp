// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExDataAssetFactory.h"

#include "HAL/IConsoleManager.h"

namespace PCGExDataAssetFactoryCVars
{
	bool bShowAllFactories = false;
	FAutoConsoleVariableRef CVarShowAllFactories(
		TEXT("pcgex.Collections.ShowAllFactories"),
		bShowAllFactories,
		TEXT("Show every PCGEx collection type in the content browser's New Asset menu, including the ones hidden by default."));
}

bool UPCGExDataAssetFactoryBase::ShouldShowInNewMenu() const
{
	return Super::ShouldShowInNewMenu() && (!bHiddenFromNewMenu || PCGExDataAssetFactoryCVars::bShowAllFactories);
}
