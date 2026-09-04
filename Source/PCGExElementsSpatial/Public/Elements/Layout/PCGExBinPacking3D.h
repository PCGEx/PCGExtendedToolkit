// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Adapted from https://www.nature.com/articles/s41598-023-39013-9
// Sebastián V. Romero, Eneko Osaba, Esther Villar-Rodriguez, Izaskun Oregi & Yue Ban
// Hybrid approach for solving real-world bin packing problem instances using quantum annealer

#pragma once

#include "CoreMinimal.h"
#include "PCGExLayout.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExUVW.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExBinPacking3D.generated.h"

struct FPCGExSortRuleConfig;

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExData
{
	struct FMutablePoint;
}

///
/// Enums
///

UENUM()
enum class EPCGExBP3DRotationMode : uint8
{
	None          = 0 UMETA(DisplayName = "None", ToolTip="No rotation testing"),
	CardinalOnly  = 1 UMETA(DisplayName = "Cardinal (90deg)", ToolTip="Test 90 degree rotations only (4 orientations around up axis)"),
	Paper6        = 2 UMETA(DisplayName = "Paper 6 Orientations", ToolTip="6 axis permutations with symmetry reduction (Q4RealBPP). Cube=1, square prism=3, all-different=6"),
	AllOrthogonal = 3 UMETA(DisplayName = "All Orthogonal", ToolTip="Test all 24 orthogonal orientations"),
};

UENUM()
enum class EPCGExBP3DAffinityType : uint8
{
	Negative = 0 UMETA(DisplayName = "Negative (Incompatible)", ToolTip="Categories must NOT share the same bin"),
	Positive = 1 UMETA(DisplayName = "Positive (Co-locate)", ToolTip="Categories SHOULD share the same bin"),
};

///
/// Affinity Rule Struct
///

USTRUCT(BlueprintType)
struct FPCGExBP3DAffinityRule
{
	GENERATED_BODY()

	FPCGExBP3DAffinityRule() = default;

	/** Type of affinity constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBP3DAffinityType Type = EPCGExBP3DAffinityType::Negative;

	/** First category value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 CategoryA = 0;

	/** Second category value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 CategoryB = 1;
};

///
/// Settings
///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/modify/bin-packing-3d-q4realbpp"))
class UPCGExBinPacking3DSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BinPacking3D, "Bin Packing 3D (Q4RealBPP)", "3D bin packing with real-world constraints from the Q4RealBPP paper: weight limits, category affinities, load bearing, and multi-objective scoring.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Transform);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	//
	// Settings
	//

	/** Controls the order in which points will be sorted, when using sorting rules. Uses largest-first by default for best-fit decreasing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Descending;

	/** If enabled, items will be sorted by volume (largest first) before packing. This is the classic BFD approach. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bSortByVolume = true;

	/** Per-bin seed. Represent a bound-relative location to start packing from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seeding", meta = (PCG_Overridable))
	EPCGExBinSeedMode SeedMode = EPCGExBinSeedMode::UVWConstant;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seeding", meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::UVWConstant", EditConditionHides))
	FVector SeedUVW = FVector(-1, -1, -1);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seeding", meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::UVWAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector SeedUVWAttribute;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seeding", meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::PositionConstant", EditConditionHides))
	FVector SeedPosition = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seeding", meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::PositionAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector SeedPositionAttribute;

	//
	// Settings|Fitting
	//

	/** Rotation testing mode. More rotations = better fit but slower. Paper6 uses the paper's symmetry-reduced orientations. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable))
	EPCGExBP3DRotationMode RotationMode = EPCGExBP3DRotationMode::Paper6;

	/** If enabled, will evaluate all bins for each item to find the globally best placement. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable))
	bool bGlobalBestFit = true;

	/** Per-item occupation padding. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorVector OccupationPadding = FPCGExInputShorthandSelectorVector(FName("Padding"));

	/** If enabled, the padding will not be relative (rotated) if the item is rotated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable))
	bool bAbsolutePadding = true;


	//
	// Settings|Load Bearing
	//

	/** Enable load-bearing constraint (Paper Eq. 26). Items on top must be lighter than items below. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bEnableLoadBearing = false;

	/** Per-item mass ratio threshold (eta). new_weight must be <= eta * supporting_weight. 1.0 = equal weight, 0.5 = top must be half the weight. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable, EditCondition="bEnableLoadBearing"))
	FPCGExInputShorthandSelectorDoubleAbs LoadBearingThreshold = FPCGExInputShorthandSelectorDoubleAbs(FName("LoadBearingThreshold"), 1.0);

	//
	// Settings|Support
	//

	/** Require items above the floor to have physical support beneath them. Prevents visually floating items. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bRequireSupport = true;

	/** Per-item minimum fraction of base area that must be supported (0 = any contact, 1 = full base). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Packing", meta = (PCG_Overridable, EditCondition="bRequireSupport"))
	FPCGExInputShorthandSelectorDouble01 MinSupportRatio = FPCGExInputShorthandSelectorDouble01(FName("MinSupportRatio"), 0.2);

	//
	// Settings|Objectives
	//

	/** Weight for bin-usage objective (o1, Paper Eq. 1). Higher fill-ratio bins are preferred. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Objectives", meta = (PCG_Overridable, ClampMin=0.0, ClampMax=1.0))
	double ObjectiveWeightBinUsage = 0.3;

	/** Weight for height objective (o2, Paper Eq. 2). Lower placement Z is preferred (floor-up packing). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Objectives", meta = (PCG_Overridable, ClampMin=0.0, ClampMax=1.0))
	double ObjectiveWeightHeight = 0.3;

	/** Weight for load-balance objective (o3, Paper Eq. 3). Placement closer to bin center-of-mass is preferred. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Objectives", meta = (PCG_Overridable, ClampMin=0.0, ClampMax=1.0))
	double ObjectiveWeightLoadBalance = 0.2;

	/** Weight for surface-contact objective. Items touching more surfaces (walls/other items) are preferred for visual stability. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Objectives", meta = (PCG_Overridable, ClampMin=0.0, ClampMax=1.0))
	double ObjectiveWeightContact = 0.2;

	//
	// Settings|Weight Constraint
	//

	/** Enable maximum weight per bin constraint (Paper Eq. 23). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight Constraint", meta = (PCG_Overridable))
	bool bEnableWeightConstraint = false;

	/** Per-item weight/mass value. Shared by the weight constraint (Paper Eq. 23) and load bearing (Paper Eq. 26, mass comparison). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight Constraint", meta = (PCG_Overridable, EditCondition="bEnableWeightConstraint || bEnableLoadBearing", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs ItemWeight = FPCGExInputShorthandSelectorDoubleAbs(FName("Weight"), 1.0);

	/** Per-bin maximum weight (read from bin points). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight Constraint", meta = (PCG_Overridable, EditCondition="bEnableWeightConstraint", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs BinMaxWeight = FPCGExInputShorthandSelectorDoubleAbs(FName("MaxWeight"), 100.0);

	//
	// Settings|Category Affinities
	//

	/** Enable category-based affinity constraints (Paper Eq. 24-25). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Category Affinities", meta = (PCG_Overridable))
	bool bEnableAffinities = false;

	/** Per-item integer category. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Category Affinities", meta = (PCG_Overridable, EditCondition="bEnableAffinities", EditConditionHides))
	FPCGExInputShorthandSelectorInteger32 ItemCategory = FPCGExInputShorthandSelectorInteger32(FName("Category"), -1);

	/** List of affinity rules between category pairs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Category Affinities", meta = (PCG_Overridable, EditCondition="bEnableAffinities", EditConditionHides, TitleProperty="Type: {CategoryA} <-> {CategoryB}"))
	TArray<FPCGExBP3DAffinityRule> AffinityRules;

	//
	// Warnings and Errors
	//

	/** If enabled, won't throw a warning if there are more bins than there are inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTooManyBinsWarning = false;

	/** If enabled, won't throw a warning if there are fewer bins than there are inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTooFewBinsWarning = false;

	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const;
};

///
/// Context
///

struct FPCGExBinPacking3DContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBinPacking3DElement;

	TSet<int32> ValidIOIndices;

	TSharedPtr<PCGExData::FPointIOCollection> Bins;
	TArray<FPCGExUVW> BinsUVW;

	TSharedPtr<PCGExData::FPointIOCollection> Discarded;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

///
/// Element
///

class FPCGExBinPacking3DElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BinPacking3D)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

///
/// Namespace
///

namespace PCGExBinPacking3D
{
	// Extended item with weight and category
	struct FBP3DItem
	{
		int32 Index = -1;
		FBox Box = FBox(ForceInit);
		FBox PaddedBox = FBox(ForceInit);
		FVector Padding = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector OriginalSize = FVector::ZeroVector;
		double Weight = 0.0;
		int32 Category = -1;
		double LoadBearingThreshold = 1.0;
		double MinSupportRatio = 0.0;

		FBP3DItem() = default;
	};

	// Placement candidate with paper objective scores
	struct FBP3DPlacementCandidate
	{
		int32 BinIndex = -1;
		int32 EPIndex = -1;
		int32 RotationIndex = 0;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector RotatedSize = FVector::ZeroVector;
		FVector PlacementMin = FVector::ZeroVector;
		FVector EffectivePadding = FVector::ZeroVector;
		double Score = TNumericLimits<double>::Max();

		// Paper objective scores (all [0,1], lower = better)
		double BinUsageScore = 0.0;
		double HeightScore = 0.0;
		double LoadBalanceScore = 0.0;
		double ContactScore = 0.0;

		bool IsValid() const
		{
			return BinIndex >= 0 && EPIndex >= 0;
		}
	};

	// Paper's 6 orientations with symmetry reduction
	class PCGEXELEMENTSSPATIAL_API FBP3DRotationHelper
	{
	public:
		static void GetPaper6Rotations(const FVector& ItemSize, TArray<FRotator>& OutRotations);
		static void GetRotationsToTest(EPCGExBP3DRotationMode Mode, TArray<FRotator>& OutRotations);
		static FVector RotateSize(const FVector& Size, const FRotator& Rotation);
	};

	// Bin using Extreme Point placement (replaces guillotine-cut free-space approach)
	class PCGEXELEMENTSSPATIAL_API FBP3DBin : public TSharedFromThis<FBP3DBin>
	{
	protected:
		double MaxVolume = 0;
		double UsedVolume = 0;
		FVector Seed = FVector::ZeroVector;
		FVector PackSign = FVector::OneVector;

		TArray<FVector> ExtremePoints;

		void AddExtremePoint(const FVector& Point);
		void GenerateExtremePoints(const FBox& PaddedItemBox);
		void RemoveInvalidExtremePoints(const FBox& PaddedItemBox);
		FVector ProjectPoint(const FVector& RawPoint) const;
		bool IsInsideAnyItem(const FVector& Point) const;

	public:
		int32 BinIndex = -1;
		FBox Bounds;
		FTransform Transform;

		bool bAbsolutePadding = true;
		TArray<FBP3DItem> Items;

		// Weight constraint
		double CurrentWeight = 0.0;
		double MaxWeight = 0.0;

		// Affinity: set of categories present in this bin
		TSet<int32> PresentCategories;

		FBP3DBin(int32 InBinIndex, const PCGExData::FConstPoint& InBinPoint, const FVector& InSeed);
		~FBP3DBin() = default;

		double GetFillRatio() const
		{
			return MaxVolume > 0 ? UsedVolume / MaxVolume : 0;
		}

		int32 GetEPCount() const
		{
			return ExtremePoints.Num();
		}

		FVector GetBinCenter() const
		{
			return Bounds.GetCenter();
		}

		bool HasOverlap(const FBox& TestBox) const;
		double ComputeContactScore(const FBox& TestBox) const;

		bool EvaluatePlacement(
			const FVector& ItemSize,
			const FVector& ItemPadding,
			int32 EPIndex,
			const FRotator& Rotation,
			FBP3DPlacementCandidate& OutCandidate) const;

		bool CheckLoadBearing(const FBP3DPlacementCandidate& Candidate, double ItemWeight, double Threshold) const;
		double ComputeSupportRatio(const FBox& ItemBox) const;

		void CommitPlacement(const FBP3DPlacementCandidate& Candidate, FBP3DItem& InItem);
		void UpdatePoint(PCGExData::FMutablePoint& InPoint, const FBP3DItem& InItem) const;
	};

	// Main processor
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBinPacking3DContext, UPCGExBinPacking3DSettings>
	{
	protected:
		TSharedPtr<PCGExSorting::FSorter> Sorter;
		TArray<TSharedPtr<FBP3DBin>> Bins;
		TBitArray<> Fitted;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> PaddingBuffer;
		TSharedPtr<PCGExDetails::TSettingValue<double>> ItemWeightBuffer;
		TSharedPtr<PCGExDetails::TSettingValue<int32>> CategoryBuffer;
		TSharedPtr<PCGExDetails::TSettingValue<double>> LoadBearingThresholdBuffer;
		TSharedPtr<PCGExDetails::TSettingValue<double>> MinSupportRatioBuffer;
		bool bHasUnfitted = false;

		TArray<int32> ProcessingOrder;

		// Affinity lookup structures
		TSet<uint64> NegativeAffinityPairs;
		TMap<int32, int32> PositiveAffinityGroup;

		// Per-bin max weight
		TArray<double> BinMaxWeights;

		FBP3DPlacementCandidate FindBestPlacement(const FBP3DItem& InItem);
		double ComputeFinalScore(const FBP3DPlacementCandidate& Candidate) const;

		// Affinity helpers
		void BuildAffinityLookups();
		static uint64 MakeAffinityKey(int32 A, int32 B);
		bool HasNegativeAffinity(int32 CatA, int32 CatB) const;
		int32 FindPositiveGroup(int32 Category) const;

		bool IsCategoryCompatibleWithBin(int32 ItemCategory, const FBP3DBin& Bin) const;
		int32 FindRequiredBinForPositiveAffinity(int32 ItemCategory) const;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
			bForceSingleThreadedProcessPoints = true;
		}

		virtual ~FProcessor() override
		{
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};
}
