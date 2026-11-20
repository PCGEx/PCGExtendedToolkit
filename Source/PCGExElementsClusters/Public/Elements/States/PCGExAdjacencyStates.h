// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExClusterStates.h"
#include "UObject/Object.h"
#include "PCGExAdjacencyStates.generated.h"

namespace PCGExBitmask
{
	class FBitmaskData;
}

USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERS_API FPCGExAdjacencyStateConfigBase
{
	GENERATED_BODY()

	FPCGExAdjacencyStateConfigBase()
	{
	}

	// TODO : Bitmask refs
	// TODO : Bitmask collections
	// TODO : Angle threshold (same for all)

	/** Shared angle threshold */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Angle = 22.5;

	/** Whether to transform directions using the vtx' point transform */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTransformDirection = true;

	/** Operations executed on the flag if all filters pass (or if no filter is set) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TArray<FPCGExBitmaskRef> Compositions;

	/** Operations executed on the flag if all filters pass (or if no filter is set) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TMap<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR> Collections;

	/** If enabled, and if filters exist, will apply alternative bitwise operations when filters fail. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseAlternativeBitmasksOnFilterFail = false;

	/** Operations executed on the flag if any filters fails */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" ├─ Compositions", EditCondition="bUseAlternativeBitmasksOnFilterFail"))
	TArray<FPCGExBitmaskRef> OnFailCompositions;

	/** Operations executed on the flag if any filters fails */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Collections", EditCondition="bUseAlternativeBitmasksOnFilterFail"))
	TMap<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR> OnFailCollections;

	/** Whether to invert the dot product check. Bitmasks will be applied with direction does NOT match. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};

/**
 * 
 */
UCLASS(Hidden, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSCLUSTERS_API UPCGExAdjacencyStateFactoryData : public UPCGExClusterStateFactoryData
{
	GENERATED_BODY()

public:
	bool bTransformDirection = true;
	bool bInvert = false;
	TSharedPtr<PCGExBitmask::FBitmaskData> SuccessBitmaskData;
	TSharedPtr<PCGExBitmask::FBitmaskData> FailBitmaskData;

	virtual bool GetRequiresFilters() const override
	{
		return false;
	}

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExAdjacencyStates
{
	class PCGEXELEMENTSCLUSTERS_API FState final : public PCGExClusterStates::FState
	{
	protected:
		TConstPCGValueRange<FTransform> InTransformRange;

	public:
		bool bTransformDirection = true;
		bool bInvert = false;
		TSharedPtr<PCGExBitmask::FBitmaskData> SuccessBitmaskData;
		TSharedPtr<PCGExBitmask::FBitmaskData> FailBitmaskData;

		const UPCGExAdjacencyStateFactoryData* StateFactory = nullptr;

		explicit FState(const UPCGExAdjacencyStateFactoryData* InFactory)
			: PCGExClusterStates::FState(InFactory)
			  , StateFactory(InFactory)
		{
		}

		virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;

		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const int32 Index) const override;
		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExClusters::FNode& Node) const override;
		virtual void ProcessFlags(const bool bSuccess, int64& InFlags, const PCGExGraphs::FEdge& Edge) const override;

	protected:
		TSharedPtr<PCGExClusterFilter::FManager> Manager;
	};
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-write-states/state-bitmask-adjacency"))
class PCGEXELEMENTSCLUSTERS_API UPCGExAdjacencyStateFactoryProviderSettings : public UPCGExStateFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoClusterState)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ClusterNodeFlag, "State : Bitmask Adjacency", "A bulk-check for directional adjacency, using bitmask collections", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	virtual bool CanOutputBitmasks() const override
	{
		return false;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties, DisplayAfter="Name"))
	FPCGExAdjacencyStateConfigBase Config;
};
