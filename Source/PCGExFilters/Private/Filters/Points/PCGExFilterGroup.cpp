// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExFilterGroup.h"

#include "PCGExFilterCommon.h"
#include "PCGExFiltersSubSystem.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFilterTypeSets.h"

namespace PCGExFilterGroup
{
	bool FFilterGroup::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		PointDataFacade = InPointDataFacade;
		return InitManaged(InContext);
	}

	bool FFilterGroup::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		bInitForCluster = true;
		Cluster = InCluster;
		PointDataFacade = InPointDataFacade;
		EdgeDataFacade = InEdgeDataFacade;
		return InitManaged(InContext);
	}

	void FFilterGroup::PostInit()
	{
		// No internal init yet, will cause issues with local caching

		/*
		if (!bCacheResults) { return; }
		const int32 NumResults = GetFilterType() == PCGExFilters::EType::Node ? Cluster->Nodes->Num() : EdgeDataFacade->Source->GetNum();
		Results.SetNumUninitialized(NumResults);
		for (bool& Result : Results) { Result = false; }
		*/
	}

	void FFilterGroup::SetSupportedTypes(const TSet<FPCGDataTypeBaseId>* InTypes)
	{
		SupportedFactoriesTypes = InTypes;
	}

	bool FFilterGroup::InitManaged(FPCGExContext* InContext)
	{
		bool bWantsTrueConstant = false;
		bool bWantsFalseConstant = false;

		for (const UPCGExPointFilterFactoryData* ManagedFactory : *ManagedFactories)
		{
			if (SupportedFactoriesTypes && !SupportedFactoriesTypes->Contains(ManagedFactory->GetDataTypeId()))
			{
				PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(FTEXT("A grouped filter is of an unexpected type : {0}."), FText::FromString(GetNameSafe(ManagedFactory->GetClass()))));
				continue;
			}

			TSharedPtr<PCGExPointFilter::IFilter> NewFilter = ManagedFactory->CreateFilter();
			NewFilter->bUseDataDomainSelectorsOnly = ManagedFactory->GetOnlyUseDataDomain();
			NewFilter->bCacheResults = false;
			NewFilter->SetSupportedTypes(SupportedFactoriesTypes);
			NewFilter->bWillBeUsedWithCollections = bWillBeUsedWithCollections;

			if (!InitManagedFilter(InContext, NewFilter, ManagedFactory->InitializationFailurePolicy != EPCGExFilterNoDataFallback::Error))
			{
				// TODO: duplicated with FManager::Init -- consolidate via a shared FFilterStack base (see note there).
				if (bWillBeUsedWithCollections)
				{
					// See FManager::Init: in collection mode a per-point Init failure is expected (the
					// dummy seed facade may lack the data). The filter is evaluated per-data through
					// Test(IO, ParentCollection), so keep it rather than substituting a constant.
					ManagedFilters.Add(NewFilter);
					continue;
				}

				if (ManagedFactory->InitializationFailurePolicy == EPCGExFilterNoDataFallback::Error)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("A grouped filter failed to initialize properly : {0}."), FText::FromString(GetNameSafe(ManagedFactory->GetClass()))));
				}
				else if (ManagedFactory->InitializationFailurePolicy == EPCGExFilterNoDataFallback::Pass)
				{
					bWantsTrueConstant = true;
				}
				else
				{
					bWantsFalseConstant = true;
					break;
				}
				continue;
			}

			ManagedFilters.Add(NewFilter);
		}

		auto RegisterConstant = [&](bool bConstant)
		{
			PCGEX_FILTERS_SUBSYSTEM
			const TSharedPtr<PCGExPointFilter::IFilter> NewFilter = PCGExFiltersSubsystem->GetConstantFilter(bConstant);
			NewFilter->bUseDataDomainSelectorsOnly = true;
			NewFilter->bCacheResults = bCacheResults;
			NewFilter->bUseEdgeAsPrimary = bUseEdgeAsPrimary;
			NewFilter->bWillBeUsedWithCollections = bWillBeUsedWithCollections;

			InitManagedFilter(InContext, NewFilter);
			ManagedFilters.Add(NewFilter);
		};

		if (bWantsFalseConstant)
		{
			RegisterConstant(false);
		}
		if (bWantsTrueConstant)
		{
			RegisterConstant(true);
		}

		return PostInitManaged(InContext);
	}

	// Routes child filter initialization based on its type:
	// - Groups: recurse through the group Init path (supporting nested AND/OR)
	// - Cluster-only filters: require cluster context, fail gracefully if unavailable
	// - Point filters: standard Init, using edge data when bUseEdgeAsPrimary is set
	bool FFilterGroup::InitManagedFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& Filter, const bool bQuiet) const
	{
		if (Filter->GetFilterType() == PCGExFilters::EType::Group)
		{
			if (bInitForCluster)
			{
				FFilterGroup* FilterGroup = static_cast<FFilterGroup*>(Filter.Get());
				FilterGroup->bUseEdgeAsPrimary = bUseEdgeAsPrimary;
				return FilterGroup->Init(InContext, Cluster.ToSharedRef(), PointDataFacade.ToSharedRef(), EdgeDataFacade.ToSharedRef());
			}

			return Filter->Init(InContext, PointDataFacade);
		}

		if (PCGExFactories::ClusterOnlyFilters().Contains(Filter->Factory->GetDataTypeId()))
		{
			if (!bInitForCluster)
			{
				// Other filter types require cluster data, which we don't have :/
				if (!bQuiet)
				{
					PCGEX_LOG_INVALID_INPUT(InContext, FTEXT("Using a Cluster filter without cluster data"));
				}
				return false;
			}

			IFilter* ClusterFilter = static_cast<IFilter*>(Filter.Get());
			ClusterFilter->bUseEdgeAsPrimary = bUseEdgeAsPrimary;
			return ClusterFilter->Init(InContext, Cluster.ToSharedRef(), PointDataFacade.ToSharedRef(), EdgeDataFacade.ToSharedRef());
		}

		return Filter->Init(InContext, bUseEdgeAsPrimary ? EdgeDataFacade : PointDataFacade);
	}

	bool FFilterGroup::PostInitManaged(FPCGExContext* InContext)
	{
		bValid = !ManagedFilters.IsEmpty();

		if (!bValid)
		{
			return false;
		}

		// Sort mappings so higher priorities come last, as they have to potential to override values.
		ManagedFilters.Sort([](const TSharedPtr<PCGExPointFilter::IFilter>& A, const TSharedPtr<PCGExPointFilter::IFilter>& B)
		{
			return A->Factory->Priority < B->Factory->Priority;
		});

		// Update index & post-init
		Stack.Reserve(ManagedFilters.Num());
		for (int i = 0; i < ManagedFilters.Num(); i++)
		{
			TSharedPtr<PCGExPointFilter::IFilter> Filter = ManagedFilters[i];
			Filter->FilterIndex = i;
			PostInitManagedFilter(InContext, Filter);
			Stack.Add(Filter.Get());
		}

		return true;
	}

	void FFilterGroup::PostInitManagedFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter)
	{
		InFilter->PostInit();
	}

	bool FFilterGroupAND::Test(const int32 Index) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (!Filter->Test(Index))
			{
				return bInvert;
			}
		}
		return !bInvert;
	}

	bool FFilterGroupAND::Test(const PCGExClusters::FNode& Node) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (!Filter->Test(Node))
			{
				return bInvert;
			}
		}
		return !bInvert;
	}

	bool FFilterGroupAND::Test(const PCGExGraphs::FEdge& Edge) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (!Filter->Test(Edge))
			{
				return bInvert;
			}
		}
		return !bInvert;
	}

	bool FFilterGroupAND::Test(const PCGExData::FProxyPoint& Point) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (!Filter->Test(Point))
			{
				return bInvert;
			}
		}
		return !bInvert;
	}

	bool FFilterGroupAND::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (!Filter->Test(IO, ParentCollection))
			{
				return bInvert;
			}
		}
		return !bInvert;
	}

	bool FFilterGroupOR::Test(const int32 Index) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (Filter->Test(Index))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}

	bool FFilterGroupOR::Test(const PCGExClusters::FNode& Node) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (Filter->Test(Node))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}

	bool FFilterGroupOR::Test(const PCGExGraphs::FEdge& Edge) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (Filter->Test(Edge))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}

	bool FFilterGroupOR::Test(const PCGExData::FProxyPoint& Point) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (Filter->Test(Point))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}

	bool FFilterGroupOR::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
	{
		for (const PCGExPointFilter::IFilter* Filter : Stack)
		{
			if (Filter->Test(IO, ParentCollection))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}
}

#define PCGEX_FILTERGROUP_FOREACH(_BODY) for (const TObjectPtr<const UPCGExPointFilterFactoryData>& SubFilter : FilterFactories) { if (!IsValid(SubFilter)) { continue; } _BODY }

bool UPCGExFilterGroupFactoryData::SupportsProxyEvaluation() const
{
	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(if (!SubFilter->SupportsProxyEvaluation()) { return false; })
	return true;
}

bool UPCGExFilterGroupFactoryData::SupportsCollectionEvaluation() const
{
	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(if(!SubFilter->SupportsCollectionEvaluation()){ return false; })
	return true;
}

bool UPCGExFilterGroupFactoryData::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	Super::RegisterConsumableAttributes(InContext);

	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(SubFilter->RegisterConsumableAttributes(InContext);)
	return true;
}

bool UPCGExFilterGroupFactoryData::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	Super::RegisterConsumableAttributesWithData(InContext, InData);

	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(SubFilter->RegisterConsumableAttributesWithData(InContext, InData);)
	return true;
}

void UPCGExFilterGroupFactoryData::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const
{
	Super::RegisterAssetDependencies(InDependencies);

	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(SubFilter->RegisterAssetDependencies(InDependencies);)
}

void UPCGExFilterGroupFactoryData::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	// Ensure we grab dependencies from plugged-in factories recursively
	PCGEX_FILTERGROUP_FOREACH(SubFilter->RegisterBuffersDependencies(InContext, FacadePreloader);)
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExFilterGroupFactoryDataAND::CreateFilter() const
{
	PCGEX_MAKE_SHARED(NewFilterGroup, PCGExFilterGroup::FFilterGroupAND, this, &FilterFactories)
	NewFilterGroup->bInvert = bInvert;
	return NewFilterGroup;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExFilterGroupFactoryDataOR::CreateFilter() const
{
	PCGEX_MAKE_SHARED(NewFilterGroup, PCGExFilterGroup::FFilterGroupOR, this, &FilterFactories)
	NewFilterGroup->bInvert = bInvert;
	return NewFilterGroup;
}

#undef PCGEX_FILTERGROUP_FOREACH

#pragma region UPCGExFilterGroupProvider

#if WITH_EDITOR
FString UPCGExFilterGroupProviderSettings::GetDisplayName() const
{
	return Mode == EPCGExFilterGroupMode::OR ? TEXT("OR") : TEXT("AND");
}


TArray<FPCGPreConfiguredSettingsInfo> UPCGExFilterGroupProviderSettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Infos;

	const TSet<EPCGExFilterGroupMode> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExFilterGroupMode>(ValuesToSkip, FTEXT("{0} (Combine Filters)"));
}
#endif

void UPCGExFilterGroupProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	Mode = PreconfigureInfo.PreconfiguredIndex == 0 ? EPCGExFilterGroupMode::AND : EPCGExFilterGroupMode::OR;
}

TArray<FPCGPinProperties> UPCGExFilterGroupProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceFiltersLabel, "List of filters that will be processed in either AND or OR mode.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFilterGroupProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(GetMainOutputPin(), "Gathered filters.", Required)
	return PinProperties;
}

FName UPCGExFilterGroupProviderSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputFilterLabel;
}

UPCGExFactoryData* UPCGExFilterGroupProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFilterGroupFactoryData* NewFactory;

	if (Mode == EPCGExFilterGroupMode::AND)
	{
		NewFactory = InContext->ManagedObjects->New<UPCGExFilterGroupFactoryDataAND>();
	}
	else
	{
		NewFactory = InContext->ManagedObjects->New<UPCGExFilterGroupFactoryDataOR>();
	}

	if (!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceFiltersLabel, NewFactory->FilterFactories, PCGExFactories::AnyFilters()))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	int32 MaxPriority = Priority;
	for (const TObjectPtr<const UPCGExPointFilterFactoryData>& Factory : NewFactory->FilterFactories)
	{
		MaxPriority = FMath::Max(MaxPriority, Factory->Priority);
	}

	NewFactory->Priority = MaxPriority;
	NewFactory->bInvert = bInvert;

	return Super::CreateFactory(InContext, NewFactory);
}

#pragma endregion
