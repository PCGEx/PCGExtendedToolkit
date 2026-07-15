// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExIsoEdgeDirectionFilter.h"

#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Sorting/PCGExSortingRuleProvider.h"

#define LOCTEXT_NAMESPACE "PCGExIsoEdgeDirectionFilter"
#define PCGEX_NAMESPACE IsoEdgeDirectionFilter

PCGEX_SETTING_VALUE_IMPL(FPCGExIsoEdgeDirectionFilterConfig, Direction, FVector, CompareAgainst, Direction, DirectionConstant)

void UPCGExIsoEdgeDirectionFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.DirectionSettings.RegisterBuffersDependencies(InContext, FacadePreloader, &EdgeSortingRules);
}

bool UPCGExIsoEdgeDirectionFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
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

TSharedPtr<PCGExPointFilter::IFilter> UPCGExIsoEdgeDirectionFilterFactory::CreateFilter() const
{
	return MakeShared<FIsoEdgeDirectionFilter>(this);
}

FIsoEdgeDirectionFilter::FIsoEdgeDirectionFilter(const UPCGExIsoEdgeDirectionFilterFactory* InFactory)
	: IEdgeFilter(InFactory)
	  , TypedFilterFactory(InFactory)
{
	DotComparison = InFactory->Config.DotComparisonDetails;
	HashComparison = InFactory->Config.HashComparisonDetails;
	DirectionSettings = TypedFilterFactory->Config.DirectionSettings;
}

bool FIsoEdgeDirectionFilter::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!IFilter::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	// Init for vtx
	if (!DirectionSettings.Init(InContext, InPointDataFacade, &TypedFilterFactory->EdgeSortingRules, PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	// Init for edges
	if (!DirectionSettings.InitFromParent(InContext, DirectionSettings, InEdgeDataFacade, PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	OperandDirection = TypedFilterFactory->Config.GetValueSettingDirection(PCGEX_QUIET_HANDLING);
	OperandDirection->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandDirection->Init(InEdgeDataFacade))
	{
		return false;
	}
	if (!OperandDirection->IsConstant())
	{
		DirectionMultiplier = TypedFilterFactory->Config.bInvertDirection ? -1 : 1;
	}

	if (TypedFilterFactory->Config.ComparisonQuality == EPCGExDirectionCheckMode::Dot)
	{
		if (!DotComparison.Init(InContext, InEdgeDataFacade))
		{
			return false;
		}
	}
	else
	{
		bUseDot = false;
		if (!HashComparison.Init(InContext, InEdgeDataFacade))
		{
			return false;
		}
	}

	InTransforms = InEdgeDataFacade->Source->GetIn()->GetConstTransformValueRange();

	return true;
}

bool FIsoEdgeDirectionFilter::Test(const PCGExGraphs::FEdge& Edge) const
{
	PCGExGraphs::FEdge MutableEdge = Edge;
	DirectionSettings.SortEndpoints(Cluster.Get(), MutableEdge);

	const FVector Direction = Cluster->GetEdgeDir(MutableEdge);

	return bUseDot ? TestDot(Edge.PointIndex, Direction) : TestHash(Edge.PointIndex, Direction);
}

bool FIsoEdgeDirectionFilter::TestDot(const int32 PtIndex, const FVector& EdgeDir) const
{
	const FVector RefDir = OperandDirection->Read(PtIndex).GetSafeNormal() * DirectionMultiplier;
	return DotComparison.Test(FVector::DotProduct(TypedFilterFactory->Config.bTransformDirection ? InTransforms[PtIndex].TransformVectorNoScale(RefDir) : RefDir, EdgeDir), PtIndex);
}

bool FIsoEdgeDirectionFilter::TestHash(const int32 PtIndex, const FVector& EdgeDir) const
{
	FVector RefDir = OperandDirection->Read(PtIndex) * DirectionMultiplier;
	if (TypedFilterFactory->Config.bTransformDirection)
	{
		RefDir = InTransforms[PtIndex].TransformVectorNoScale(RefDir);
	}

	RefDir.Normalize();
	return HashComparison.Test(RefDir, EdgeDir, PtIndex);
}

FIsoEdgeDirectionFilter::~FIsoEdgeDirectionFilter()
{
	TypedFilterFactory = nullptr;
}

TArray<FPCGPinProperties> UPCGExIsoEdgeDirectionFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (Config.DirectionSettings.DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort)
	{
		PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceEdgeSortingRules, "Plug sorting rules here. Order is defined by each rule' priority value, in ascending order.", Required, FPCGExDataTypeInfoSortRule::AsId())
	}
	return PinProperties;
}

UPCGExFactoryData* UPCGExIsoEdgeDirectionFilterProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExIsoEdgeDirectionFilterFactory* NewFactory = InContext->ManagedObjects->New<UPCGExIsoEdgeDirectionFilterFactory>();

	NewFactory->Config = Config;
	if (Config.DirectionSettings.DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort)
	{
		NewFactory->EdgeSortingRules = PCGExSorting::GetSortingRules(InContext, PCGExClusters::Labels::SourceEdgeSortingRules);
	}

	Super::CreateFactory(InContext, NewFactory);

	if (!NewFactory->Init(InContext))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
	}
	return NewFactory;
}

#if WITH_EDITOR
FString UPCGExIsoEdgeDirectionFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Edge Direction ") + PCGExCompare::ToString(Config.DotComparisonDetails.Comparison);

	UPCGExIsoEdgeDirectionFilterProviderSettings* MutableSelf = const_cast<UPCGExIsoEdgeDirectionFilterProviderSettings*>(this);
	MutableSelf->Config.DirectionConstant = Config.DirectionConstant.GetSafeNormal();

	if (Config.CompareAgainst == EPCGExInputValueType::Constant)
	{
		DisplayName += TEXT("Constant");
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.Direction);
	}
	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
