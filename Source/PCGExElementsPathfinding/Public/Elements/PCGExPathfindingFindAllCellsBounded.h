// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Core/PCGExClustersProcessor.h"

#include "PCGExPathfindingFindAllCellsBounded.generated.h"

namespace PCGExClusters
{
	class FProjectedPointSet;
	class FCellConstraints;
	class FCellPathBuilder;
}

namespace PCGExFindAllCellsBounded
{
	class FProcessor;

	const FName SourceBoundsLabel = FName("Bounds");

	const FName OutputPathsInsideLabel = FName("Paths : Inside");
	const FName OutputPathsTouchingLabel = FName("Paths : Touching");
	const FName OutputPathsOutsideLabel = FName("Paths : Outside");

	const FName OutputBoundsInsideLabel = FName("Bounds : Inside");
	const FName OutputBoundsTouchingLabel = FName("Bounds : Touching");
	const FName OutputBoundsOutsideLabel = FName("Bounds : Outside");
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

// Triage enums and tags are now in PCGExCellDetails.h

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cells/find-all-cells-bounded"))
class UPCGExFindAllCellsBoundedSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FindAllCellsBounded, "Pathfinding : Find All Cells (Bounded)", "Finds all cluster cells and triages them by spatial bounds relationship (Inside/Touching/Outside).");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Pathfinding); }
#endif

protected:
	virtual bool HasDynamicPins() const override { return true; }
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
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

	FORCEINLINE bool OutputInside() const { return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Inside)); }
	FORCEINLINE bool OutputTouching() const { return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Touching)); }
	FORCEINLINE bool OutputOutside() const { return !!(TriageFlags & static_cast<uint8>(EPCGExCellTriageFlags::Outside)); }

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints = FPCGExCellConstraintsDetails(true);

	/** Cell output settings (output mode, attributes, OBB settings) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellArtifactsDetails Artifacts;

	/** Hole growth settings. Expands hole exclusion to adjacent cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellGrowthDetails HoleGrowth;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails(true);

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExFindAllCellsBoundedElement;
};

struct FPCGExFindAllCellsBoundedContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFindAllCellsBoundedElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExCellArtifactsDetails Artifacts;
	FPCGExCellGrowthDetails HoleGrowth;

	TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
	TSharedPtr<PCGExData::FFacade> HolesFacade;

	FBox BoundsFilter = FBox(ForceInit);

	// Separate output collections for each triage category
	TSharedPtr<PCGExData::FPointIOCollection> OutputPathsInside;
	TSharedPtr<PCGExData::FPointIOCollection> OutputPathsTouching;
	TSharedPtr<PCGExData::FPointIOCollection> OutputPathsOutside;

	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBoundsInside;
	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBoundsTouching;
	TSharedPtr<PCGExData::FPointIOCollection> OutputCellBoundsOutside;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFindAllCellsBoundedElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindAllCellsBounded)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindAllCellsBounded
{
	// Use shared triage result enum from PCGExCellDetails.h
	using ECellTriageResult = EPCGExCellTriageResult;

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindAllCellsBoundedContext, UPCGExFindAllCellsBoundedSettings>
	{
	protected:
		TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
		TSharedPtr<PCGExClusters::FCellPathBuilder> CellProcessor;

		TArray<TSharedPtr<PCGExClusters::FCell>> CellsInside;
		TArray<TSharedPtr<PCGExClusters::FCell>> CellsTouching;
		TArray<TSharedPtr<PCGExClusters::FCell>> CellsOutside;

		TArray<TSharedPtr<PCGExData::FPointIO>> CellsIOInside;
		TArray<TSharedPtr<PCGExData::FPointIO>> CellsIOTouching;
		TArray<TSharedPtr<PCGExData::FPointIO>> CellsIOOutside;

		// For Combined mode tagging
		TArray<FString> CellTagsInside;
		TArray<FString> CellTagsTouching;
		TArray<FString> CellTagsOutside;

		// Hole expansion tracking
		TSet<int32> ExcludedFaceIndices; // Faces excluded due to hole expansion
		TMap<int32, TSet<int32>> CellAdjacencyMap; // Cached adjacency

	public:
		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;

		virtual void Cleanup() override;

		/** Expand hole exclusion to adjacent cells */
		void ExpandHoleExclusion(int32 HoleIndex, int32 InitialFaceIndex, int32 MaxGrowth);

	protected:
		ECellTriageResult ClassifyCell(const TSharedPtr<PCGExClusters::FCell>& InCell) const;
	};
}
