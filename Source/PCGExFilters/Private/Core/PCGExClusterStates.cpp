// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClusterStates.h"


#include "PCGParamData.h"
#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExClusterFilter.h"

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoClusterState, UPCGExClusterStateFactoryData)

TSharedPtr<PCGExPointFilter::IFilter> UPCGExClusterStateFactoryData::CreateFilter() const
{
	PCGEX_MAKE_SHARED(NewState, PCGExClusterStates::FState, this)
	NewState->Config = Config;
	NewState->BaseConfig = NewState->Config;
	return NewState;
}

void UPCGExClusterStateFactoryData::BeginDestroy()
{
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGExClusterStateFactoryProviderSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_UPDATE_TO_DATA_VERSION(1, 71, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::ApplyDeprecation(InOutNode);
}
#endif

namespace PCGExClusterStates
{
	FState::~FState()
	{
	}

	bool FState::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		Config.Init();

		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade)) { return false; }

		Manager = MakeShared<PCGExClusterFilter::FManager>(InCluster, PointDataFacade.ToSharedRef(), EdgeDataFacade.ToSharedRef());
		Manager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters);
		return true;
	}

	bool FState::InitInternalManager(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories)
	{
		return Manager->Init(InContext, InFactories);
	}

	bool FState::Test(const int32 Index) const
	{
		return Manager->Test(Index);
	}

	bool FState::Test(const PCGExClusters::FNode& Node) const
	{
		return Manager->Test(Node);
	}

	bool FState::Test(const PCGExGraphs::FEdge& Edge) const
	{
		return Manager->Test(Edge);
	}

	// Applies the configured bitmask operation to the shared flags based on pass/fail.
	// Each state can independently set, clear, or toggle specific flag bits.
	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const int32 Index) const
	{
		if (Config.bOnTestPass && bSuccess) { Config.PassStateFlags.Mutate(InFlags); }
		else if (Config.bOnTestFail && !bSuccess) { Config.FailStateFlags.Mutate(InFlags); }
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExClusters::FNode& Node) const
	{
		if (Config.bOnTestPass && bSuccess) { Config.PassStateFlags.Mutate(InFlags); }
		else if (Config.bOnTestFail && !bSuccess) { Config.FailStateFlags.Mutate(InFlags); }
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExGraphs::FEdge& Edge) const
	{
		if (Config.bOnTestPass && bSuccess) { Config.PassStateFlags.Mutate(InFlags); }
		else if (Config.bOnTestFail && !bSuccess) { Config.FailStateFlags.Mutate(InFlags); }
	}

	FStateManager::FStateManager(const TSharedPtr<TArray<int64>>& InFlags, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataCache, const TSharedRef<PCGExData::FFacade>& InEdgeDataCache)
		: FManager(InCluster, InPointDataCache, InEdgeDataCache)
	{
		FlagsCache = InFlags;
	}

	// Evaluates all states and accumulates flag mutations. Always returns true --
	// the purpose is the side-effect on the FlagsCache, not point filtering.
	bool FStateManager::Test(const int32 Index)
	{
		int64& Flags = *(FlagsCache->GetData() + Index);
		for (const TSharedPtr<FState>& State : States) { State->ProcessFlags(State->Test(Index), Flags, Index); }
		return true;
	}

	bool FStateManager::Test(const PCGExClusters::FNode& Node)
	{
		int64& Flags = *(FlagsCache->GetData() + Node.PointIndex);
		for (const TSharedPtr<FState>& State : States) { State->ProcessFlags(State->Test(Node), Flags, Node); }
		return true;
	}

	bool FStateManager::Test(const PCGExGraphs::FEdge& Edge)
	{
		int64& Flags = *(FlagsCache->GetData() + Edge.PointIndex);
		for (const TSharedPtr<FState>& State : States) { State->ProcessFlags(State->Test(Edge), Flags, Edge); }
		return true;
	}

	void FStateManager::PostInitFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter)
	{
		const TSharedPtr<FState>& State = StaticCastSharedPtr<FState>(InFilter);
		State->InitInternalManager(InContext, State->StateFactory->FilterFactories);

		FManager::PostInitFilter(InContext, InFilter);

		States.Add(State);
	}
}

UPCGExFactoryData* UPCGExClusterStateFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExClusterStateFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExClusterStateFactoryData>();

	NewFactory->BaseConfig = Config;
	NewFactory->Config = Config;

	Super::CreateFactory(InContext, NewFactory);

	return NewFactory;
}

TSet<PCGExFactories::EType> UPCGExClusterStateFactoryProviderSettings::GetInternalFilterTypes() const
{
	return PCGExFactories::ClusterNodeFilters;
}
