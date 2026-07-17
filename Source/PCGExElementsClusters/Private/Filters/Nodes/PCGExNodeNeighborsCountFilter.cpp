// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Nodes/PCGExNodeNeighborsCountFilter.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExNodeNeighborsCountFilter"
#define PCGEX_NAMESPACE NodeNeighborsCountFilter

PCGEX_SETTING_VALUE_IMPL(FPCGExNodeNeighborsCountFilterConfig, LocalCount, double, CompareAgainst, LocalCount, Count)

void UPCGExNodeNeighborsCountFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.LocalCount);
	}
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNodeNeighborsCountFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExNodeNeighborsCount::FFilter>(this);
}

namespace PCGExNodeNeighborsCount
{
	bool FFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		LocalCount = TypedFilterFactory->Config.GetValueSettingLocalCount(PCGEX_QUIET_HANDLING);
		LocalCount->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!LocalCount->Init(PointDataFacade, false))
		{
			return false;
		}

		return true;
	}

	bool FFilter::Test(const PCGExClusters::FNode& Node) const
	{
		const double A = Node.Num();
		const double B = LocalCount ? LocalCount->Read(Node.PointIndex) : TypedFilterFactory->Config.Count;
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(NodeNeighborsCount)

#if WITH_EDITOR
FString UPCGExNodeNeighborsCountFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = "Num Edges" + PCGExCompare::ToString(Config.Comparison);

	if (Config.CompareAgainst == EPCGExInputValueType::Constant)
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.Count);
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.LocalCount);
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
