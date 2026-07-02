// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPointStates.h"
#include "UObject/Object.h"

#include "Core/PCGExClusterFilter.h"
#include "PCGExClusterStates.generated.h"


USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExClusterStateConfigBase : public FPCGExStateConfigBase
{
	GENERATED_BODY()

	FPCGExClusterStateConfigBase()
	{
	}
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | State : Cluster"))
struct FPCGExDataTypeInfoClusterState : public FPCGExDataTypeInfoPointState
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXFILTERS_API)
};

/**
 * Cluster-aware variant of the state factory. Creates FState instances that
 * use a cluster filter manager internally, giving the state's filters access
 * to full cluster topology (nodes, edges, adjacency).
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExClusterStateFactoryData : public UPCGExPointStateFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoClusterState)

	UPROPERTY()
	FPCGExClusterStateConfigBase Config;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual void BeginDestroy() override;
};

namespace PCGExClusterStates
{
	class PCGEXFILTERS_API FState : public PCGExClusterFilter::IFilter
	{
	public:
		FPCGExClusterStateConfigBase Config;
		FPCGExStateConfigBase BaseConfig;

		const UPCGExClusterStateFactoryData* StateFactory = nullptr;

		explicit FState(const UPCGExClusterStateFactoryData* InFactory)
			: IFilter(InFactory)
			  , StateFactory(InFactory)
		{
		}

		virtual ~FState() override;

		virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
		bool InitInternalManager(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories);

		virtual bool Test(const int32 Index) const override;
		virtual bool Test(const PCGExClusters::FNode& Node) const override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;

		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const int32 Index) const;
		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExClusters::FNode& Node) const;
		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExGraphs::FEdge& Edge) const;

	protected:
		TSharedPtr<PCGExClusterFilter::FManager> Manager;
	};

	class PCGEXFILTERS_API FStateManager final : public PCGExClusterFilter::FManager
	{
		TArray<TSharedPtr<FState>> States;
		TSharedPtr<TArray<int64>> FlagsCache;

	public:
		explicit FStateManager(const TSharedPtr<TArray<int64>>& InFlags, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataCache, const TSharedRef<PCGExData::FFacade>& InEdgeDataCache);

		virtual bool Test(const int32 Index) override;
		virtual bool Test(const PCGExClusters::FNode& Node) override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) override;

	protected:
		virtual void PostInitFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter) override;
	};
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/metadata/flag-nodes/node-flag"))
class PCGEXFILTERS_API UPCGExClusterStateFactoryProviderSettings : public UPCGExStateFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoClusterState)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ClusterNodeFlag, "State : Cluster", "A single, filter-driven vtx state.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterState);
	}
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties, DisplayAfter="Name"))
	FPCGExClusterStateConfigBase Config;

protected:
	virtual TSet<FPCGDataTypeBaseId> GetInternalFilterTypes() const override;
};
