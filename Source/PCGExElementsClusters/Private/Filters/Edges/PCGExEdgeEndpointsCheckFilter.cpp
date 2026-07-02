// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeEndpointsCheckFilter.h"


#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Graphs/PCGExGraph.h"
#include "HAL/PlatformAtomics.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeEndpointsCheckFilter"
#define PCGEX_NAMESPACE EdgeEndpointsCheckFilter

void UPCGExEdgeEndpointsCheckFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	for (const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* FactorySet : {&FilterFactories, &FilterFactoriesB})
	{
		for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : *FactorySet)
		{
			Factory->RegisterBuffersDependencies(InContext, FacadePreloader);
		}
	}
}

bool UPCGExEdgeEndpointsCheckFilterFactory::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	if (!Super::RegisterConsumableAttributes(InContext))
	{
		return false;
	}

	for (const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* FactorySet : {&FilterFactories, &FilterFactoriesB})
	{
		for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : *FactorySet)
		{
			if (!Factory->RegisterConsumableAttributes(InContext))
			{
				return false;
			}
		}
	}

	return true;
}

bool UPCGExEdgeEndpointsCheckFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	for (const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* FactorySet : {&FilterFactories, &FilterFactoriesB})
	{
		for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : *FactorySet)
		{
			if (!Factory->RegisterConsumableAttributesWithData(InContext, InData))
			{
				return false;
			}
		}
	}

	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeEndpointsCheckFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeEndpointsCheck::FFilter>(this);
}

namespace PCGExEdgeEndpointsCheck
{
	// Resolves a node's filter result against the given manager, lazily caching it (-1 == not yet computed).
	FORCEINLINE int8 ResolveCachedResult(const TSharedPtr<PCGExClusterFilter::FManager>& Manager, TArray<int8>& Cache, const PCGExClusters::FNode* Node)
	{
		int8 Result = Cache[Node->Index]; // TODO Atomic read?
		if (Result == -1)
		{
			Result = Manager->Test(*Node);
			FPlatformAtomics::AtomicStore(&Cache[Node->Index], Result);
		}
		return Result;
	}

	bool FFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		VtxFiltersManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), InPointDataFacade, InEdgeDataFacade);
		VtxFiltersManager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
		if (!VtxFiltersManager->Init(InContext, TypedFilterFactory->FilterFactories))
		{
			return false;
		}

		ResultCache.Init(-1, Cluster->Nodes->Num());

		Expected = TypedFilterFactory->Config.Expects == EPCGExFilterResult::Fail ? 0 : 1;

		if (TypedFilterFactory->Config.bUseTwoFilterSets)
		{
			VtxFiltersManagerB = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), InPointDataFacade, InEdgeDataFacade);
			VtxFiltersManagerB->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
			if (!VtxFiltersManagerB->Init(InContext, TypedFilterFactory->FilterFactoriesB))
			{
				return false;
			}

			ResultCacheB.Init(-1, Cluster->Nodes->Num());

			ExpectedB = TypedFilterFactory->Config.ExpectsB == EPCGExFilterResult::Fail ? 0 : 1;
		}

		return true;
	}

	bool FFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		const PCGExClusters::FNode* Start = Cluster->GetEdgeStart(Edge);
		const PCGExClusters::FNode* End = Cluster->GetEdgeEnd(Edge);

		TArray<int8>& MutableResultCache = const_cast<TArray<int8>&>(ResultCache);
		const int8 StartResult = ResolveCachedResult(VtxFiltersManager, MutableResultCache, Start);
		const int8 EndResult = ResolveCachedResult(VtxFiltersManager, MutableResultCache, End);

		bool bPass = true;

		if (TypedFilterFactory->Config.bUseTwoFilterSets)
		{
			TArray<int8>& MutableResultCacheB = const_cast<TArray<int8>&>(ResultCacheB);
			const int8 StartResultB = ResolveCachedResult(VtxFiltersManagerB, MutableResultCacheB, Start);
			const int8 EndResultB = ResolveCachedResult(VtxFiltersManagerB, MutableResultCacheB, End);

			// "Matches A" = first-set result equals Expected; "matches B" = second-set result equals ExpectedB.
			const bool bStartMatchesA = StartResult == Expected;
			const bool bEndMatchesA = EndResult == Expected;
			const bool bStartMatchesB = StartResultB == ExpectedB;
			const bool bEndMatchesB = EndResultB == ExpectedB;

			if (TypedFilterFactory->Config.bRespectEdgeDirection)
			{
				// A is bound to Start, B to End.
				bPass = bStartMatchesA && bEndMatchesB;
			}
			else
			{
				// One endpoint matches A and the other matches B, in either orientation.
				bPass = (bStartMatchesA && bEndMatchesB) || (bEndMatchesA && bStartMatchesB);
			}

			return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
		}

		switch (TypedFilterFactory->Config.Mode)
		{
		case EPCGExEdgeEndpointsCheckMode::None:
			bPass = StartResult != Expected && EndResult != Expected;
			break;
		case EPCGExEdgeEndpointsCheckMode::Both:
			bPass = StartResult == Expected && EndResult == Expected;
			break;
		case EPCGExEdgeEndpointsCheckMode::Any:
			bPass = StartResult == Expected || EndResult == Expected;
			break;
		case EPCGExEdgeEndpointsCheckMode::Start:
			bPass = StartResult == Expected;
			break;
		case EPCGExEdgeEndpointsCheckMode::End:
			bPass = EndResult == Expected;
			break;
		case EPCGExEdgeEndpointsCheckMode::SeeSaw:
			bPass = StartResult != EndResult;
			break;
		}

		return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
	}

	FFilter::~FFilter()
	{
		VtxFiltersManager.Reset();
		VtxFiltersManagerB.Reset();
		TypedFilterFactory = nullptr;
	}
}

TArray<FPCGPinProperties> UPCGExEdgeEndpointsCheckFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceVtxFiltersLabel, TEXT("Filters used on endpoints. In two-input mode, this is the first set (A)."), Required)
	if (Config.bUseTwoFilterSets)
	{
		PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceVtxFiltersLabelB, TEXT("Second filter set (B), matched against the other endpoint."), Required)
	}
	return PinProperties;
}

UPCGExFactoryData* UPCGExEdgeEndpointsCheckFilterProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExEdgeEndpointsCheckFilterFactory* NewFactory = InContext->ManagedObjects->New<UPCGExEdgeEndpointsCheckFilterFactory>();

	NewFactory->Config = Config;

	Super::CreateFactory(InContext, NewFactory);

	if (!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceVtxFiltersLabel, NewFactory->FilterFactories, PCGExFactories::ClusterNodeFilters()))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	if (Config.bUseTwoFilterSets)
	{
		if (!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceVtxFiltersLabelB, NewFactory->FilterFactoriesB, PCGExFactories::ClusterNodeFilters()))
		{
			InContext->ManagedObjects->Destroy(NewFactory);
			return nullptr;
		}
	}

	if (!NewFactory->Init(InContext))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	return NewFactory;
}

#if WITH_EDITOR
FString UPCGExEdgeEndpointsCheckFilterProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
