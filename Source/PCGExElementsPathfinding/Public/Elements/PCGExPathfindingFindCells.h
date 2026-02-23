// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Containers/PCGExScopedContainers.h"

#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExCellSeedOwnership.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExPathfindingFindCells.generated.h"

namespace PCGExClusters
{
	class FProjectedPointSet;
	class FCellConstraints;
	class FCellPathBuilder;
	class FCell;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

namespace PCGExFindContours
{
	class FProcessor;
	const FName OutputGoodSeedsLabel = TEXT("SeedGenSuccess");
	const FName OutputBadSeedsLabel = TEXT("SeedGenFailed");
}

UENUM()
enum class EPCGExContourShapeTypeOutput : uint8
{
	Both        = 0 UMETA(DisplayName = "Convex & Concave", ToolTip="Output both convex and concave paths"),
	ConvexOnly  = 1 UMETA(DisplayName = "Convex Only", ToolTip="Output only convex paths"),
	ConcaveOnly = 2 UMETA(DisplayName = "Concave Only", ToolTip="Output only concave paths")
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cells/find-cells"))
class UPCGExFindContoursSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FindContours, "Pathfinding : Find Cells", "Attempts to find a closed cell of connected edges around seed points.");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Pathfinding); }
#endif

protected:
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	/** Drive how a seed selects a node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails SeedPicking;

	/** How to determine seed ownership when multiple seeds compete for a cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCellSeedOwnership SeedOwnership = EPCGExCellSeedOwnership::SeedOrder;

	/** Sort direction when using Best Candidate ownership. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Sort Direction", EditCondition="SeedOwnership==EPCGExCellSeedOwnership::BestCandidate", EditConditionHides))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints = FPCGExCellConstraintsDetails(true);

	/** Cell output settings (output mode, attributes, OBB settings) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellArtifactsDetails Artifacts;

	/** Seed growth settings. Expands seed selection to adjacent cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable))
	FPCGExCellGrowthDetails SeedGrowth;

	/** If true, write expansion metadata to output cells */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteExpansionAttributes = false;

	/** Attribute name for pick count (how many times a cell was selected by seeds/growth) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable, EditCondition="bWriteExpansionAttributes"))
	FName PickCountAttributeName = FName("PCGEx/PickCount");

	/** Attribute name for depth (minimum depth at which cell was picked, 0 = direct seed) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable, EditCondition="bWriteExpansionAttributes"))
	FName DepthAttributeName = FName("PCGEx/Depth");

	/** Output a filtered set of points containing only seeds that generated a valid path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOutputFilteredSeeds = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOutputFilteredSeeds"))
	FPCGExCellSeedMutationDetails SeedMutations = FPCGExCellSeedMutationDetails(true);

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails(true);

	/** Copy seed point attributes as tags on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding")
	FPCGExAttributeToTagDetails SeedAttributesToPathTags;

	/** Which Seed attributes to forward on paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding")
	FPCGExForwardDetails SeedForwarding;

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExFindContoursElement;
};

struct FPCGExFindContoursContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFindContoursElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExCellArtifactsDetails Artifacts;
	FPCGExCellGrowthDetails SeedGrowth;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
	TSharedPtr<PCGExCells::FSeedOwnershipHandler> SeedOwnership;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;
	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBounds;
	TSharedPtr<PCGExData::FPointIO> GoodSeeds;
	TSharedPtr<PCGExData::FPointIO> BadSeeds;

	TArray<int8> SeedQuality;

	FPCGExAttributeToTagDetails SeedAttributesToPathTags;
	TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFindContoursElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindContours)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindContours
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindContoursContext, UPCGExFindContoursSettings>
	{
	protected:
		TSharedPtr<PCGExClusters::FProjectedPointSet> Seeds;
		TSharedPtr<PCGExClusters::FCellPathBuilder> CellProcessor;
		TArray<TSharedPtr<PCGExClusters::FCell>> EnumeratedCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> AllCellsIncludingFailed; // For checking seed consumption
		TSharedPtr<PCGExClusters::FCell> WrapperCell;

		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<PCGExClusters::FCell>>> ScopedValidCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> ValidCells;
		TArray<TSharedPtr<PCGExData::FPointIO>> CellsIOIndices;

		// Expansion tracking
		TMap<int32, PCGExClusters::FCellExpansionData> CellExpansionMap; // FaceIndex -> ExpansionData
		TMap<int32, TSharedPtr<PCGExClusters::FCell>> FaceIndexToCellMap; // FaceIndex -> Cell
		TMap<int32, TSet<int32>> CellAdjacencyMap; // Cached adjacency

	public:
		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		void HandleWrapperOnlyCase(const int32 NumSeeds);

		/** Expand from a seed's initial cell to adjacent cells up to growth depth */
		void ExpandSeedToAdjacentCells(int32 SeedIndex, int32 InitialFaceIndex, int32 MaxGrowth);

		virtual void Cleanup() override;
	};
}
