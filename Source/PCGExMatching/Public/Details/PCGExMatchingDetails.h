// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchingCommon.h"
#include "Data/PCGExDataCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExMatchingDetails.generated.h"

struct FPCGPinProperties;


/**
 * Used when data from different pins needs to be paired together
 * by using either tags or @Data attribute but no access to points.
 */
USTRUCT(BlueprintType)
struct PCGEXMATCHING_API FPCGExMatchingDetails
{
	GENERATED_BODY()

	virtual ~FPCGExMatchingDetails() = default;

	explicit FPCGExMatchingDetails(const EPCGExMapMatchMode InMode = EPCGExMapMatchMode::Disabled)
		: Mode(InMode)
	{
	}

	explicit FPCGExMatchingDetails(const EPCGExMatchingDetailsUsage InUsage, const EPCGExMapMatchMode InMode = EPCGExMapMatchMode::Disabled)
		: Usage(InUsage), Mode(InMode)
	{
	}

	UPROPERTY()
	EPCGExMatchingDetailsUsage Usage = EPCGExMatchingDetailsUsage::Default;

	/** Whether matching is enabled or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExMapMatchMode Mode = EPCGExMapMatchMode::Disabled;

	/** Which cluster component must match the tags */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Usage == EPCGExMatchingDetailsUsage::Cluster", EditConditionHides, HideEditConditionToggle))
	EPCGExClusterComponentTagMatchMode ClusterMatchMode = EPCGExClusterComponentTagMatchMode::Vtx;

	/** Whether to output unmatched data in a separate pin */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	bool bSplitUnmatched = true;

	/** If enabled, outputs data that got no valid matches.
	 * Not all nodes support this options. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName=" ├─ Output Unmatched", EditCondition="!bSplitUnmatched && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	bool bOutputUnmatched = true;

	/** If enabled, will throw a warning when there is no valid target matches.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Quiet Unmatched Warning", EditCondition="!bSplitUnmatched && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	bool bQuietUnmatchedTargetWarning = true;

	/** Whether to limit the number of matches or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode != EPCGExMapMatchMode::Disabled && Usage != EPCGExMatchingDetailsUsage::Sampling && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	bool bLimitMatches = false;

	/** Type of Match limit */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bLimitMatches && Mode != EPCGExMapMatchMode::Disabled && Usage != EPCGExMatchingDetailsUsage::Sampling && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	EPCGExInputValueType LimitInput = EPCGExInputValueType::Constant;

	/** Attribute to read Limit value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Limit (Attr)", EditCondition="bLimitMatches && LimitInput != EPCGExInputValueType::Constant && Mode != EPCGExMapMatchMode::Disabled && Usage != EPCGExMatchingDetailsUsage::Sampling && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	FPCGAttributePropertyInputSelector LimitAttribute;

	/** Constant Limit value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Limit", EditCondition="bLimitMatches && LimitInput == EPCGExInputValueType::Constant && Mode != EPCGExMapMatchMode::Disabled && Usage != EPCGExMatchingDetailsUsage::Sampling && Usage != EPCGExMatchingDetailsUsage::Filter", EditConditionHides))
	int32 Limit = 1;

	bool IsEnabled() const { return Mode != EPCGExMapMatchMode::Disabled; }

	bool WantsUnmatchedSplit() const { return Mode != EPCGExMapMatchMode::Disabled && bSplitUnmatched; }
};
