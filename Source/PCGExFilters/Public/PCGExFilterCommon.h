// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExMatchingDetails.h"
#include "PCGExFilterCommon.generated.h"

UENUM()
enum class EPCGExFilterFallback : uint8
{
	Pass = 0 UMETA(DisplayName = "Pass", ToolTip="This item will be considered to successfully pass the filter", ActionIcon="MissingData_Pass"),
	Fail = 1 UMETA(DisplayName = "Fail", ToolTip="This item will be considered to failing to pass the filter", ActionIcon="MissingData_Fail"),
};

UENUM()
enum class EPCGExFilterResult : uint8
{
	Pass = 0 UMETA(DisplayName = "Pass", ToolTip="Passes the filters"),
	Fail = 1 UMETA(DisplayName = "Fail", ToolTip="Fails the filters"),
};

UENUM(BlueprintType)
enum class EPCGExFilterNoDataFallback : uint8
{
	Error = 0 UMETA(DisplayName = "Throw Error", ToolTip="This filter will throw an error if there is no data.", ActionIcon="MissingData_Error"),
	Pass  = 1 UMETA(DisplayName = "Pass", ToolTip="This filter will pass if there is no data", ActionIcon="MissingData_Pass"),
	Fail  = 2 UMETA(DisplayName = "Fail", ToolTip="This filter will fail if there is no data", ActionIcon="MissingData_Fail"),
};

UENUM()
enum class EPCGExFilterGroupMode : uint8
{
	AND = 0 UMETA(DisplayName = "And", ToolTip="All connected filters must pass.", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "And Combine"),
	OR  = 1 UMETA(DisplayName = "Or", ToolTip="Only a single connected filter must pass.", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Or Combine")
};

namespace PCGExFilters
{
	enum class EType : uint8
	{
		None = 0,
		Point,
		Group,
		Node,
		Edge,
		Collection,
	};

	namespace Labels
	{
		const FName OutputFilterLabel = FName("Filter");
		const FName OutputColFilterLabel = FName("Data Filter");
		const FName OutputFilterLabelNode = FName("Vtx Filter");
		const FName OutputFilterLabelEdge = FName("Edge Filter");
		const FName SourceFiltersLabel = FName("Filters");

		const FName SourceFiltersConditionLabel = FName("Conditions Filters");
		const FName SourceKeepConditionLabel = FName("Keep Conditions");
		const FName SourceToggleConditionLabel = FName("Toggle Conditions");
		const FName SourceStartConditionLabel = FName("Start Conditions");
		const FName SourceStopConditionLabel = FName("Stop Conditions");
		const FName SourcePinConditionLabel = FName("Pin Conditions");

		const FName SourcePointFiltersLabel = FName("Point Filters");
		const FName SourceVtxFiltersLabel = FName("Vtx Filters");
		const FName SourceEdgeFiltersLabel = FName("Edge Filters");

		const FName OutputInsideFiltersLabel = FName("Inside");
		const FName OutputOutsideFiltersLabel = FName("Outside");

		const FName SourceUseValueIfFilters = TEXT("UsableValueFilters");
	}
}

/**
 * Filter-specific matching details with a configurable fallback for when no targets match.
 */
USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExFilterMatchingDetails : public FPCGExMatchingDetails
{
	GENERATED_BODY()

	FPCGExFilterMatchingDetails()
		: FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Filter)
	{
	}

	/** Behavior when no target matches the input data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode != EPCGExMapMatchMode::Disabled", EditConditionHides, DisplayAfter="Mode"))
	EPCGExFilterFallback NoMatchFallback = EPCGExFilterFallback::Fail;
};
