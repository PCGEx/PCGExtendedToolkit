// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExLayout.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExUVW.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExBinPacking.generated.h"

struct FPCGExSortRuleConfig;

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExData
{
	struct FMutablePoint;
}

UENUM()
enum class EPCGExBinFreeSpacePartitionMode : uint8
{
	UVWConstant       = 0 UMETA(DisplayName = "UVW (Constant)", ToolTip="A constant bound-relative position"),
	UVWAttribute      = 1 UMETA(DisplayName = "UVW", ToolTip="A per-bin bound-relative position"),
	PositionConstant  = 2 UMETA(DisplayName = "Position (Constant)", ToolTip="A constant world position"),
	PositionAttribute = 3 UMETA(DisplayName = "Position (Attribute)", ToolTip="A per-bin world position"),
};

UENUM()
enum class EPCGExPlacementFavor : uint8
{
	SeedProximity = 0 UMETA(DisplayName = "Seed Proximity", ToolTip="Favor seed proximity over space conservation"),
	Space         = 1 UMETA(DisplayName = "Space Conservation", ToolTip="Favor space conservation over seed proximity"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/modify/bin-packing"))
class UPCGExBinPackingSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;

	PCGEX_NODE_INFOS(BinPacking, "Bin Packing", "[EXPERIMENTAL] An simple bin packing node.");

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
	/** Controls the order in which points will be sorted, when using sorting rules. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	/** Per-bin seed. Represent a bound-relative location to start packing from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExBinSeedMode SeedMode = EPCGExBinSeedMode::UVWConstant;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::UVWConstant", EditConditionHides))
	FVector SeedUVW = FVector(0, 0, -1);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::UVWAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector SeedUVWAttribute;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::PositionConstant", EditConditionHides))
	FVector SeedPosition = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="SeedMode == EPCGExBinSeedMode::PositionAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector SeedPositionAttribute;

	/** Will attempt to infer the split axis from relative seed positioning, and fall back to selected axis if it can't find one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	bool bInferSplitAxisFromSeed = false;

	/** The main stacking axis is the axis that will generate the smallest free space for further insertion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	EPCGExAxis SplitAxis = EPCGExAxis::Up;

	/** The cross stacking axis is the axis that will generate the largest free space on the "sides" of the main axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	EPCGExSpaceSplitMode SplitMode = EPCGExSpaceSplitMode::Minimal;

	/** If enabled, fitting will try to avoid wasted space by not creating free spaces that are below a certain threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	bool bAvoidWastedSpace = true;

	/** If enabled, fitting will try to avoid wasted space by not creating free spaces that are below a certain threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	EPCGExPlacementFavor PlacementFavor = EPCGExPlacementFavor::SeedProximity;

	// TODO : Testable Rotation

	/** Per-item occupation padding. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorVector OccupationPadding = FPCGExInputShorthandSelectorVector(FName("Padding"));

	/** If enabled, the padding will not be relative (rotated) if the item is rotated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fitting", meta = (PCG_Overridable))
	bool bAbsolutePadding = true;

	/** If enabled, won't throw a warning if there are more bins than there are inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTooManyBinsWarning = false;

	/** If enabled, won't throw a warning if there are fewer bins than there are inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTooFewBinsWarning = false;

	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const;
};

struct FPCGExBinPackingContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBinPackingElement;

	TSet<int32> ValidIOIndices;

	TSharedPtr<PCGExData::FPointIOCollection> Bins;
	TArray<FPCGExUVW> BinsUVW;

	TSharedPtr<PCGExData::FPointIOCollection> Discarded;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBinPackingElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BinPacking)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBinPacking
{
	using PCGExLayout::FItem;
	using PCGExLayout::FSpace;

	class FBinSplit : public TSharedFromThis<FBinSplit>
	{
	public:
		FBinSplit() = default;
		virtual ~FBinSplit() = default;

		virtual void SplitSpace(const FSpace& Space, FBox& ItemBox, TArray<FBox>& OutPartitions) const = 0;
	};

	template <EPCGExAxis SplitAxis = EPCGExAxis::Up, EPCGExSpaceSplitMode Mode = EPCGExSpaceSplitMode::Minimal>
	class TBinSplit : public FBinSplit
	{
	public:
		virtual void SplitSpace(const FSpace& Space, FBox& ItemBox, TArray<FBox>& OutPartitions) const override
		{
			PCGExLayout::SplitSpace<SplitAxis, Mode>(Space, ItemBox, OutPartitions);
		}
	};


	class FBin : public TSharedFromThis<FBin>
	{
	protected:
		double MaxVolume = 0;
		double MaxDist = 0;
		FVector Seed = FVector::ZeroVector;
		TSharedPtr<FBinSplit> Splitter;

		TArray<FSpace> Spaces;
		void AddSpace(const FBox& InBox);

	public:
		int32 MaxItems = 0;
		FBox Bounds;
		FTransform Transform;

		const UPCGExBinPackingSettings* Settings = nullptr;
		FVector WastedSpaceThresholds = FVector::ZeroVector;
		TArray<FItem> Items;

		FBin(const PCGExData::FConstPoint& InBinPoint, const FVector& InSeed, const TSharedPtr<FBinSplit>& InSplitter);
		~FBin() = default;

		bool IsFull() const
		{
			return Items.Num() <= MaxItems;
		}

		int32 GetBestSpaceScore(const FItem& InItem, double& OutScore, FRotator& OutRotator) const;
		void AddItem(int32 SpaceIndex, FItem& InItem);
		bool Insert(FItem& InItem);
		void UpdatePoint(PCGExData::FMutablePoint& InPoint, const FItem& InItem) const;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBinPackingContext, UPCGExBinPackingSettings>
	{
	protected:
		TSharedPtr<FBinSplit> Splitter;
		double MinOccupation = 0;
		TSharedPtr<PCGExSorting::FSorter> Sorter;
		TArray<TSharedPtr<FBin>> Bins;
		TBitArray<> Fitted;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> PaddingBuffer;
		bool bHasUnfitted = false;

		TArray<int32> ProcessingOrder;

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
