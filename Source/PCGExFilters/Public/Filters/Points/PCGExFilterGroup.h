// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExClusterFilter.h"
#include "UObject/Object.h"
#include "PCGExFilterGroup.generated.h"

/**
 * Factory for composite filter groups (AND/OR). Aggregates child filter factories
 * and creates a FFilterGroup instance that evaluates them with the selected logic.
 * Inherits from ClusterFilterFactory to support both point and cluster child filters.
 * All registration methods (consumable attributes, assets, buffers) recurse into children.
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExFilterGroupFactoryData : public UPCGExClusterFilterFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoFilter)

	UPROPERTY()
	bool bInvert = false;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> FilterFactories;

	virtual bool SupportsProxyEvaluation() const override;
	virtual bool SupportsCollectionEvaluation() const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override
	{
		return nullptr;
	}

	virtual bool RegisterConsumableAttributes(FPCGExContext* InContext) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
	virtual void RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

/** AND group: all child filters must pass for the group to pass. */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFilterGroupFactoryDataAND : public UPCGExFilterGroupFactoryData
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

/** OR group: any child filter passing causes the group to pass. */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFilterGroupFactoryDataOR : public UPCGExFilterGroupFactoryData
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExFilterGroup
{
	/**
	 * Base class for composite filter groups. Manages its own internal filter stack
	 * (separate from FManager) and supports nested groups, cluster filters, and
	 * regular point filters. Handles both point and cluster initialization paths.
	 *
	 * Subclasses (FFilterGroupAND / FFilterGroupOR) implement the Test() logic:
	 * - AND: short-circuits on first failure, returns !bInvert
	 * - OR:  short-circuits on first success, returns !bInvert
	 */
	class PCGEXFILTERS_API FFilterGroup : public PCGExClusterFilter::IFilter
	{
	public:
		explicit FFilterGroup(const UPCGExFilterGroupFactoryData* InFactory, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
			: IFilter(InFactory)
			  , GroupFactory(InFactory)
			  , ManagedFactories(InFilterFactories)
		{
		}

		bool bValid = false;
		bool bInvert = false;
		const UPCGExFilterGroupFactoryData* GroupFactory;
		const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* ManagedFactories;

		virtual PCGExFilters::EType GetFilterType() const override
		{
			return PCGExFilters::EType::Group;
		}

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;

		virtual void PostInit() override;

		virtual bool Test(const int32 Index) const override = 0;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override = 0;
		virtual bool Test(const PCGExClusters::FNode& Node) const override = 0;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override = 0;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override = 0;

		virtual void SetSupportedTypes(const TSet<FPCGDataTypeBaseId>* InTypes) override;

	protected:
		const TSet<FPCGDataTypeBaseId>* SupportedFactoriesTypes = nullptr;
		TArray<TSharedPtr<PCGExPointFilter::IFilter>> ManagedFilters;
		TArray<const PCGExPointFilter::IFilter*> Stack;

		virtual bool InitManaged(FPCGExContext* InContext);
		bool InitManagedFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& Filter, const bool bQuiet = false) const;
		virtual bool PostInitManaged(FPCGExContext* InContext);
		virtual void PostInitManagedFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter);
	};

	class PCGEXFILTERS_API FFilterGroupAND final : public FFilterGroup
	{
	public:
		explicit FFilterGroupAND(const UPCGExFilterGroupFactoryData* InFactory, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
			: FFilterGroup(InFactory, InFilterFactories)
		{
		}

		virtual bool Test(const int32 Index) const override;
		virtual bool Test(const PCGExClusters::FNode& Node) const override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;
	};

	class PCGEXFILTERS_API FFilterGroupOR final : public FFilterGroup
	{
	public:
		explicit FFilterGroupOR(const UPCGExFilterGroupFactoryData* InFactory, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* InFilterFactories)
			: FFilterGroup(InFactory, InFilterFactories)
		{
		}

		virtual bool Test(const int32 Index) const override;
		virtual bool Test(const PCGExClusters::FNode& Node) const override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;
	};
}

#pragma region UPCGExFilterGroupProvider

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|FilterGroup", meta=(PCGExNodeLibraryDoc="filters/filter-group-and-or"))
class PCGEXFILTERS_API UPCGExFilterGroupProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoFilter)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FilterGroup, "Filter Group", "Creates an Filter Group.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterHub);
	}

	virtual bool ShouldDrawNodeCompact() const override
	{
		return bDrawAsCompactNode;
	}
	
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif
	//~End UPCGSettings

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

public:
	virtual FName GetMainOutputPin() const override;
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Filter Priority. Will use the highest value between the one set here and from the connected filters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;

	/** Filter Mode.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	EPCGExFilterGroupMode Mode = EPCGExFilterGroupMode::AND;

	/** Inverts the group output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	bool bInvert = false;
	
	/** Whether to draw this node as a compact node */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bDrawAsCompactNode = true;
};

#pragma endregion
