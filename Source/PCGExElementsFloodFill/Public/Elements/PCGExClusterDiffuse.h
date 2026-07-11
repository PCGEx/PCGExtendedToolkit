// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExDiffusionGrowthProcessor.h"
#include "Curves/CurveFloat.h"
#include "Utils/PCGExCurveLookup.h"
#include "PCGExClusterDiffuse.generated.h"

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class FUnionOpsManager;
	class FBlendOpsSchema;
}

UENUM()
enum class EPCGExClusterDiffuseWeightMode : uint8
{
	Count         = 0 UMETA(DisplayName = "Count", ToolTip="Every contributor weighs equally; the vtx becomes the mean of the seeds that reached it."),
	Distance      = 1 UMETA(DisplayName = "Distance", ToolTip="Closer seeds (by distance to the vtx) weigh more, farther seeds less."),
	Depth         = 2 UMETA(DisplayName = "Depth", ToolTip="Weight by diffusion depth (graph steps from the seed)."),
	CountAndDepth = 3 UMETA(DisplayName = "Count + Depth", ToolTip="A blend of equal weighting and depth weighting."),
};

UENUM()
enum class EPCGExClusterDiffuseWeightSpace : uint8
{
	Relative = 0 UMETA(DisplayName = "Relative", ToolTip="Each vtx is normalized so its strongest contributor reaches full intensity -- a territorial / Voronoi-style blend. Distance/Depth only bias overlaps; a vtx reached by a single seed always gets that seed at full strength."),
	Falloff  = 1 UMETA(DisplayName = "Falloff", ToolTip="Absolute intensity: each seed is full at its origin and fades to 0 at that diffusion's furthest reach. Distance/Depth become real gradients out from each seed, and Seed Factor is an absolute multiplier. Where intensity is low the vtx fades to its original value (if it participates) or to 0."),
};

/**
 * Metadata-only diffusion over clusters: seeds spread their attributes onto reached vtx via blend
 * operations. Sibling to Cluster : Flood Fill, but with no path output and no per-vtx scalar outputs.
 *
 * Unlike Flood Fill, diffusions do NOT claim vtx -- they overlap freely. Each reached vtx is the
 * union of every seed that touched it, reconciled by a single weighted blend pass.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-diffuse"))
class UPCGExClusterDiffuseSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ClusterDiffuse, "Cluster : Diffuse", "Diffuses seed attributes onto cluster vtx via blend operations (metadata only -- no paths, overlapping diffusions).");
#endif

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Seeds settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	FPCGExFloodFillSeedPickingDetails Seeds;

	/** Defines how each vtx is diffused */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillProcessing Processing = EPCGExFloodFillProcessing::Parallel;

	/** Diffusion settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	FPCGExFloodFillFlowDetails Diffusion;

	/** What drives each seed's contribution weight when blended onto a vtx. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_NotOverridable))
	EPCGExClusterDiffuseWeightMode Weighting = EPCGExClusterDiffuseWeightMode::Count;

	/** How the weights are interpreted. Relative = normalized/territorial (single seed always full). Falloff = absolute intensity that fades from each seed to its furthest reach. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_NotOverridable))
	EPCGExClusterDiffuseWeightSpace WeightSpace = EPCGExClusterDiffuseWeightSpace::Relative;

	/** Whether the vtx's own (pre-diffusion) value participates in the blend, alongside the seeds that reached it. When off, reached vtx are fully replaced by the seed blend. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorBoolean VtxParticipates = FPCGExInputShorthandSelectorBoolean(FName("VtxParticipates"), false, false);

	/** Per-seed multiplier on each seed's contribution, read from the seeds point cloud. Scales how strongly each seed competes where diffusions overlap (1 = neutral). Has no effect on a vtx reached by a single seed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble SeedFactor = FPCGExInputShorthandSelectorDouble(FName("SeedFactor"), 1.0, false);

	/** Whether to shape the Falloff intensity with an in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_NotOverridable, EditCondition="WeightSpace == EPCGExClusterDiffuseWeightSpace::Falloff", EditConditionHides))
	bool bUseLocalFalloffCurve = false;

	/** Curve that shapes the Falloff intensity (input: 1 at the seed, fading to 0 at the diffusion's furthest reach). Linear = no shaping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_NotOverridable, DisplayName="Falloff Curve", EditCondition="WeightSpace == EPCGExClusterDiffuseWeightSpace::Falloff && bUseLocalFalloffCurve", EditConditionHides))
	FRuntimeFloatCurve LocalFalloffCurve;

	/** Curve that shapes the Falloff intensity (input: 1 at the seed, fading to 0 at the diffusion's furthest reach). Linear = no shaping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_Overridable, DisplayName="Falloff Curve", EditCondition="WeightSpace == EPCGExClusterDiffuseWeightSpace::Falloff && !bUseLocalFalloffCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> FalloffCurve;

	/** Falloff curve lookup mode / resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta = (PCG_NotOverridable, EditCondition="WeightSpace == EPCGExClusterDiffuseWeightSpace::Falloff", EditConditionHides))
	FPCGExCurveLookupDetails FalloffCurveLookup;

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExClusterDiffuseElement;
};

struct FPCGExClusterDiffuseContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExClusterDiffuseElement;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;
	TArray<TObjectPtr<const UPCGExBlendOpFactory>> SeedBlendingFactories;
	TArray<TObjectPtr<const UPCGExFillControlsFactoryData>> FillControlFactories;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
	// Per-seed contribution factor. Warmed single-threaded in Boot, then read-only off-thread by every processor.
	TSharedPtr<PCGExDetails::TSettingValue<double>> SeedFactorValue;
	// Pre-resolved (Boot, single-threaded) blend-op configs for the seeds layer, so per-processor blender init is thread-safe.
	TSharedPtr<PCGExBlending::FBlendOpsSchema> SeedBlendOpsSchema;
	// Falloff shaping curve, built once in Boot; applied to the [0,1] falloff intensity (Falloff space only).
	PCGExFloatLUT FalloffLUT;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExClusterDiffuseElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ClusterDiffuse)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExClusterDiffuse
{
	const FName SourceSeedBlendingLabel = FName(TEXT("Seed Blend Ops"));

	class FBatch;

	// One seed's contribution to a reached vtx (collected after growth, consumed by the blend pass).
	struct FContribution
	{
		int32 SeedVtxIndex = -1;   // seed's vtx point index -- the Layer 1 blend source
		int32 SeedPointIndex = -1; // seed's seeds-cloud point index (Layer 2 source); <0 marks the participation self-entry
		int32 Depth = 0;           // diffusion depth at which the seed reached this vtx (for Relative weighting)
		double NormDepth = 0.0;    // depth / diffusion max depth, [0,1] (0 at seed, 1 at furthest) -- Falloff Depth
		double NormDist = 0.0;     // path distance / diffusion max distance, [0,1] -- Falloff Distance
		double PathDistance = 0.0; // raw diffusion path length from the seed -- Relative Distance (comparable across diffusions)
	};

	class FProcessor final : public PCGExFloodFill::TDiffusionGrowthProcessor<FPCGExClusterDiffuseContext, UPCGExClusterDiffuseSettings>
	{
		friend FBatch;

	protected:
		// Contributors in CSR layout, keyed by CLUSTER NODE index (not vtx point index): node i's
		// contributors live in Contributions[ContribOffsets[i] .. ContribEnds[i]). Built after growth.
		// One flat allocation for all contributors, sized to the cluster rather than the shared vtx collection.
		TArray<int32> ContribOffsets;
		TArray<int32> ContribEnds;
		TArray<FContribution> Contributions;
		// Layer 1: blends seed VTX values onto reached vtx (source & target = vtx facade).
		TSharedPtr<PCGExBlending::FUnionOpsManager> VtxBlender;
		// Layer 2: blends seeds-cloud values onto reached vtx (source = seeds facade, target = vtx facade).
		TSharedPtr<PCGExBlending::FUnionOpsManager> SeedBlender;

		// GetInfluencesCount() intentionally left at the base default (null): claiming is disabled, so diffusions
		// overlap and the union blend reconciles multi-influence vtx.

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TDiffusionGrowthProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			bAllowEdgesDataFacadeScopedGet = false;
		}

		virtual ~FProcessor() override;

		virtual void CompleteWork() override;
		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
	protected:
		TSharedPtr<PCGExDetails::TSettingValue<int32>> FillRate;
		// Built once (single-threaded) in Process() and shared via PrepareSingle; never created in the parallel CompleteWork.
		TSharedPtr<PCGExBlending::FUnionOpsManager> VtxBlender;
		TSharedPtr<PCGExBlending::FUnionOpsManager> SeedBlender;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);
		virtual ~FBatch() override;

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void Process() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
