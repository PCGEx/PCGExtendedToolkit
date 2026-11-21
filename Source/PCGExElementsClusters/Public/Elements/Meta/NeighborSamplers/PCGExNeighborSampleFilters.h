// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExNeighborSampleFactoryProvider.h"
#include "Factories/PCGExFactoryProvider.h"
#include "PCGExNeighborSampleFilters.generated.h"

///

USTRUCT(BlueprintType)
struct FPCGExSamplerFilterConfig
{
	GENERATED_BODY()

	FPCGExSamplerFilterConfig()
	{
	}

	/** Write the count of neighbors that passed filters to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteInsideNum = false;

	/** Name of the attribute to write the number of tests that passed (inside filters) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Inside Num", EditCondition="bWriteInsideNum"))
	FName InsideNumAttributeName = FName(FName("InsideNum"));

	/** If enabled, outputs the value divided by the total number of samples */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = " └─ Normalize", EditCondition="bWriteInsideNum", EditConditionHides, HideEditConditionToggle))
	bool bNormalizeInsideNum = false;

	/** Write the count of neighbors that failed filters to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle, InlineEditConditionToggle))
	bool bWriteOutsideNum = false;

	/** Name of the attribute to write the number of tests that failed (outside filters) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Outside Num", EditCondition="bWriteOutsideNum"))
	FName OutsideNumAttributeName = FName(FName("OutsideNum"));

	/** If enabled, outputs the value divided by the total number of samples */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = " └─ Normalize", EditCondition="bWriteOutsideNum", EditConditionHides, HideEditConditionToggle))
	bool bNormalizeOutsideNum = false;

	/** Write the total number of neighbors tested to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTotalNum = false;

	/** Name of the attribute to write the total number of points tested */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Total Num", EditCondition="bWriteTotalNum"))
	FName TotalNumAttributeName = FName(FName("TotalNum"));


	/** Write the weighted sum of neighbors that passed filters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteInsideWeight = false;

	/** Name of the attribute to write the number of tests weight that passed (inside filters) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Inside Weight", EditCondition="bWriteInsideWeight"))
	FName InsideWeightAttributeName = FName(FName("InsideWeight"));

	/** If enabled, outputs the value divided by the total weight of samples */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = " └─ Normalize", EditCondition="bWriteInsideWeight", EditConditionHides, HideEditConditionToggle))
	bool bNormalizeInsideWeight = false;


	/** Write the weighted sum of neighbors that failed filters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOutsideWeight = false;

	/** Name of the attribute to write the number of tested weight that passed (inside filters) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Outside Weight", EditCondition="bWriteOutsideWeight"))
	FName OutsideWeightAttributeName = FName(FName("OutsideWeight"));

	/** If enabled, outputs the value divided by the total weight of samples */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = " └─ Normalize", EditCondition="bWriteOutsideWeight", EditConditionHides, HideEditConditionToggle))
	bool bNormalizeOutsideWeight = false;


	/** Write the total weight of all neighbors tested. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTotalWeight = false;

	/** Name of the attribute to write the total weight tested */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName = "Total Weight", EditCondition="bWriteTotalWeight"))
	FName TotalWeightAttributeName = FName(FName("TotalWeight"));
};

/**
 * 
 */
class FPCGExNeighborSampleFilters : public FPCGExNeighborSampleOperation
{
public:
	FPCGExSamplerFilterConfig Config;
	TSharedPtr<PCGExClusterFilter::FManager> FilterManager;

	virtual void PrepareForCluster(FPCGExContext* InContext, TSharedRef<PCGExClusters::FCluster> InCluster, TSharedRef<PCGExData::FFacade> InVtxDataFacade, TSharedRef<PCGExData::FFacade> InEdgeDataFacade) override;

	virtual void SampleNeighborNode(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope) override;
	virtual void SampleNeighborEdge(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope) override;
	virtual void FinalizeNode(const PCGExClusters::FNode& TargetNode, const int32 Count, const double TotalWeight, const PCGExMT::FScope& Scope) override;

	virtual void CompleteOperation() override;

protected:
	TArray<int32> Inside;
	TArray<double> InsideWeight;
	TArray<int32> Outside;
	TArray<double> OutsideWeight;

	TSharedPtr<PCGExData::TBuffer<int32>> NumInsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> NormalizedNumInsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> WeightInsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> NormalizedWeightInsideBuffer;

	TSharedPtr<PCGExData::TBuffer<int32>> NumOutsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> NormalizedNumOutsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> WeightOutsideBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> NormalizedWeightOutsideBuffer;

	TSharedPtr<PCGExData::TBuffer<int32>> TotalNumBuffer;
	TSharedPtr<PCGExData::TBuffer<int32>> TotalWeightBuffer;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExNeighborSamplerFactoryFilters : public UPCGExNeighborSamplerFactoryData
{
	GENERATED_BODY()

public:
	FPCGExSamplerFilterConfig Config;
	virtual TSharedPtr<FPCGExNeighborSampleOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|NeighborSample", meta=(PCGExNodeLibraryDoc="sampling/cluster-sample-neighbors/sampler-test-neighbors"))
class UPCGExNeighborSampleFiltersSettings : public UPCGExNeighborSampleProviderSettings
{
	GENERATED_BODY()

public:
	UPCGExNeighborSampleFiltersSettings(const FObjectInitializer& ObjectInitializer);

protected:
	virtual bool SupportsVtxFilters(bool& bIsRequired) const override;
	virtual bool SupportsEdgeFilters(bool& bIsRequired) const override;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(NeighborSamplerFilters, "Sampler : Test Neighbors", "Writes the number of neighbors that pass the provided filters")

#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Sampler Settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSamplerFilterConfig Config;
};
