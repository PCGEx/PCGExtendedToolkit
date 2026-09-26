// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Nodes/PCGExNodeNeighborsCountFilter.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExNodeNeighborsCountFilter"
#define PCGEX_NAMESPACE NodeNeighborsCountFilter

#if WITH_EDITOR
void FPCGExNodeNeighborsCountFilterConfig::ApplyDeprecation()
{
	CountValue.Update(CompareAgainst_DEPRECATED, LocalCount_DEPRECATED, Count_DEPRECATED);
}

void FPCGExNodeNeighborsCountFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("LocalCount")), FName(TEXT("CountValue")), FName(TEXT("Attribute")), FName(TEXT("Operand A (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Count")), FName(TEXT("CountValue")), FName(TEXT("Constant")), FName(TEXT("Operand A")));
}
#endif

void UPCGExNodeNeighborsCountFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.CountValue.RegisterBufferDependencies(InContext, FacadePreloader);
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

		LocalCount = TypedFilterFactory->Config.CountValue.GetValueSetting(PCGEX_QUIET_HANDLING);
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
		const double B = LocalCount->Read(Node.PointIndex);
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(NodeNeighborsCount)

#if WITH_EDITOR
void UPCGExNodeNeighborsCountFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNodeNeighborsCountFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExNodeNeighborsCountFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = "Num Edges" + PCGExCompare::ToString(Config.Comparison);

	if (Config.CountValue.Input == EPCGExInputValueType::Constant)
	{
		DisplayName += FString::SanitizeFloat(Config.CountValue.Constant);
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.CountValue.Attribute);
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
