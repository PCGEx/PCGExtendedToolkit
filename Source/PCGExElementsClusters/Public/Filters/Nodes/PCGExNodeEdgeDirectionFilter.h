// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExNodeAdjacencyFilter.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExSettingsMacros.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Filters/PCGExAdjacency.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExNodeEdgeDirectionFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNodeEdgeDirectionFilterConfig
{
	GENERATED_BODY()

	FPCGExNodeEdgeDirectionFilterConfig() = default;

	/** Type of check. Fast comparison ignores adjacency consolidation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExDirectionCheckMode ComparisonQuality = EPCGExDirectionCheckMode::Dot;

	/** Adjacency Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExAdjacencySettings Adjacency;

	/** Direction orientation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAdjacencyDirectionOrigin DirectionOrder = EPCGExAdjacencyDirectionOrigin::FromNode;

	/** Direction to compare adjacent edges against -- Read as an FVector direction. */
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
class UPCGExNodeEdgeDirectionFilterFactory : public UPCGExNodeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNodeEdgeDirectionFilterConfig Config;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

class FNodeEdgeDirectionFilter final : public PCGExClusterFilter::IVtxFilter
{
public:
	explicit FNodeEdgeDirectionFilter(const UPCGExNodeEdgeDirectionFilterFactory* InFactory)
		: IVtxFilter(InFactory)
		  , TypedFilterFactory(InFactory)
	{
		Adjacency = InFactory->Config.Adjacency;
		DotComparison = InFactory->Config.DotComparisonDetails;
		HashComparison = InFactory->Config.HashComparisonDetails;
	}

	const UPCGExNodeEdgeDirectionFilterFactory* TypedFilterFactory;

	bool bFromNode = true;
	bool bUseDot = true;

	FPCGExAdjacencySettings Adjacency;
	FPCGExDotComparisonDetails DotComparison;
	FPCGExVectorHashComparisonDetails HashComparison;

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> OperandDirection;
	TConstPCGValueRange<FTransform> VtxTransforms;

	virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual bool Test(const PCGExClusters::FNode& Node) const override;

	bool TestDot(const PCGExClusters::FNode& Node) const;
	bool TestHash(const PCGExClusters::FNode& Node) const;

	virtual ~FNodeEdgeDirectionFilter() override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/vtx-filters/vtx-filter-edge-direction"))
class UPCGExNodeEdgeDirectionFilterProviderSettings : public UPCGExVtxFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NodeEdgeDirectionFilterFactory, "Vtx Filter : Edge Direction", "Dot product comparison of connected edges against a direction attribute stored on the vtx.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterCluster);
	}
#endif

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNodeEdgeDirectionFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
