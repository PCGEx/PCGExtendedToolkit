// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlVtxFilters.h"


#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Core/PCGExFilterTypeSets.h"

bool FPCGExFillControlVtxFilters::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryVtxFilters* TypedFactory = Cast<UPCGExFillControlsFactoryVtxFilters>(Factory);

	VtxFilterManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), InHandler->VtxDataFacade.ToSharedRef(), InHandler->EdgeDataFacade.ToSharedRef());
	VtxFilterManager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
	return VtxFilterManager->Init(InContext, TypedFactory->FilterFactories);
}

bool FPCGExFillControlVtxFilters::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return VtxFilterManager->Test(*Candidate.Node);
}

bool FPCGExFillControlVtxFilters::IsValidProbe(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	if (Candidate.Link.Edge == -1)
	{
		return true;
	} // That's the seed
	return VtxFilterManager->Test(*Candidate.Node);
}

bool FPCGExFillControlVtxFilters::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	return VtxFilterManager->Test(*Candidate.Node);
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryVtxFilters::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlVtxFilters)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryVtxFilters::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : FilterFactories)
	{
		Factory->RegisterBuffersDependencies(InContext, FacadePreloader);
	}
}

bool UPCGExFillControlsFactoryVtxFilters::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	if (!Super::RegisterConsumableAttributes(InContext))
	{
		return false;
	}

	for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : FilterFactories)
	{
		if (!Factory->RegisterConsumableAttributes(InContext))
		{
			return false;
		}
	}

	return true;
}

bool UPCGExFillControlsFactoryVtxFilters::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : FilterFactories)
	{
		if (!Factory->RegisterConsumableAttributesWithData(InContext, InData))
		{
			return false;
		}
	}

	return true;
}

TArray<FPCGPinProperties> UPCGExFillControlsVtxFiltersProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceVtxFiltersLabel, TEXT("Filters used on edges."), Required)
	return PinProperties;
}

UPCGExFactoryData* UPCGExFillControlsVtxFiltersProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryVtxFilters* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryVtxFilters>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	Super::CreateFactory(InContext, NewFactory);

	if (!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceVtxFiltersLabel, NewFactory->FilterFactories, PCGExFactories::ClusterNodeFilters()))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	return NewFactory;
}

#if WITH_EDITOR
FString UPCGExFillControlsVtxFiltersProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC"));
}
#endif
