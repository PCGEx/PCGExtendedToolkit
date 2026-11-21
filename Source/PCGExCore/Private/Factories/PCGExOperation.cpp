// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExOperation.h"

#include "Data/PCGExData.h"

void FPCGExOperation::BindContext(FPCGExContext* InContext)
{
	Context = InContext;
}

#if WITH_EDITOR
void FPCGExOperation::UpdateUserFacingInfos()
{
}
#endif

void FPCGExOperation::RegisterConsumableAttributesWithFacade(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade) const
{
}

void FPCGExOperation::RegisterPrimaryBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) const
{
}

TSharedPtr<PCGExMT::FScopedContainer> FPCGExOperation::GetScopedContainer(const PCGExMT::FScope& InScope) const
{
	return nullptr;
}

void FPCGExOperation::RegisterAssetDependencies(FPCGExContext* InContext)
{
}
