// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/PCGExPointFilter.h"
#include "PCGExVersion.h"
#include "Data/PCGExTaggedData.h"
#include "PCGExFilterCommon.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"
#include "Math/PCGExWinding.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
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
	virtual bool SupportsProxyEvaluation() const override { return true; } // TODO Change this one we support per-point tolerance from attribute

	TSharedPtr<TArray<FPCGExTaggedData>> Datas;
	TArray<TSharedPtr<PCGExPaths::FPolyPath>> PolyPaths;
	TSharedPtr<PCGExOctree::FItemOctree> Octree;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual bool WantsPreparation(FPCGExContext* InContext) override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	TSharedPtr<PCGExPathInclusion::FHandler> CreateHandler() const;

	/** Static matching: builds an ignore list using the first point only (data-vs-data).
	 *  Use for collection-level proxy evaluation (bCheckAgainstDataBounds) or when no per-point matching is needed.
	 *  For per-point evaluation, filters should create their own FDataMatcher and call Test(FConstPoint, ...) instead.
	 *  Returns true if matching passed (or no matcher). Returns false if no targets matched. */
	bool PopulateMatchIgnoreList(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<const UPCGData*>& OutIgnoreList) const;

	/** Whether match rule factories were provided (matching pin connected). */
	bool HasMatchRuleFactories() const { return !MatchRuleFactories.IsEmpty(); }
	/** Access match rule factories for creating per-point FDataMatcher instances in filter Init(). */
	const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& GetMatchRuleFactories() const { return MatchRuleFactories; }

	virtual void BeginDestroy() override;

protected:
	virtual FName GetInputLabel() const { return PCGExPaths::Labels::SourcePathsLabel; }

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
		default: case EPCGExSplineCheckType::IsInside: return TEXT("Is Inside");
		case EPCGExSplineCheckType::IsInsideOrOn: return TEXT("Is Inside or On");
		case EPCGExSplineCheckType::IsInsideAndOn: return TEXT("Is Outside and On");
		case EPCGExSplineCheckType::IsOutside: return TEXT("Is Outside");
		case EPCGExSplineCheckType::IsOutsideOrOn: return TEXT("Is Outside or On");
		case EPCGExSplineCheckType::IsOutsideAndOn: return TEXT("Is Outside and On");
		case EPCGExSplineCheckType::IsOn: return TEXT("Is On");
		case EPCGExSplineCheckType::IsNotOn: return TEXT("Is not On");
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

		EFlags GoodFlags = None;
		EFlags BadFlags = None;
		ESplineMatch FlagScope = Any;

	public:
		double Tolerance = MAX_dbl;
		double ToleranceSquared = MAX_dbl;
		bool bScaleTolerance = false;
		FVector ToleranceScaleFactor = FVector(1, 1, 1);
		TSet<const UPCGData*> MatchIgnoreList;

		explicit FHandler(const UPCGExPolyPathFilterFactory* InFactory);

		void Init(const EPCGExSplineCheckType InCheckType);

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

		EFlags GetInclusionFlags(const FVector& WorldPosition, int32& InclusionCount, const bool bClosestOnly, const UPCGData* InParentData = nullptr, const TSet<const UPCGData*>* InAdditionalExclude = nullptr) const;
		PCGExMath::FClosestPosition FindClosestIntersection(const PCGExMath::FSegment& Segment, const FPCGExPathIntersectionDetails& InDetails, const UPCGData* InParentData = nullptr, const TSet<const UPCGData*>* InAdditionalExclude = nullptr) const;
	};
}
