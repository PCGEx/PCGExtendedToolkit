// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "UObject/Object.h"

#include "Factories/PCGExFactoryProvider.h"

#include "Details/PCGExBlendingDetails.h"
#include "Factories/PCGExOperation.h"

#include "Core/PCGExClusterFilter.h"
#include "Elements/PCGExFilterVtx.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExNeighborSampleFactoryProvider.generated.h"

/// 
#define PCGEX_SAMPLER_CREATE_OPERATION\
	NewOperation->SamplingConfig = SamplingConfig; \
	NewOperation->WeightLUT = SamplingConfig.WeightLUT;\
	NewOperation->VtxFilterFactories.Append(VtxFilterFactories); \
	NewOperation->EdgesFilterFactories.Append(EdgesFilterFactories); \
	NewOperation->ValueFilterFactories.Append(ValueFilterFactories);

namespace PCGExGraphs
{
	struct FLink;
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Neighbor Sampler"))
struct FPCGExDataTypeInfoNeighborSampler : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXELEMENTSCLUSTERS_API)
};

namespace PCGExNeighborSample
{
	const FName SourceSamplersLabel = TEXT("Samplers");
	const FName OutputSamplerLabel = TEXT("Sampler");
}

USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERS_API FPCGExSamplingConfig
{
	GENERATED_BODY()

	FPCGExSamplingConfig()
	{
		LocalWeightCurve.EditorCurveData.AddKey(0, 0);
		LocalWeightCurve.EditorCurveData.AddKey(1, 1);
	}

	UPROPERTY(meta=(PCG_NotOverridable))
	bool bSupportsBlending = true;

	/** Type of range for weight blending computation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExRangeType RangeType = EPCGExRangeType::FullRange;

	/** The maximum sampling traversal depth */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=1))
	int32 MaxDepth = 1;

	/** How to compute the initial blend weight */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSupportsBlending", EditConditionHides, HideEditConditionToggle))
	EPCGExBlendOver BlendOver = EPCGExBlendOver::Index;

	/** Max dist */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="BlendOver == EPCGExBlendOver::Distance", EditConditionHides, ClampMin=0.001))
	double MaxDistance = 300;

	/** The fixed blending value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.001, EditCondition="bSupportsBlending && BlendOver == EPCGExBlendOver::Fixed", EditConditionHides, HideEditConditionToggle))
	double FixedBlend = 1;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve over which the sampling will be remapped. Used differently depending on sampler.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Weight Curve", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalWeightCurve;

	/** Curve over which the sampling will be remapped. Used differently depending on sampler.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight Curve", EditCondition="!bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> WeightCurve = TSoftObjectPtr<UCurveFloat>(PCGExCurves::WeightDistributionLinear);

	PCGExFloatLUT WeightLUT = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails WeightCurveLookup;

	/** Which type of neighbor to sample */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExClusterElement NeighborSource = EPCGExClusterElement::Vtx;

	void Init();
};


/**
 * 
 */
class PCGEXELEMENTSCLUSTERS_API FPCGExNeighborSampleOperation : public FPCGExOperation
{
public:
	TSharedPtr<PCGExClusterFilter::FManager> PointFilters;
	TSharedPtr<PCGExClusterFilter::FManager> ValueFilters;

	TSharedPtr<PCGExData::FFacade> VtxDataFacade;
	TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

	FPCGExSamplingConfig SamplingConfig;

	PCGExFloatLUT WeightLUT = nullptr;

	virtual void PrepareForCluster(FPCGExContext* InContext, TSharedRef<PCGExClusters::FCluster> InCluster, TSharedRef<PCGExData::FFacade> InVtxDataFacade, TSharedRef<PCGExData::FFacade> InEdgeDataFacade);
	virtual bool IsOperationValid();

	TSharedRef<PCGExData::FPointIO> GetSourceIO() const;
	TSharedRef<PCGExData::FFacade> GetSourceDataFacade() const;

	virtual void PrepareForLoops(const TArray<PCGExMT::FScope>& Loops);

	virtual void ProcessNode(const int32 NodeIndex, const PCGExMT::FScope& Scope);
	virtual void PrepareNode(const PCGExClusters::FNode& TargetNode, const PCGExMT::FScope& Scope) const;

	virtual void SampleNeighborNode(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope);
	virtual void SampleNeighborEdge(const PCGExClusters::FNode& TargetNode, const PCGExGraphs::FLink Lk, const double Weight, const PCGExMT::FScope& Scope);

	virtual void FinalizeNode(const PCGExClusters::FNode& TargetNode, const int32 Count, const double TotalWeight, const PCGExMT::FScope& Scope);
	virtual void CompleteOperation();

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgesFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> ValueFilterFactories;

protected:
	bool bIsValidOperation = true;
	TSharedPtr<PCGExClusters::FCluster> Cluster;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSCLUSTERS_API UPCGExNeighborSamplerFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoNeighborSampler)

	UPROPERTY()
	FPCGExSamplingConfig SamplingConfig;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgesFilterFactories;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> ValueFilterFactories;

	virtual TSharedPtr<FPCGExNeighborSampleOperation> CreateOperation(FPCGExContext* InContext) const;

	virtual void RegisterVtxBuffersDependencies(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, PCGExData::FFacadePreloader& FacadePreloader) const;

	virtual void RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const override;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|NeighborSample")
class PCGEXELEMENTSCLUSTERS_API UPCGExNeighborSampleProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoNeighborSampler)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	//PCGEX_NODE_INFOS_CUSTOM_SUBTITLE( NeighborSamplerAttribute, "Sampler : Abstract", "Abstract sampler settings.", PCGEX_FACTORY_NAME_PRIORITY)
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(NeighborSampler);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

	virtual bool SupportsVtxFilters(bool& bIsRequired) const;
	virtual bool SupportsEdgeFilters(bool& bIsRequired) const;

	//~Begin UPCGExFactoryProviderSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExNeighborSample::OutputSamplerLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
	//~End UPCGExFactoryProviderSettings

	/** Priority for sampling order. Higher values are processed last. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;

	/** Priority for sampling order. Higher values are processed last. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1, ShowOnlyInnerProperties))
	FPCGExSamplingConfig SamplingConfig;
};
