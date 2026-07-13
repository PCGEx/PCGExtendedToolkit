// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#define DBL_INTERSECTION_TOLERANCE 1
#define DBL_COLLOCATION_TOLERANCE 1
#define DBL_COMPARE_TOLERANCE 0.01

#include "CoreMinimal.h"
#include "PCGExCommon.generated.h"

struct FPCGTaggedData;
class UPCGData;
class IPCGAttributeAccessorKeys;

using PCGExValueHash = uint32;
using InlineSparseAllocator = TSetAllocator<TSparseArrayAllocator<TInlineAllocator<8>, TInlineAllocator<8>>, TInlineAllocator<8>>;

UENUM()
enum class EPCGExOptionState : uint8
{
	Default  = 0 UMETA(DisplayName = "Default", Tooltip="Uses the default value selected in settings", ActionIcon="Default"),
	Enabled  = 1 UMETA(DisplayName = "Enabled", Tooltip="Option is enabled, if supported.", ActionIcon="Enabled"),
	Disabled = 2 UMETA(DisplayName = "Disabled", Tooltip="Option is disabled, if supported.", ActionIcon="Disabled")
};

UENUM(BlueprintType)
enum class EPCGExPointBoundsSource : uint8
{
	ScaledBounds  = 0 UMETA(DisplayName = "Scaled Bounds", ToolTip="Scaled Bounds", ActionIcon="ScaledBounds"),
	DensityBounds = 1 UMETA(DisplayName = "Density Bounds", ToolTip="Density Bounds (scaled + steepness)", ActionIcon="DensityBounds"),
	Bounds        = 2 UMETA(DisplayName = "Bounds", ToolTip="Unscaled Bounds (why?)", ActionIcon="Bounds"),
	Center        = 3 UMETA(DisplayName = "Center", ToolTip="A tiny size 1 box.", ActionIcon="Center")
};

UENUM(BlueprintType)
enum class EPCGExDistance : uint8
{
	Center       = 0 UMETA(DisplayName = "Center", ToolTip="Center", ActionIcon="Dist_Center"),
	SphereBounds = 1 UMETA(DisplayName = "Sphere Bounds", ToolTip="Point sphere which radius is scaled extent", ActionIcon="Dist_SphereBounds"),
	BoxBounds    = 2 UMETA(DisplayName = "Box Bounds", ToolTip="Point extents", ActionIcon="Dist_BoxBounds"),
	None         = 3 UMETA(Hidden, DisplayName = "None", ToolTip="Used for union blending with full weight."),
};

UENUM(BlueprintType)
enum class EPCGExDistanceType : uint8
{
	Euclidian = 0 UMETA(DisplayName = "Euclidian", ToolTip="Euclidian distance"),
	Manhattan = 1 UMETA(DisplayName = "Manhattan", ToolTip="Manhattan distance"),
	Chebyshev = 2 UMETA(DisplayName = "Chebyshev", ToolTip="Chebyshev distance"),
};

UENUM(BlueprintType)
enum class EPCGExTransformMode : uint8
{
	Absolute = 0 UMETA(DisplayName = "Absolute", ToolTip="Absolute, ignores source transform."),
	Relative = 1 UMETA(DisplayName = "Relative", ToolTip="Relative to source transform."),
};

UENUM(BlueprintType)
enum class EPCGExResolutionMode : uint8
{
	Distance = 0 UMETA(DisplayName = "Distance", ToolTip="Points-per-meter"),
	Fixed    = 1 UMETA(DisplayName = "Count", ToolTip="Fixed number of points"),
};

namespace PCGExCommon
{
	using ContextState = FName;

#define PCGEX_GET_DATAIDTAG(_TAG, _ID) _TAG->GetTypedValue<int64>(_ID)

	const FString PCGExPrefix = TEXT("PCGEx/");

#define PCGEX_CTX_STATE(_NAME) const PCGExCommon::ContextState _NAME = FName(#_NAME);

	namespace States
	{
		PCGEX_CTX_STATE(State_Preparation)
		PCGEX_CTX_STATE(State_LoadingAssetDependencies)
		PCGEX_CTX_STATE(State_AsyncPreparation)
		PCGEX_CTX_STATE(State_FacadePreloading)

		PCGEX_CTX_STATE(State_InitialExecution)
		PCGEX_CTX_STATE(State_ReadyForNextPoints)
		PCGEX_CTX_STATE(State_ProcessingPoints)

		PCGEX_CTX_STATE(State_WaitingOnAsyncWork)
		PCGEX_CTX_STATE(State_Done)

		PCGEX_CTX_STATE(State_Processing)
		PCGEX_CTX_STATE(State_Completing)
		PCGEX_CTX_STATE(State_Writing)

		PCGEX_CTX_STATE(State_UnionWriting)
	}

	namespace Labels
	{
		const FName SourceSeedsLabel = TEXT("Seeds");
		const FName SourceTargetsLabel = TEXT("Targets");
		const FName SourceSourcesLabel = TEXT("Sources");
		const FName SourceBoundsLabel = TEXT("Bounds");
		const FName SourceDeformersLabel = TEXT("Deformers");
		const FName OutputDiscardedLabel = TEXT("Discarded");
	}
}
