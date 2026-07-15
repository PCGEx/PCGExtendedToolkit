// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeNeighborsCountFilter.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Graphs/PCGExGraph.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeNeighborsCountFilter"
#define PCGEX_NAMESPACE EdgeNeighborsCountFilter

#if WITH_EDITOR
void FPCGExEdgeNeighborsCountFilterConfig::ApplyDeprecation()
{
	Threshold.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, ThresholdConstant_DEPRECATED);
}

void FPCGExEdgeNeighborsCountFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdConstant")), FName(TEXT("Threshold")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("Threshold")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeNeighborsCountFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeNeighborsCount::FFilter>(this);
}

namespace PCGExEdgeNeighborsCount
{
	bool FFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		ThresholdBuffer = TypedFilterFactory->Config.Threshold.GetValueSetting(PCGEX_QUIET_HANDLING);
		ThresholdBuffer->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!ThresholdBuffer->Init(PointDataFacade))
		{
			return false;
		}

		return true;
	}

	bool FFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		const PCGExClusters::FNode* From = Cluster->GetEdgeStart(Edge);
		const PCGExClusters::FNode* To = Cluster->GetEdgeEnd(Edge);

		// TODO : Make these lambdas

		const int32 Threshold = ThresholdBuffer->Read(Edge.PointIndex);
		const EPCGExComparison Comparison = TypedFilterFactory->Config.Comparison;
		const EPCGExRefineEdgeThresholdMode Mode = TypedFilterFactory->Config.Mode;
		const double Tolerance = TypedFilterFactory->Config.Tolerance;
		bool bResult = false;

		if (Mode == EPCGExRefineEdgeThresholdMode::Both)
		{
			bResult = PCGExCompare::Compare(Comparison, From->Num(), Threshold) && PCGExCompare::Compare(Comparison, To->Num(), Threshold, Tolerance);
		}
		else if (Mode == EPCGExRefineEdgeThresholdMode::Any)
		{
			bResult = PCGExCompare::Compare(Comparison, From->Num(), Threshold) || PCGExCompare::Compare(Comparison, To->Num(), Threshold, Tolerance);
		}
		else if (Mode == EPCGExRefineEdgeThresholdMode::Sum)
		{
			bResult = PCGExCompare::Compare(Comparison, (From->Num() + To->Num()), Threshold, Tolerance);
		}

		return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
	}

	FFilter::~FFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(EdgeNeighborsCount)

#if WITH_EDITOR
void UPCGExEdgeNeighborsCountFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExEdgeNeighborsCountFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExEdgeNeighborsCountFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = "Num Vtx (";

	switch (Config.Mode)
	{
	case EPCGExRefineEdgeThresholdMode::Sum:
		DisplayName += "Sum";
		break;
	case EPCGExRefineEdgeThresholdMode::Any:
		DisplayName += "Any";
		break;
	case EPCGExRefineEdgeThresholdMode::Both:
		DisplayName += "Both";
		break;
	}

	DisplayName += ")" + PCGExCompare::ToString(Config.Comparison);
	if (Config.Threshold.Input == EPCGExInputValueType::Constant)
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.Threshold.Constant);
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.Threshold.Attribute);
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
