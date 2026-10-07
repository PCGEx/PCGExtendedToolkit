// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExCellTriage.h"
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

	PCGExCellTriage::FOutputs Triage;

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
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFindAllCellsBoundedContext, UPCGExFindAllCellsBoundedSettings>
	{
	protected:
		TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
		TSharedPtr<PCGExClusters::FCellPathBuilder> CellProcessor;

		PCGExCellTriage::FBuckets Buckets;

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
	};
}
