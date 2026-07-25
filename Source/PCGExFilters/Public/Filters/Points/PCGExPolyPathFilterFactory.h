// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExFilterCommon.h"
#include "PCGExVersion.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExTaggedData.h"
#include "Math/PCGExWinding.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "UObject/Object.h"
#include "PCGExPolyPathFilterFactory.generated.h"

namespace PCGExPaths
{
	class FPolyPath;
}

namespace PCGExPathInclusion
{
	class FHandler;
}

UENUM()
enum class EPCGExSplineSamplingIncludeMode : uint8
{
	All            = 0 UMETA(DisplayName = "All", ToolTip="Sample all inputs"),
	ClosedLoopOnly = 1 UMETA(DisplayName = "Closed loops only", ToolTip="Sample only closed loops"),
	OpenSplineOnly = 2 UMETA(DisplayName = "Open lines only", ToolTip="Sample only open lines"),
};

UENUM()
enum class EPCGExSplineCheckType : uint8
{
	IsInside       = 0 UMETA(DisplayName = "Is Inside", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside"),
	IsInsideOrOn   = 1 UMETA(DisplayName = "Is Inside or On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside or On"),
	IsInsideAndOn  = 2 UMETA(DisplayName = "Is Inside and On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside and On"),
	IsOutside      = 3 UMETA(DisplayName = "Is Outside", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Outside"),
	IsOutsideOrOn  = 4 UMETA(DisplayName = "Is Outside or On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Outside or On"),
	IsOutsideAndOn = 5 UMETA(DisplayName = "Is Outside and On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Outside and On"),
	IsOn           = 6 UMETA(DisplayName = "Is On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Is On"),
	IsNotOn        = 7 UMETA(DisplayName = "Is not On", Tooltip="...", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Not On"),
};

UENUM()
enum class EPCGExSplineFilterPick : uint8
{
	Closest = 0 UMETA(DisplayName = "Closest", Tooltip="..."),
	All     = 1 UMETA(DisplayName = "All", Tooltip="...")
};

/**
 * 
 */
UCLASS(Abstract, MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExPolyPathFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()
	friend PCGExPathInclusion::FHandler;

public:
	virtual bool SupportsProxyEvaluation() const override
	{
		return true;
	} // TODO Change this one we support per-point tolerance from attribute

	TSharedPtr<TArray<FPCGExTaggedData>> Datas;
	TArray<TSharedPtr<PCGExPaths::FPolyPath>> PolyPaths;
	TSharedPtr<PCGExOctree::FItemOctree> Octree;

	/** Strong owners for the synthesized per-path FTags, parallel to Datas. FPCGExTaggedData::Tags is a
	 *  TWeakPtr, so without retaining the FTags here, Datas' tag references would dangle and tag-based data
	 *  matching silently fails (every candidate's GetTags() returns null). The handler/filter keeps this
	 *  factory alive for as long as matching runs (it also holds a raw pointer into PolyPaths). */
	TArray<TSharedPtr<PCGExData::FTags>> OwnedTags;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual bool WantsPreparation(FPCGExContext* InContext) override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	TSharedPtr<PCGExPathInclusion::FHandler> CreateHandler() const;

	/** Collection-level matching: builds the ignore list once (data-vs-data, first element only); the list is always
	 *  built. Returns true if matching passed (or no matcher); returns false if no targets matched.
	 *  Sets bOutWantsPoints=true if a rule reads a per-point attribute on the tested data -- per-point callers should
	 *  reject it (see PCGExPointFilter::RejectPerPointMatchRule); collection/proxy callers can still use the list. */
	bool PopulateMatchIgnoreList(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<const UPCGData*>& OutIgnoreList, bool& bOutWantsPoints) const;

	/** Whether match rule factories were provided (matching pin connected). */
	bool HasMatchRuleFactories() const
	{
		return !MatchRuleFactories.IsEmpty();
	}

	/** Access match rule factories for creating per-point FDataMatcher instances in filter Init(). */
	const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& GetMatchRuleFactories() const
	{
		return MatchRuleFactories;
	}

	virtual void BeginDestroy() override;

protected:
	virtual FName GetInputLabel() const
	{
		return PCGExPaths::Labels::SourcePathsLabel;
	}

	virtual void InitConfig_Internal()
	{
	}

	double LocalFidelity = 50;
	double LocalExpansion = 0;
	double LocalExpansionZ = -1;
	double InclusionOffset = 0;
	FPCGExGeo2DProjectionDetails LocalProjection;
	EPCGExSplineSamplingIncludeMode LocalSampleInputs = EPCGExSplineSamplingIncludeMode::All;
	EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged;
	FPCGExFilterMatchingDetails DataMatching;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> MatchRuleFactories;

	bool bScaleTolerance = false;
	bool bUsedForInclusion = true;
	bool bIgnoreSelf = true;
	bool bBuildEdgeOctree = false;

	TArray<FPCGTaggedData> TempTargets;
	TArray<TSharedPtr<PCGExPaths::FPolyPath>> TempPolyPaths;
	TArray<FPCGExTaggedData> TempTaggedData;
	TArray<TSharedPtr<PCGExData::FTags>> TempTags;
};

namespace PCGExPathInclusion
{
	enum EFlags : uint8
	{
		None    = 0,
		Inside  = 1 << 0,
		Outside = 1 << 1,
		On      = 1 << 2,
	};

	enum ESplineMatch : uint8
	{
		Any = 0,
		All,
		Skip
	};

#if PCGEX_ENGINE_VERSION > 506
	PCGEXFILTERS_API FPCGDataTypeIdentifier GetInclusionIdentifier();
#endif

	PCGEXFILTERS_API void DeclareInclusionPin(TArray<FPCGPinProperties>& PinProperties);

#if WITH_EDITOR
	static FString ToString(const EPCGExSplineCheckType Check)
	{
		switch (Check)
		{
		default: case EPCGExSplineCheckType::IsInside:
			return TEXT("Is Inside");
		case EPCGExSplineCheckType::IsInsideOrOn:
			return TEXT("Is Inside or On");
		case EPCGExSplineCheckType::IsInsideAndOn:
			return TEXT("Is Inside and On");
		case EPCGExSplineCheckType::IsOutside:
			return TEXT("Is Outside");
		case EPCGExSplineCheckType::IsOutsideOrOn:
			return TEXT("Is Outside or On");
		case EPCGExSplineCheckType::IsOutsideAndOn:
			return TEXT("Is Outside and On");
		case EPCGExSplineCheckType::IsOn:
			return TEXT("Is On");
		case EPCGExSplineCheckType::IsNotOn:
			return TEXT("Is not On");
		}
	}
#endif

	class FHandler : public TSharedFromThis<FHandler>
	{
		TSharedPtr<TArray<FPCGExTaggedData>> Datas;
		const TArray<TSharedPtr<PCGExPaths::FPolyPath>>* Paths;
		TSharedPtr<PCGExOctree::FItemOctree> Octree;
		EPCGExSplineCheckType Check = EPCGExSplineCheckType::IsInside;

		bool bFastCheck = false;
		bool bDistanceCheckOnly = false;
		bool bIgnoreSelf = true;

		// Precision mode: when not Center, the tested bounds (sphere/box) must satisfy the check, not just the
		// center. Implemented by inflating the 'On' band by the bound's in-plane reach toward the nearest boundary
		// (see GetInclusionFlags). Set in Init; forces the distance path (disables bFastCheck for inside/outside).
		EPCGExDistance Precision = EPCGExDistance::Center;

		EFlags GoodFlags = None;
		EFlags BadFlags = None;
		ESplineMatch FlagScope = Any;

	public:
		double Tolerance = TNumericLimits<double>::Max();
		double ToleranceSquared = TNumericLimits<double>::Max();
		bool bScaleTolerance = false;
		FVector ToleranceScaleFactor = FVector(1, 1, 1);
		TSet<const UPCGData*> MatchIgnoreList;

		explicit FHandler(const UPCGExPolyPathFilterFactory* InFactory);

		// False when the source factory produced no polypaths (null octree). Filters must fail init rather than
		// let a Test path reach the octree, which is dereferenced unguarded.
		bool IsUsable() const { return Octree.IsValid(); }

		// InPrecision is intentionally non-defaulted: every caller must state its bounds mode explicitly so the
		// shared handler (also used by Time/SegmentCross filters) can never silently inherit a precision setting.
		void Init(const EPCGExSplineCheckType InCheckType, const EPCGExDistance InPrecision);

		FORCEINLINE bool TestFlags(const EFlags InFlags) const
		{
			bool bPass = (InFlags & BadFlags) == 0; // None of the bad flags
			if (bPass && FlagScope != Skip)
			{
				bPass = FlagScope == Any
					? EnumHasAnyFlags(InFlags, GoodFlags)
					:                                    // Any of the good flags
					EnumHasAllFlags(InFlags, GoodFlags); // All of the good flags
			}
			return bPass;
		}

		// Tested bounds are passed as the point's transform + local bounds (read from value ranges by the caller),
		// avoiding any FPoint virtual dispatch on the per-point hot path. Center mode only reads the location.
		EFlags GetInclusionFlags(const FTransform& InTransform, const FVector& InBoundsMin, const FVector& InBoundsMax, int32& InclusionCount, const bool bClosestOnly, const UPCGData* InParentData = nullptr, const TSet<const UPCGData*>* InAdditionalExclude = nullptr) const;
		PCGExMath::FClosestPosition FindClosestIntersection(const PCGExMath::FSegment& Segment, const FPCGExPathIntersectionDetails& InDetails, const UPCGData* InParentData = nullptr, const TSet<const UPCGData*>* InAdditionalExclude = nullptr) const;
	};
}
