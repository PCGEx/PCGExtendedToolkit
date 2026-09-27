// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Nodes/PCGExNodeEdgeDirectionFilter.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExNodeEdgeDirectionFilter"
#define PCGEX_NAMESPACE NodeEdgeDirectionFilter

#if WITH_EDITOR
void FPCGExNodeEdgeDirectionFilterConfig::ApplyDeprecation()
{
	DirectionValue.Update(CompareAgainst_DEPRECATED, Direction_DEPRECATED, DirectionConstant_DEPRECATED);
	// The legacy invert only applies to attribute input.
	DirectionValue.bFlip = bInvertDirection_DEPRECATED && CompareAgainst_DEPRECATED == EPCGExInputValueType::Attribute;
	HashComparisonDetails.ApplyDeprecation();
}

void FPCGExNodeEdgeDirectionFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Direction")), FName(TEXT("DirectionValue")), FName(TEXT("Attribute")), FName(TEXT("Direction (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("DirectionConstant")), FName(TEXT("DirectionValue")), FName(TEXT("Constant")), FName(TEXT("Direction")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("bInvertDirection")), FName(TEXT("DirectionValue")), FName(TEXT("bFlip")), FName(TEXT(" └─ Invert")));
	HashComparisonDetails.RenamePins(InSettings, InOutNode);
}
#endif

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNodeEdgeDirectionFilterFactory::CreateFilter() const
{
	return MakeShared<FNodeEdgeDirectionFilter>(this);
}

bool UPCGExNodeEdgeDirectionFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	if (Config.ComparisonQuality == EPCGExDirectionCheckMode::Dot)
	{
		Config.DotComparisonDetails.RegisterConsumableAttributesWithData(InContext, InData);
	}
	else
	{
		Config.HashComparisonDetails.RegisterConsumableAttributesWithData(InContext, InData);
	}

	return true;
}

bool FNodeEdgeDirectionFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	bFromNode = TypedFilterFactory->Config.DirectionOrder == EPCGExAdjacencyDirectionOrigin::FromNode;

	OperandDirection = TypedFilterFactory->Config.DirectionValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandDirection->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandDirection->Init(PointDataFacade, false))
	{
		return false;
	}

	if (!Adjacency.Init(InContext, PointDataFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	if (TypedFilterFactory->Config.ComparisonQuality == EPCGExDirectionCheckMode::Dot)
	{
		if (!DotComparison.Init(InContext, PointDataFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
		{
			return false;
		}
	}
	else
	{
		bUseDot = false;
		if (!HashComparison.Init(InContext, PointDataFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
		{
			return false;
		}
	}

	VtxTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

bool FNodeEdgeDirectionFilter::Test(const PCGExClusters::FNode& Node) const
{
	return bUseDot ? TestDot(Node) : TestHash(Node);
}

bool FNodeEdgeDirectionFilter::TestDot(const PCGExClusters::FNode& Node) const
{
	const int32 PointIndex = Node.PointIndex;

	FVector RefDir = OperandDirection->Read(PointIndex).GetSafeNormal();
	if (TypedFilterFactory->Config.bTransformDirection)
	{
		RefDir = VtxTransforms[PointIndex].TransformVectorNoScale(RefDir);
	}

	const double DotThreshold = DotComparison.GetComparisonThreshold(PointIndex);

	TArray<double> Dots;
	Dots.SetNumUninitialized(Node.Num());

	// Precompute all dot products

	if (bFromNode)
	{
		for (int i = 0; i < Dots.Num(); i++)
		{
			Dots[i] = FVector::DotProduct(RefDir, Cluster->GetDir(Node.Index, Node.Links[i].Node));
		}
	}
	else
	{
		for (int i = 0; i < Dots.Num(); i++)
		{
			Dots[i] = FVector::DotProduct(RefDir, Cluster->GetDir(Node.Links[i].Node, Node.Index));
		}
	}

	if (Adjacency.bTestAllNeighbors)
	{
		double A = 0;
		if (Adjacency.Consolidation == EPCGExAdjacencyGatherMode::Individual)
		{
			for (const double Dot : Dots)
			{
				if (!DotComparison.Test(Dot, DotThreshold))
				{
					return false;
				}
			}
			return true;
		}

		// First, build the consolidated operand B
		switch (Adjacency.Consolidation)
		{
		default: case EPCGExAdjacencyGatherMode::Average:
			for (const double Dot : Dots)
			{
				A += Dot;
			}
			A /= Node.Links.Num();
			break;
		case EPCGExAdjacencyGatherMode::Min:
			A = TNumericLimits<double>::Max();
			for (const double Dot : Dots)
			{
				A = FMath::Min(A, Dot);
			}
			break;
		case EPCGExAdjacencyGatherMode::Max:
			A = TNumericLimits<double>::Lowest();
			for (const double Dot : Dots)
			{
				A = FMath::Max(A, Dot);
			}
			break;
		case EPCGExAdjacencyGatherMode::Sum:
			for (const double Dot : Dots)
			{
				A += Dot;
			}
			break;
		}

		return DotComparison.Test(A, DotThreshold);
	}

	// Only some adjacent samples must pass the comparison
	const int32 Threshold = Adjacency.GetThreshold(Node);

	// Early exit on impossible thresholds (i.e node has less neighbor that the minimum or exact requirements)		
	if (Threshold == -1)
	{
		return false;
	}

	int32 LocalSuccessCount = 0;
	for (const double Dot : Dots)
	{
		if (DotComparison.Test(Dot, DotThreshold))
		{
			LocalSuccessCount++;
		}
	}

	return PCGExCompare::Compare(Adjacency.ThresholdComparison, LocalSuccessCount, Threshold, Adjacency.ThresholdTolerance);
}

bool FNodeEdgeDirectionFilter::TestHash(const PCGExClusters::FNode& Node) const
{
	const int32 PointIndex = Node.PointIndex;

	FVector RefDir = OperandDirection->Read(PointIndex).GetSafeNormal();
	if (TypedFilterFactory->Config.bTransformDirection)
	{
		RefDir = VtxTransforms[PointIndex].TransformVectorNoScale(RefDir);
	}

	const FVector CWTolerance = HashComparison.GetCWTolerance(PointIndex);
	const uint64 A = PCGEx::SH3(RefDir, CWTolerance);

	TArray<uint64> Hashes;
	Hashes.SetNumUninitialized(Node.Links.Num());

	// Precompute all dot products

	if (bFromNode)
	{
		for (int i = 0; i < Hashes.Num(); i++)
		{
			Hashes[i] = PCGEx::SH3(Cluster->GetDir(Node.Index, Node.Links[i].Node), CWTolerance);
		}
	}
	else
	{
		for (int i = 0; i < Hashes.Num(); i++)
		{
			Hashes[i] = PCGEx::SH3(Cluster->GetDir(Node.Links[i].Node, Node.Index), CWTolerance);
		}
	}

	if (Adjacency.bTestAllNeighbors)
	{
		for (const uint64 Hash : Hashes)
		{
			if (A != Hash)
			{
				return false;
			}
		}
		return true;
	}

	// Only some adjacent samples must pass the comparison
	const int32 Threshold = Adjacency.GetThreshold(Node);

	// Early exit on impossible thresholds (i.e node has less neighbor that the minimum or exact requirements)		
	if (Threshold == -1)
	{
		return false;
	}

	int32 LocalSuccessCount = 0;
	for (const uint64 Hash : Hashes)
	{
		if (A == Hash)
		{
			LocalSuccessCount++;
		}
	}

	return PCGExCompare::Compare(Adjacency.ThresholdComparison, LocalSuccessCount, Threshold, Adjacency.ThresholdTolerance);
}

FNodeEdgeDirectionFilter::~FNodeEdgeDirectionFilter()
{
	TypedFilterFactory = nullptr;
}

PCGEX_CREATE_FILTER_FACTORY(NodeEdgeDirection)

#if WITH_EDITOR
void UPCGExNodeEdgeDirectionFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("HashToleranceInput")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNodeEdgeDirectionFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExNodeEdgeDirectionFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Edge Direction ") + PCGExCompare::ToString(Config.DotComparisonDetails.Comparison);

	if (Config.DirectionValue.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.DirectionValue.Attribute);
	}
	else
	{
		DisplayName += TEXT("Constant");
	}

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
