// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Nodes/PCGExNodeAdjacencyFilter.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"

#define LOCTEXT_NAMESPACE "PCGExNodeAdjacencyFilter"
#define PCGEX_NAMESPACE NodeAdjacencyFilter

PCGEX_SETTING_VALUE_IMPL(FPCGExNodeAdjacencyFilterConfig, OperandB, double, EPCGExInputValueType::Attribute, OperandB, 0)

#if WITH_EDITOR
void FPCGExNodeAdjacencyFilterConfig::ApplyDeprecation()
{
	OperandAValue.Update(CompareAgainst_DEPRECATED, OperandA_DEPRECATED, OperandAConstant_DEPRECATED);
}

void FPCGExNodeAdjacencyFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandA")), FName(TEXT("OperandAValue")), FName(TEXT("Attribute")), FName(TEXT("Operand A (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandAConstant")), FName(TEXT("OperandAValue")), FName(TEXT("Constant")), FName(TEXT("Operand A")));
}
#endif

void UPCGExNodeAdjacencyFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.OperandAValue.RegisterBufferDependencies(InContext, FacadePreloader);
	if (Config.OperandBSource == EPCGExClusterElement::Vtx)
	{
		FacadePreloader.Register<double>(InContext, Config.OperandB);
	}
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNodeAdjacencyFilterFactory::CreateFilter() const
{
	return MakeShared<FNodeAdjacencyFilter>(this);
}

bool FNodeAdjacencyFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	bCaptureFromNodes = TypedFilterFactory->Config.OperandBSource != EPCGExClusterElement::Edge;

	if (!InitSettingValue(OperandA, TypedFilterFactory->Config.OperandAValue.GetValueSetting(PCGEX_QUIET_HANDLING), PointDataFacade, false))
	{
		return false;
	}

	if (!Adjacency.Init(InContext, PointDataFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	if (!InitSettingValue(OperandB, TypedFilterFactory->Config.GetValueSettingOperandB(PCGEX_QUIET_HANDLING), bCaptureFromNodes ? PointDataFacade : EdgeDataFacade, false))
	{
		return false;
	}

	// Binds TestSubFunc around one way of reading operand B off a link, so the read stays inlined in each loop.
	const auto BindTest = [this](auto ReadB)
	{
		if (Adjacency.bTestAllNeighbors)
		{
			switch (Adjacency.Consolidation)
			{
			case EPCGExAdjacencyGatherMode::Individual:
				// Each adjacent sample must pass the comparison, exit early.
				TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
				{
					for (const PCGExGraphs::FLink Lk : Node.Links)
					{
						if (!PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, ReadB(NodesRef, Lk), TypedFilterFactory->Config.Tolerance))
						{
							return false;
						}
					}
					return true;
				};
				return;
			case EPCGExAdjacencyGatherMode::Average:
				TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
				{
					double B = 0;
					for (const PCGExGraphs::FLink Lk : Node.Links)
					{
						B += ReadB(NodesRef, Lk);
					}
					return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B / FMath::Max(1, static_cast<double>(Node.Num())), TypedFilterFactory->Config.Tolerance);
				};
				return;
			case EPCGExAdjacencyGatherMode::Min:
				TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
				{
					double B = TNumericLimits<double>::Max();
					for (const PCGExGraphs::FLink Lk : Node.Links)
					{
						B = FMath::Min(B, ReadB(NodesRef, Lk));
					}
					return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
				};
				return;
			case EPCGExAdjacencyGatherMode::Max:
				TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
				{
					double B = TNumericLimits<double>::Lowest();
					for (const PCGExGraphs::FLink Lk : Node.Links)
					{
						B = FMath::Max(B, ReadB(NodesRef, Lk));
					}
					return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
				};
				return;
			case EPCGExAdjacencyGatherMode::Sum:
				TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
				{
					double B = 0;
					for (const PCGExGraphs::FLink Lk : Node.Links)
					{
						B += ReadB(NodesRef, Lk);
					}
					return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
				};
				return;
			default:
				break;
			}
		}

		// Only some adjacent samples must pass the comparison
		TestSubFunc = [this, ReadB](const PCGExClusters::FNode& Node, const TArray<PCGExClusters::FNode>& NodesRef, const double A)
		{
			const int32 Threshold = Adjacency.GetThreshold(Node);
			if (Threshold == -1)
			{
				return false;
			}
			int32 LocalSuccessCount = 0;

			for (const PCGExGraphs::FLink Lk : Node.Links)
			{
				if (PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, ReadB(NodesRef, Lk), TypedFilterFactory->Config.Tolerance))
				{
					LocalSuccessCount++;
				}
			}

			return PCGExCompare::Compare(Adjacency.ThresholdComparison, LocalSuccessCount, Threshold, Adjacency.ThresholdTolerance);
		};
	};

	if (bCaptureFromNodes)
	{
		BindTest([this](const TArray<PCGExClusters::FNode>& NodesRef, const PCGExGraphs::FLink Lk) { return OperandB->Read(NodesRef[Lk.Node].PointIndex); });
	}
	else
	{
		BindTest([this](const TArray<PCGExClusters::FNode>& NodesRef, const PCGExGraphs::FLink Lk) { return OperandB->Read(Lk.Edge); });
	}

	return true;
}

bool FNodeAdjacencyFilter::Test(const PCGExClusters::FNode& Node) const
{
	return TestSubFunc(Node, *Cluster->Nodes, OperandA->Read(Node.PointIndex));
}

FNodeAdjacencyFilter::~FNodeAdjacencyFilter()
{
	TypedFilterFactory = nullptr;
}

PCGEX_CREATE_FILTER_FACTORY(NodeAdjacency)

#if WITH_EDITOR
void UPCGExNodeAdjacencyFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNodeAdjacencyFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExNodeAdjacencyFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = Config.OperandAValue.Input == EPCGExInputValueType::Attribute ?
		                      PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandAValue.Attribute) :
		                      FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.OperandAValue.Constant) / 1000.0));

	DisplayName += PCGExCompare::ToString(Config.Comparison);

	DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB);
	DisplayName += TEXT(" (");

	switch (Config.Adjacency.Mode)
	{
	case EPCGExAdjacencyTestMode::All:
		DisplayName += TEXT("All");
		break;
	case EPCGExAdjacencyTestMode::Some:
		DisplayName += TEXT("Some");
		break;
	default: ;
	}

	DisplayName += TEXT(")");
	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
