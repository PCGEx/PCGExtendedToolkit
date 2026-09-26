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
#include "Details/PCGExInputShorthandsDetails.h"
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

	/** Direction to compare the edge against -- Read as an FVector direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Direction"))
	FPCGExInputShorthandSelectorDirection DirectionValue = FPCGExInputShorthandSelectorDirection(FPCGAttributePropertyInputSelector(), FVector::UpVector);

	/** Transform the reference direction with the local point' transform */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bTransformDirection = false;

	/** Dot comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="ComparisonQuality == EPCGExDirectionCheckMode::Dot", EditConditionHides))
	FPCGExDotComparisonDetails DotComparisonDetails;

	/** Hash comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="ComparisonQuality == EPCGExDirectionCheckMode::Hash", EditConditionHides))
	FPCGExVectorHashComparisonDetails HashComparisonDetails;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType CompareAgainst_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector Direction_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bInvertDirection_DEPRECATED = false;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector DirectionConstant_DEPRECATED = FVector::UpVector;

#pragma endregion

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
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
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
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
