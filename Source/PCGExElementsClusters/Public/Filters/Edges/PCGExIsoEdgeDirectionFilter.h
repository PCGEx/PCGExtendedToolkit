// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Sorting/PCGExSortingDetails.h"
#include "PCGExCoreMacros.h"
#include "Clusters/PCGExEdgeDirectionDetails.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExSettingsMacros.h"
#include "Filters/PCGExAdjacency.h"

#include "PCGExIsoEdgeDirectionFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExIsoEdgeDirectionFilterConfig
{
	GENERATED_BODY()

	FPCGExIsoEdgeDirectionFilterConfig() = default;

	/** Defines how the edge's endpoints are ordered to establish its direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExEdgeDirectionSettings DirectionSettings;

	/** Type of check. Dot compares angles precisely; Hash (Fast) compares quantized directions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExDirectionCheckMode ComparisonQuality = EPCGExDirectionCheckMode::Dot;

	/** Where to read the compared direction from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Direction to compare the edge against -- Read as an FVector direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Direction (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector Direction;

	/** Flip the direction vector before comparison. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Invert", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	bool bInvertDirection = false;

	/** Direction to compare the edge against. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Direction", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	FVector DirectionConstant = FVector::UpVector;

	/** Transform the reference direction with the local point' transform */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bTransformDirection = false;

	/** Dot comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="ComparisonQuality == EPCGExDirectionCheckMode::Dot", EditConditionHides))
	FPCGExDotComparisonDetails DotComparisonDetails;

	/** Hash comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="ComparisonQuality == EPCGExDirectionCheckMode::Hash", EditConditionHides))
	FPCGExVectorHashComparisonDetails HashComparisonDetails;

	PCGEX_SETTING_VALUE_DECL(Direction, FVector)
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExIsoEdgeDirectionFilterFactory : public UPCGExEdgeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExIsoEdgeDirectionFilterConfig Config;

	UPROPERTY()
	TArray<FPCGExSortRuleConfig> EdgeSortingRules;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

class FIsoEdgeDirectionFilter final : public PCGExClusterFilter::IEdgeFilter
{
public:
	explicit FIsoEdgeDirectionFilter(const UPCGExIsoEdgeDirectionFilterFactory* InFactory);

	const UPCGExIsoEdgeDirectionFilterFactory* TypedFilterFactory;

	bool bUseDot = true;
	double DirectionMultiplier = 1.0;

	FPCGExEdgeDirectionSettings DirectionSettings;
	FPCGExDotComparisonDetails DotComparison;
	FPCGExVectorHashComparisonDetails HashComparison;

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> OperandDirection;

	TConstPCGValueRange<FTransform> InTransforms;

	virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;

	bool TestDot(const int32 PtIndex, const FVector& EdgeDir) const;
	bool TestHash(const int32 PtIndex, const FVector& EdgeDir) const;

	virtual ~FIsoEdgeDirectionFilter() override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/edge-filters/edge-filter-edge-direction"))
class UPCGExIsoEdgeDirectionFilterProviderSettings : public UPCGExEdgeFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(IsoEdgeDirectionFilterFactory, "Edge Filter : Edge Direction", "Dot product comparison of the edge direction against a local attribute or constant.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterCluster);
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExIsoEdgeDirectionFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
