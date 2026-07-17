// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExEdgeLengthFilter.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExEdgeLengthFilter"
#define PCGEX_NAMESPACE EdgeLengthFilter

#if WITH_EDITOR
void FPCGExEdgeLengthFilterConfig::ApplyDeprecation()
{
	Threshold.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, ThresholdConstant_DEPRECATED);
}

void FPCGExEdgeLengthFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdConstant")), FName(TEXT("Threshold")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("Threshold")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEdgeLengthFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExEdgeLength::FLengthFilter>(this);
}

namespace PCGExEdgeLength
{
	bool FLengthFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		TConstPCGValueRange<FTransform> VtxTransforms = InPointDataFacade->Source->GetIn()->GetConstTransformValueRange();

		Threshold = TypedFilterFactory->Config.Threshold.GetValueSetting(PCGEX_QUIET_HANDLING);
		Threshold->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!Threshold->Init(PointDataFacade))
		{
			return false;
		}

		return true;
	}

	bool FLengthFilter::Test(const PCGExGraphs::FEdge& Edge) const
	{
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, Cluster->GetEdgeLength(Edge), Threshold->Read(Edge.PointIndex), TypedFilterFactory->Config.Tolerance) ? !TypedFilterFactory->Config.bInvert : TypedFilterFactory->Config.bInvert;
	}

	FLengthFilter::~FLengthFilter()
	{
		TypedFilterFactory = nullptr;
	}
}

PCGEX_CREATE_FILTER_FACTORY(EdgeLength)

#if WITH_EDITOR
void UPCGExEdgeLengthFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExEdgeLengthFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExEdgeLengthFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = "Edge Length ";
	DisplayName += PCGExCompare::ToString(Config.Comparison);
	if (Config.Threshold.Input == EPCGExInputValueType::Constant)
	{
		DisplayName += FString::Printf(TEXT("%f"), Config.Threshold.Constant);
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
