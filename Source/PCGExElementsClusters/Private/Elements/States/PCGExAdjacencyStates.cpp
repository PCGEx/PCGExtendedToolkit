// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/States/PCGExAdjacencyStates.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExClusterFilter.h"
#include "Data/PCGExData.h"
#include "Data/Bitmasks/PCGExBitmaskData.h"

TSharedPtr<PCGExPointFilter::IFilter> UPCGExAdjacencyStateFactoryData::CreateFilter() const
{
	PCGEX_MAKE_SHARED(NewState, PCGExAdjacencyStates::FState, this)
	NewState->bInvert = bInvert;
	NewState->bTransformDirection = bTransformDirection;
	NewState->SuccessBitmaskData = SuccessBitmaskData;
	NewState->FailBitmaskData = FailBitmaskData;
	return NewState;
}

namespace PCGExAdjacencyStates
{
	bool FState::Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
	{
		if (!PCGExClusterStates::FState::Init(InContext, InCluster, InPointDataFacade, InEdgeDataFacade))
		{
			return false;
		}

		InTransformRange = InPointDataFacade->GetIn()->GetConstTransformValueRange();

		return true;
	}

	FState::~FState()
	{
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const int32 Index) const
	{
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExClusters::FNode& Node) const
	{
		if (!bSuccess && !FailBitmaskData)
		{
			return;
		}

		const FTransform& InTransform = InTransformRange[Node.PointIndex];

		if (bInvert)
		{
			for (const PCGExGraphs::FLink& Lk : Node.Links)
			{
				FVector Dir = Cluster->GetDir(Node.Index, Lk.Node);
				if (bTransformDirection)
				{
					Dir = InTransform.InverseTransformVectorNoScale(Dir);
				}
				if (bSuccess)
				{
					SuccessBitmaskData->MutateUnmatch(Dir, InFlags);
				}
				else
				{
					FailBitmaskData->MutateUnmatch(Dir, InFlags);
				}
			}
		}
		else
		{
			for (const PCGExGraphs::FLink& Lk : Node.Links)
			{
				FVector Dir = Cluster->GetDir(Node.Index, Lk.Node);
				if (bTransformDirection)
				{
					Dir = InTransform.InverseTransformVectorNoScale(Dir);
				}
				if (bSuccess)
				{
					SuccessBitmaskData->MutateMatch(Dir, InFlags);
				}
				else
				{
					FailBitmaskData->MutateMatch(Dir, InFlags);
				}
			}
		}
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExGraphs::FEdge& Edge) const
	{
	}
}

TArray<FPCGPinProperties> UPCGExAdjacencyStateFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceFiltersLabel, TEXT("Filters used to check which node should be processed."), Advanced)
	return PinProperties;
}

UPCGExFactoryData* UPCGExAdjacencyStateFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExAdjacencyStateFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExAdjacencyStateFactoryData>();
	if (!Super::CreateFactory(InContext, NewFactory))
	{
		return nullptr;
	}

	TSharedPtr<PCGExBitmask::FBitmaskData> SuccessBitmaskData = MakeShared<PCGExBitmask::FBitmaskData>();
	TSharedPtr<PCGExBitmask::FBitmaskData> FailBitmaskData = nullptr;

	if (Config.bUseAlternativeBitmasksOnFilterFail)
	{
		FailBitmaskData = PCGExBitmask::FBitmaskData::Make(Config.OnFailCollections, Config.OnFailCompositions, Config.Angle);
	}

	SuccessBitmaskData = PCGExBitmask::FBitmaskData::Make(Config.Collections, Config.Compositions, Config.Angle);

	NewFactory->bTransformDirection = Config.bTransformDirection;
	NewFactory->bInvert = Config.bInvert;
	NewFactory->SuccessBitmaskData = SuccessBitmaskData;
	NewFactory->FailBitmaskData = FailBitmaskData;

	return NewFactory;
}
