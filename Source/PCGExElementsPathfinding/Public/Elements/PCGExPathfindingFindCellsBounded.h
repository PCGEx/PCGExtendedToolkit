// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExCellTriage.h"
#include "Containers/PCGExScopedContainers.h"

#include "Clusters/Artifacts/PCGExCell.h"
#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExCellSeedOwnership.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExPathfindingFindCellsBounded.generated.h"

namespace PCGExClusters
{
	class FCellConstraints;
	class FCellPathBuilder;
	class FCell;
}

namespace PCGExCells
{
	class FSeededCellResolver;
}

namespace PCGExFindContoursBounded
{
	class FProcessor;

	const FName OutputGoodSeedsLabel = TEXT("SeedGenSuccess");
	const FName OutputBadSeedsLabel = TEXT("SeedGenFailed");
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cells/find-cells-bounded"))
class UPCGExFindContoursBoundedSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FindContoursBounded, "Pathfinding : Find Cells (Bounded)", "Finds closed cells around seed points and triages them by spatial bounds relationship (Inside/Touching/Outside).");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Pathfinding);
	}
#endif

protected:
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return true;
	}

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

	/** How to output triaged cells */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExCellTriageOutput OutputMode = EPCGExCellTriageOutput::Separate;

	/** Which cell categories to output (Inside/Touching/Outside) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, Bitmask, BitmaskEnum = "/Script/PCGExGraphs.EPCGExCellTriageFlags"))
	uint8 TriageFlags = static_cast<uint8>(PCGExCellTriage::DefaultFlags);

	FORCEINLINE bool OutputInside() const
	{
		return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Inside));
	}

	FORCEINLINE bool OutputTouching() const
	{
		return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Touching));
	}

	FORCEINLINE bool OutputOutside() const
	{
		return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Outside));
	}

	/** Drive how a seed selects a node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails SeedPicking;

	/** How to determine seed ownership when multiple seeds compete for a cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCellSeedOwnership SeedOwnership = EPCGExCellSeedOwnership::SeedOrder;

	/** Sort direction when using Best Candidate ownership. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Sort Direction", EditCondition="SeedOwnership == EPCGExCellSeedOwnership::BestCandidate", EditConditionHides))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints = FPCGExCellConstraintsDetails(true);

	/** Cell output settings (output mode, attributes, OBB settings) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellArtifactsDetails Artifacts;

	/** Seed growth settings. Expands seed selection to adjacent cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable))
	FPCGExCellGrowthDetails SeedGrowth;

	/** Merge adjacent cells whose seeds share a key value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Expansion", meta = (PCG_Overridable))
	FPCGExCellSeedMergeDetails SeedMerge;

	/** Output a filtered set of points containing only seeds that claimed a valid cell, merged cells included. */
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

private:
	friend class FPCGExFindContoursBoundedElement;
};

struct FPCGExFindContoursBoundedContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFindContoursBoundedElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExCellArtifactsDetails Artifacts;
	FPCGExCellGrowthDetails SeedGrowth;
	FPCGExCellSeedMergeDetails SeedMerge;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
	TSharedPtr<PCGExCells::FSeedOwnershipHandler> SeedOwnership;

	PCGExCellTriage::FOutputs Triage;

	TSharedPtr<PCGExData::FPointIO> GoodSeeds;
	TSharedPtr<PCGExData::FPointIO> BadSeeds;

	TArray<int8> SeedQuality;

	FPCGExAttributeToTagDetails SeedAttributesToPathTags;
	TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFindContoursBoundedElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindContoursBounded)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindContoursBounded
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindContoursBoundedContext, UPCGExFindContoursBoundedSettings>
	{
	protected:
		/** Seed-to-cell resolution; released once the claims are final. */
		TSharedPtr<PCGExCells::FSeededCellResolver> Resolver;
		TSharedPtr<PCGExClusters::FCellPathBuilder> CellProcessor;
		TSharedPtr<PCGExClusters::FCell> WrapperCell;

		PCGExCellTriage::FBuckets Buckets;

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

		virtual void Cleanup() override;
	};
}
