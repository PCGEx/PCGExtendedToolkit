// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Utils/PCGExCompare.h"

#include "PCGExEdgeLengthFilter.generated.h"

class UPCGSettings;
class UPCGNode;

USTRUCT(BlueprintType)
struct FPCGExEdgeLengthFilterConfig
{
	GENERATED_BODY()

	FPCGExEdgeLengthFilterConfig() = default;

	/** Edge length threshold value. Edges are tested against this distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Threshold"))
	FPCGExInputShorthandSelectorDoubleAbs Threshold = FPCGExInputShorthandSelectorDoubleAbs(FString(TEXT("")), 100, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ThresholdInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector ThresholdAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double ThresholdConstant_DEPRECATED = 100;

#pragma endregion

	/** Comparison check */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::StrictlyGreater;

	/** Rounding mode for approx. comparison modes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = 0;


	/** Invert the filter result (pass becomes fail and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExEdgeLengthFilterFactory : public UPCGExEdgeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExEdgeLengthFilterConfig Config;


	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExEdgeLength
{
	class FLengthFilter final : public PCGExClusterFilter::IEdgeFilter
	{
	public:
		explicit FLengthFilter(const UPCGExEdgeLengthFilterFactory* InFactory)
			: IEdgeFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const UPCGExEdgeLengthFilterFactory* TypedFilterFactory;

		TSharedPtr<PCGExDetails::TSettingValue<double>> Threshold;

		virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;

		virtual ~FLengthFilter() override;
	};
}


/** Outputs a single GraphParam to be consumed by other nodes */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/edge-filters/edge-filter-length"))
class UPCGExEdgeLengthFilterProviderSettings : public UPCGExEdgeFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(EdgeLengthFilterFactory, "Edge Filter : Length", "Check against the edge' length.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterCluster);
	}
#endif
	//~End UPCGSettings

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExEdgeLengthFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
