// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExPrimitiveFilter.h"

#include "Components/PrimitiveComponent.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPrimitiveData.h"

#define LOCTEXT_NAMESPACE "PCGExPrimitiveFilterDefinition"
#define PCGEX_NAMESPACE PCGExPrimitiveFilterDefinition

#pragma region UPCGExPrimitiveFilterFactory

bool UPCGExPrimitiveFilterFactory::SupportsProxyEvaluation() const
{
	return true;
}

bool UPCGExPrimitiveFilterFactory::SupportsCollectionEvaluation() const
{
	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExPrimitiveFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FPrimitiveFilter>(this);
}

PCGExFactories::EPreparationResult UPCGExPrimitiveFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(FName("Primitives"));

	for (const FPCGTaggedData& TaggedData : Inputs)
	{
		const UPCGPrimitiveData* PrimitiveData = Cast<UPCGPrimitiveData>(TaggedData.Data);
		if (!PrimitiveData)
		{
			continue;
		}

		const TWeakObjectPtr<UPrimitiveComponent> Component = PrimitiveData->GetComponent();
		if (!Component.IsValid())
		{
			continue;
		}

		PCGExPointFilter::FCachedPrimitive& Entry = CachedPrimitives.AddDefaulted_GetRef();
		Entry.WorldBounds = PrimitiveData->GetBounds();
		Entry.PrimitiveComponent = Component;
	}

	if (CachedPrimitives.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing primitive data."))
		}
		return PCGExFactories::EPreparationResult::MissingData;
	}

	// Build octree from cached primitive bounds
	FBox OctreeBounds(ForceInit);
	for (const PCGExPointFilter::FCachedPrimitive& Entry : CachedPrimitives)
	{
		OctreeBounds += Entry.WorldBounds;
	}

	Octree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
	for (int32 i = 0; i < CachedPrimitives.Num(); i++)
	{
		Octree->AddElement(PCGExOctree::FItem(i, CachedPrimitives[i].WorldBounds));
	}

	return Result;
}

void UPCGExPrimitiveFilterFactory::BeginDestroy()
{
	CachedPrimitives.Empty();
	Octree.Reset();
	Super::BeginDestroy();
}

#pragma endregion

#pragma region FPrimitiveFilter

bool PCGExPointFilter::FPrimitiveFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	CachedPrimitives = &TypedFilterFactory->CachedPrimitives;
	Octree = TypedFilterFactory->Octree.Get();
	if (!CachedPrimitives || CachedPrimitives->IsEmpty() || !Octree)
	{
		return false;
	}

	const FPCGExPrimitiveFilterConfig& Cfg = TypedFilterFactory->Config;
	BoundsSource = Cfg.BoundsSource;
	bInvert = Cfg.bInvert;

	return true;
}

bool PCGExPointFilter::FPrimitiveFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	return TestPoint(Point);
}

bool PCGExPointFilter::FPrimitiveFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
	return TestPoint(Point);
}

bool PCGExPointFilter::FPrimitiveFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	PCGExData::FProxyPoint ProxyPoint;
	IO->GetDataAsProxyPoint(ProxyPoint);
	return TestPoint(ProxyPoint);
}

#pragma endregion

#pragma region UPCGExPrimitiveFilterProviderSettings

TArray<FPCGPinProperties> UPCGExPrimitiveFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PRIMITIVES(FName("Primitives"), TEXT("Primitive data to test points against"), Required)
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Primitive)

#if WITH_EDITOR
FString UPCGExPrimitiveFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(TEXT("Overlaps"), Config.bInvert);
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
