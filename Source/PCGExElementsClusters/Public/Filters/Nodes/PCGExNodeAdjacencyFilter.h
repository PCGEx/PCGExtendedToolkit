// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExSettingsMacros.h"
#include "Filters/PCGExAdjacency.h"

#include "PCGExNodeAdjacencyFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNodeAdjacencyFilterConfig
{
	GENERATED_BODY()

	FPCGExNodeAdjacencyFilterConfig() = default;

	/** Adjacency Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExAdjacencySettings Adjacency;

	/** Type of OperandA */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand A for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand A (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides, ShowOnlyInnerProperties))
	FPCGAttributePropertyInputSelector OperandA;

	/** Constant Operand A for testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand A", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	double OperandAConstant = 0;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Source of the Operand B value -- either the neighboring point, or the edge connecting to that point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExClusterElement OperandBSource = EPCGExClusterElement::Vtx;

	/** Operand B for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ShowOnlyInnerProperties, DisplayName="Operand B (Neighbor)"))
	FPCGAttributePropertyInputSelector OperandB;

	/** Rounding mode for near measures */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	PCGEX_SETTING_VALUE_DECL(OperandA, double)
	PCGEX_SETTING_VALUE_DECL(OperandB, double)
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExNodeAdjacencyFilterFactory : public UPCGExNodeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNodeAdjacencyFilterConfig Config;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

class FNodeAdjacencyFilter final : public PCGExClusterFilter::IVtxFilter
{
public:
	explicit FNodeAdjacencyFilter(const UPCGExNodeAdjacencyFilterFactory* InFactory)
		: IVtxFilter(InFactory)
		  , TypedFilterFactory(InFactory)
	{
		Adjacency = InFactory->Config.Adjacency;
	}

	const UPCGExNodeAdjacencyFilterFactory* TypedFilterFactory;

	bool bCaptureFromNodes = false;

	TArray<double> CachedThreshold;
	FPCGExAdjacencySettings Adjacency;

	TSharedPtr<PCGExDetails::TSettingValue<double>> OperandA;
	TSharedPtr<PCGExDetails::TSettingValue<double>> OperandB;

	using TestCallback = std::function<bool(const PCGExClusters::FNode&, const TArray<PCGExClusters::FNode>&, const double A)>;
	TestCallback TestSubFunc;

	virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;

	virtual bool Test(const PCGExClusters::FNode& Node) const override;

	virtual ~FNodeAdjacencyFilter() override;
};


/** Outputs a single GraphParam to be consumed by other nodes */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/vtx-filters/vtx-filter-adjacency"))
class UPCGExNodeAdjacencyFilterProviderSettings : public UPCGExVtxFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NodeAdjacencyFilterFactory, "Vtx Filter : Adjacency", "Numeric comparison of adjacent values, testing either adjacent nodes or connected edges.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterCluster);
	}
#endif

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNodeAdjacencyFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
