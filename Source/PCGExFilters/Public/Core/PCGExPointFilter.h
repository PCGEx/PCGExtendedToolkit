// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "UObject/Object.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Factories/PCGExFactoryData.h"

#include "PCGExPointFilter.generated.h"

namespace PCGExData
{
	class FPointIO;
	struct FProxyPoint;
	class FPointIOCollection;
}

namespace PCGExGraphs
{
	struct FEdge;
}

namespace PCGExClusters
{
	struct FNode;
}

class UPCGExFilterProviderSettings;

namespace PCGExPointFilter
{
	class IFilter;
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Filter"))
struct FPCGExDataTypeInfoFilter : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXFILTERS_API)
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Filter (Point)"))
struct FPCGExDataTypeInfoFilterPoint : public FPCGExDataTypeInfoFilter
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXFILTERS_API)
};

/**
 * Base factory for all PCGEx filters. Factories are UObject-based configuration holders
 * that create lightweight filter instances (IFilter) for runtime evaluation.
 *
 * To create a new filter type:
 * 1. Subclass UPCGExPointFilterFactoryData (or UPCGExClusterFilterFactoryData for cluster filters)
 * 2. Add a Config UPROPERTY struct with your filter settings
 * 3. Override CreateFilter() to return a new instance of your filter class
 * 4. Override Init() if you need to validate settings (e.g. check selectors against data)
 * 5. Create a matching UPCGExFilterProviderSettings subclass as the PCG node
 *
 * Key policies:
 * - InitializationFailurePolicy: What happens when Init fails (error, pass-all, or fail-all)
 * - MissingDataPolicy: What happens when required input data is missing
 * - Priority: Controls evaluation order in the filter stack (lower = evaluated first)
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExFilterFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

	friend UPCGExFilterProviderSettings;

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoFilter)

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::Filter; }

	virtual bool DomainCheck();
	virtual bool GetOnlyUseDataDomain() const { return bOnlyUseDataDomain; }

	virtual bool SupportsCollectionEvaluation() const { return bOnlyUseDataDomain; }
	virtual bool SupportsProxyEvaluation() const { return false; }

	virtual bool Init(FPCGExContext* InContext);

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const;

	int32 Priority = 0;
	EPCGExFilterNoDataFallback InitializationFailurePolicy = EPCGExFilterNoDataFallback::Error;
	EPCGExFilterNoDataFallback MissingDataPolicy = EPCGExFilterNoDataFallback::Fail;

protected:
	bool bOnlyUseDataDomain = false;
};

/**
 * Base factory for point-level filters. Most custom filters should subclass this.
 * The factory type is automatically set to FilterPoint, which the filter system uses
 * to distinguish point filters from cluster or collection filters.
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExPointFilterFactoryData : public UPCGExFilterFactoryData
{
	GENERATED_BODY()

	friend UPCGExFilterProviderSettings;

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoFilterPoint)

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::FilterPoint; }
};

namespace PCGExPointFilter
{
	/**
	 * Base runtime filter instance. Created by a factory and evaluated by the FManager.
	 * Lightweight (TSharedFromThis, not UObject) for efficient per-point evaluation.
	 *
	 * Subclass guide:
	 * - Override Init() to fetch attribute readers/broadcasters from the PointDataFacade
	 * - Override Test(int32 Index) for per-point evaluation (the primary entry point)
	 * - Override Test(FProxyPoint) only for context-free evaluation (no attribute access)
	 * - Node/Edge Test() overloads default to routing through Test(PointIndex)
	 * - Test(FPointIO, FPointIOCollection) is for collection-level evaluation only
	 *
	 * The FManager calls Test() in an AND-stack: all filters must pass for a point to pass.
	 * Results can be cached in the Results array when bCacheResults is true.
	 */
	class PCGEXFILTERS_API IFilter : public TSharedFromThis<IFilter>
	{
	public:
		explicit IFilter(const TObjectPtr<const UPCGExPointFilterFactoryData>& InFactory)
			: Factory(InFactory)
		{
		}

		bool bWillBeUsedWithCollections = false;
		bool bUseDataDomainSelectorsOnly = false;
		bool bCollectionTestResult = true;
		bool bUseEdgeAsPrimary = false; // This shouldn't be there but...

		bool DefaultResult = true;
		TSharedPtr<PCGExData::FFacade> PointDataFacade;

		bool bCacheResults = true;
		TObjectPtr<const UPCGExPointFilterFactoryData> Factory;
		TArray<int8> Results;

		int32 FilterIndex = 0;

		virtual PCGExFilters::EType GetFilterType() const { return PCGExFilters::EType::Point; }

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade);
		virtual void PostInit();

		virtual bool Test(const int32 Index) const;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const; // destined for no-context evaluation only, can't rely on attributes or anything.
		virtual bool Test(const PCGExClusters::FNode& Node) const;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const;

		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const; // destined for collection only, is expected to test internal PointDataFacade directly.

		virtual void SetSupportedTypes(const TSet<PCGExFactories::EType>* InTypes)
		{
		}

		virtual ~IFilter() = default;
	};

	/**
	 * Convenience base for filters that only care about per-point data.
	 * Node and Edge Test() overloads are marked final and delegate to Test(PointIndex),
	 * so subclasses only need to override Test(int32 Index).
	 * This is the recommended base class for most custom point filters.
	 */
	class PCGEXFILTERS_API ISimpleFilter : public IFilter
	{
	public:
		explicit ISimpleFilter(const TObjectPtr<const UPCGExPointFilterFactoryData>& InFactory)
			: IFilter(InFactory)
		{
		}

		virtual bool Test(const int32 Index) const override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const PCGExClusters::FNode& Node) const override final;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override final;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;
	};

	/**
	 * Base for filters that evaluate a data collection as a whole rather than individual points.
	 * The result is computed once during Init() via Test(FPointIO, FPointIOCollection) and cached
	 * in bCollectionTestResult. All per-point Test() overloads simply return this cached value.
	 *
	 * Subclass guide:
	 * - Override Test(FPointIO, FPointIOCollection) with your collection-level logic
	 * - Per-point Test() calls are all final and return the cached boolean
	 */
	class PCGEXFILTERS_API ICollectionFilter : public IFilter
	{
	public:
		explicit ICollectionFilter(const TObjectPtr<const UPCGExPointFilterFactoryData>& InFactory)
			: IFilter(InFactory)
		{
		}

		virtual PCGExFilters::EType GetFilterType() const override { return PCGExFilters::EType::Collection; }

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 Index) const override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const PCGExClusters::FNode& Node) const override final;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override final;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;
	};

	/**
	 * Aggregates multiple IFilter instances into an AND-stack and provides batch evaluation.
	 *
	 * Lifecycle:
	 * 1. Init() creates filter instances from factories, handles InitializationFailurePolicy fallback
	 * 2. PostInit() sorts filters by priority, builds the raw-pointer Stack for fast iteration
	 * 3. Test() evaluates the stack -- all filters must pass (short-circuit on first failure)
	 *
	 * Batch Test() overloads accept a scope/range and optionally run in parallel via ParallelFor.
	 * They return the number of passing items. Parallel paths use InterlockedIncrement for the count.
	 *
	 * Extension points:
	 * - Override InitFilter() to customize how filters are initialized (see PCGExClusterFilter::FManager)
	 * - Override PostInit() to inject additional setup after all filters are ready
	 * - Override InitCache() to change how the manager-level Results array is allocated
	 */
	class PCGEXFILTERS_API FManager : public TSharedFromThis<FManager>
	{
	public:
		explicit FManager(const TSharedRef<PCGExData::FFacade>& InPointDataFacade);

		bool bUseEdgeAsPrimary = false; // This shouldn't be there...
		bool bWillBeUsedWithCollections = false;

		bool bCacheResultsPerFilter = false;
		bool bCacheResults = false;
		TArray<int8> Results;

		bool bValid = false;

		TSharedRef<PCGExData::FFacade> PointDataFacade;

		bool Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories);

		virtual bool Test(const int32 Index);
		virtual bool Test(const PCGExData::FProxyPoint& Point);
		virtual bool Test(const PCGExClusters::FNode& Node);
		virtual bool Test(const PCGExGraphs::FEdge& Edge);
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection);

		virtual int32 Test(const PCGExMT::FScope Scope, TArray<int8>& OutResults, const bool bParallel = false);
		virtual int32 Test(const PCGExMT::FScope Scope, TBitArray<>& OutResults, const bool bParallel = false);

		virtual int32 Test(const TArrayView<PCGExClusters::FNode> Items, const TArrayView<int8> OutResults, const bool bParallel = false);
		virtual int32 Test(const TArrayView<PCGExClusters::FNode> Items, const TSharedPtr<TArray<int8>>& OutResultsPtr, const bool bParallel = false);
		virtual int32 Test(const TArrayView<PCGExGraphs::FEdge> Items, const TArrayView<int8> OutResults, const bool bParallel = false);

		virtual ~FManager()
		{
		}

		void SetSupportedTypes(const TSet<PCGExFactories::EType>* InTypes);
		const TSet<PCGExFactories::EType>* GetSupportedTypes() const;

	protected:
		const TSet<PCGExFactories::EType>* SupportedFactoriesTypes = nullptr;
		TArray<TSharedPtr<IFilter>> ManagedFilters;       // Owns the filter instances
		TArray<const IFilter*> Stack;                      // Raw pointers for cache-friendly iteration in Test()

		virtual bool InitFilter(FPCGExContext* InContext, const TSharedPtr<IFilter>& Filter);
		virtual bool PostInit(FPCGExContext* InContext);
		virtual void PostInitFilter(FPCGExContext* InContext, const TSharedPtr<IFilter>& InFilter);

		virtual void InitCache();
	};

	PCGEXFILTERS_API
	void RegisterBuffersDependencies(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories, PCGExData::FFacadePreloader& FacadePreloader);

	PCGEXFILTERS_API
	void PruneForDirectEvaluation(FPCGExContext* InContext, TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories);
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Filter (Data)"))
struct FPCGExDataTypeInfoFilterCollection : public FPCGExDataTypeInfoFilter
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXFILTERS_API)
};

/**
 * Factory for collection-level filters. These evaluate entire data collections
 * rather than individual points
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExFilterCollectionFactoryData : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()
	
public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoFilterCollection)

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::FilterCollection; }
	virtual bool DomainCheck() override;
	virtual bool SupportsCollectionEvaluation() const override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override { return nullptr; }
};
