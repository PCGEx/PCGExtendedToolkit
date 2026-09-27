// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Edges/PCGExIsoEdgeDirectionFilter.h"

#include "PCGExVersion.h"
#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Sorting/PCGExSortingRuleProvider.h"

#define LOCTEXT_NAMESPACE "PCGExIsoEdgeDirectionFilter"
#define PCGEX_NAMESPACE IsoEdgeDirectionFilter

#if WITH_EDITOR
void FPCGExIsoEdgeDirectionFilterConfig::ApplyDeprecation()
{
	DirectionValue.Update(CompareAgainst_DEPRECATED, Direction_DEPRECATED, DirectionConstant_DEPRECATED);
	// The legacy invert only applies to attribute input.
	DirectionValue.bFlip = bInvertDirection_DEPRECATED && CompareAgainst_DEPRECATED == EPCGExInputValueType::Attribute;
	HashComparisonDetails.ApplyDeprecation();
}

void FPCGExIsoEdgeDirectionFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Direction")), FName(TEXT("DirectionValue")), FName(TEXT("Attribute")), FName(TEXT("Direction (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("DirectionConstant")), FName(TEXT("DirectionValue")), FName(TEXT("Constant")), FName(TEXT("Direction")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("bInvertDirection")), FName(TEXT("DirectionValue")), FName(TEXT("bFlip")), FName(TEXT(" └─ Invert")));
	HashComparisonDetails.RenamePins(InSettings, InOutNode);
}
#endif

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

	if (!DirectionSettings.InitFromParent(InContext, DirectionSettings, InEdgeDataFacade, PCGEX_QUIET_HANDLING))
	{
		return false;
	}

	OperandDirection = TypedFilterFactory->Config.DirectionValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandDirection->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandDirection->Init(InEdgeDataFacade))
	{
		return false;
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
	const FVector RefDir = OperandDirection->Read(PtIndex).GetSafeNormal();
	return DotComparison.Test(FVector::DotProduct(TypedFilterFactory->Config.bTransformDirection ? InTransforms[PtIndex].TransformVectorNoScale(RefDir) : RefDir, EdgeDir), PtIndex);
}

bool FIsoEdgeDirectionFilter::TestHash(const int32 PtIndex, const FVector& EdgeDir) const
{
	FVector RefDir = OperandDirection->Read(PtIndex);
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

	NewFactory->InitializationFailurePolicy = InitializationFailurePolicy;
	NewFactory->MissingDataPolicy = MissingDataPolicy;
	NewFactory->Config = Config;
	if (Config.DirectionSettings.DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort)
	{
		NewFactory->EdgeSortingRules = PCGExSorting::GetSortingRules(InContext, PCGExClusters::Labels::SourceEdgeSortingRules);
	}

	Super::CreateFactory(InContext, NewFactory);

	if (!NewFactory->Init(InContext))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}
	return NewFactory;
}

#if WITH_EDITOR
void UPCGExIsoEdgeDirectionFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("HashToleranceInput")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExIsoEdgeDirectionFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExIsoEdgeDirectionFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Edge Direction ") + PCGExCompare::ToString(Config.DotComparisonDetails.Comparison);

	UPCGExIsoEdgeDirectionFilterProviderSettings* MutableSelf = const_cast<UPCGExIsoEdgeDirectionFilterProviderSettings*>(this);
	MutableSelf->Config.DirectionValue.Constant = Config.DirectionValue.Constant.GetSafeNormal();

	if (Config.DirectionValue.Input == EPCGExInputValueType::Constant)
	{
		DisplayName += TEXT("Constant");
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.DirectionValue.Attribute);
	}
	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
