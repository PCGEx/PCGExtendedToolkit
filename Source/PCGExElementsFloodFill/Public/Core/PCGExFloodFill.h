// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Sorting/PCGExSortingCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "PCGExFloodFill.generated.h"

namespace PCGEx
{
	class FHashLookupMap;
}

namespace PCGExBlending
{
	class FBlendOpsManager;
}

class FPCGExBlendOperation;
class UPCGExFillControlsFactoryData;
class FPCGExFillControlOperation;

UENUM()
enum class EPCGExFloodFillNormalizedPathDepthMode : uint8
{
	FullDiffusion = 0 UMETA(DisplayName = "Full Diffusion", ToolTip="Normalize by the diffusion's max depth. Absolute position within the entire diffusion (0 at seed, 1 at the deepest captured node)."),
	FullPath      = 1 UMETA(DisplayName = "Full Path", ToolTip="Normalize by the full unpartitioned path depth. Partitioned segments preserve their position in the original path (e.g. 0.4-0.7)."),
	Partition     = 2 UMETA(DisplayName = "Partition", ToolTip="Normalize per-partition. Each path segment goes from 0 to 1 regardless of its position in the full path."),
	Cascade       = 3 UMETA(DisplayName = "Cascade", ToolTip="Hierarchical falloff. Longest paths get the full gradient (1 at seed, 0 at leaf). Branches inherit the parent value at the branch point and fall off to 0 at their own leaf. Requires partitioned output sorted by depth descending."),
};

UENUM()
enum class EPCGExFloodFillSettingSource : uint8
{
	Seed = 0 UMETA(DisplayName = "Seed", ToolTip="Read values from seed point."),
	Vtx  = 1 UMETA(DisplayName = "Vtx", ToolTip="Read values from vtx point."),
};

UENUM()
enum class EPCGExFloodFillPrioritization : uint8
{
	Heuristics = 0 UMETA(DisplayName = "Heuristics", ToolTip="Prioritize expansion based on heuristics first, then depth."),
	Depth      = 1 UMETA(DisplayName = "Depth", ToolTip="Prioritize expansion based on depth, then FillControls."),
};

UENUM()
enum class EPCGExFloodFillOrder : uint8
{
	Index   = 0 UMETA(DisplayName = "Index", ToolTip="Uses point index to drive diffusion order."),
	Sorting = 1 UMETA(DisplayName = "Sorting", ToolTip="Use sorting rules to drive diffusion order."),
};

UENUM()
enum class EPCGExFloodFillProcessing : uint8
{
	Parallel = 0 UMETA(DisplayName = "Parallel", ToolTip="Diffuse each vtx once before moving to the next iteration."),
	Sequence = 1 UMETA(DisplayName = "Sequential", ToolTip="Diffuse each vtx until it stops before moving to the next one, and so on."),
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSFLOODFILL_API FPCGExFloodFillSeedPickingDetails
{
	GENERATED_BODY()

	FPCGExFloodFillSeedPickingDetails()
	{
	}

	/** Drive how a seed point selects a node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails SeedPicking = FPCGExNodeSelectionDetails(200);

	/** Defines the sorting used for the vtx */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillOrder Ordering = EPCGExFloodFillOrder::Index;

	/** Sort direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;
};

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Flood Fill Control Step Flags"))
enum class EPCGExFloodFillControlStepsFlags : uint8
{
	None      = 0,
	Capture   = 1 << 0 UMETA(DisplayName = "Capture", ToolTip="When a node is captured by a diffusion."),
	Probing   = 1 << 1 UMETA(DisplayName = "Probing", ToolTip="When captured, a node is then 'probed', iterating through unvisited neighbors and registering them as candidates."),
	Candidate = 1 << 2 UMETA(DisplayName = "Candidate", ToolTip="When a node is identified as candidate to be flooded (e.g neighbor of a captured node)."),
};

ENUM_CLASS_FLAGS(EPCGExFloodFillControlStepsFlags)
using EPCGExFloodFillControlStepsFlagsBitmask = TEnumAsByte<EPCGExFloodFillControlStepsFlags>;

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Diffusion FillControl Flags"))
enum class EPCGExFloodFillHeuristicFlags : uint8
{
	None          = 0,
	LocalScore    = 1 << 0 UMETA(DisplayName = "Local Score", ToolTip="From neighbor to neighbor"),
	GlobalScore   = 1 << 1 UMETA(DisplayName = "Global Score", ToolTip="From seed to candidate"),
	PreviousScore = 1 << 2 UMETA(DisplayName = "Previous Score", ToolTip="Previously accumulated local score"),
};

ENUM_CLASS_FLAGS(EPCGExFloodFillHeuristicFlags)
using EPCGExFloodFillHeuristicFlagsBitmask = TEnumAsByte<EPCGExFloodFillHeuristicFlags>;

USTRUCT(BlueprintType)
struct PCGEXELEMENTSFLOODFILL_API FPCGExFloodFillFlowDetails
{
	GENERATED_BODY()

	FPCGExFloodFillFlowDetails()
	{
	}

	/** Which data should be prioritized to 'drive' diffusion */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillPrioritization Priority = EPCGExFloodFillPrioritization::Heuristics;

	/** Diffusion Rate type.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExFloodFillSettingSource FillRateSource = EPCGExFloodFillSettingSource::Seed;

	/** Diffusion rate -- how many steps each vtx grows per iteration. Zero preserves the vtx from being diffused on. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Fill Rate"))
	FPCGExInputShorthandSelectorInteger32Abs FillRate = FPCGExInputShorthandSelectorInteger32Abs(FName("FillRate"), 1, false);

#if WITH_EDITOR
	// Migrates the deprecated FillRateInput/Attribute/Constant triple into FillRate.
	void ApplyDeprecation();
#endif

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType FillRateInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName FillRateAttribute_DEPRECATED = FName("FillRate");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 FillRateConstant_DEPRECATED = 1;

#pragma endregion
};

namespace PCGExData
{
	class FPointIOCollection;
}

struct FPCGExAttributeToTagDetails;

namespace PCGExFloodFill
{
	const FName OutputFillControlsLabel = TEXT("Fill Control");
	const FName SourceFillControlsLabel = TEXT("Fill Controls");

	class FDiffusion;

	/**
	 * Configuration snapshot for FDiffusion, reducing coupling to FFillControlsHandler.
	 * Created once during diffusion setup and passed to FDiffusion constructor.
	 */
	struct FDiffusionConfig
	{
		EPCGExFloodFillPrioritization Sorting = EPCGExFloodFillPrioritization::Heuristics;

		FDiffusionConfig() = default;

		explicit FDiffusionConfig(const FPCGExFloodFillFlowDetails& Details)
			: Sorting(Details.Priority)
		{
		}
	};

	/**
	 * Perform diffusion blending from a completed diffusion's captured nodes.
	 * Separated from FDiffusion to keep growth logic decoupled from output blending.
	 * @param Diffusion The completed diffusion with captured nodes
	 * @param InVtxFacade Vertex data facade (unused, kept for potential future weight computation)
	 * @param InBlendOps Blend operations manager for performing the blending
	 * @param OutIndices Output array of point indices that were captured
	 */
	PCGEXELEMENTSFLOODFILL_API void DiffuseAndBlend(
		const FDiffusion& Diffusion,
		const TSharedPtr<PCGExData::FFacade>& InVtxFacade,
		const TSharedPtr<PCGExBlending::FBlendOpsManager>& InBlendOps,
		TArray<int32>& OutIndices);

	struct FCandidate
	{
		const PCGExClusters::FNode* Node = nullptr;
		PCGExGraphs::FLink Link;
		int32 CaptureIndex = -1;
		int32 Depth = 0;
		double PathScore = 0;
		double Score = 0;
		double PathDistance = 0;
		double Distance = 0;
		double AccumulatedValue = 0; // Generic accumulator for attribute-based controls

		FCandidate() = default;
	};

	// Min-heap comparator for candidate prioritization
	// Returns true if A should be closer to the root (higher priority = LOWER score/depth to pick first)
	// This creates a min-heap where HeapPop returns the lowest scoring candidate
	struct FCandidateHeapComparator
	{
		EPCGExFloodFillPrioritization Mode = EPCGExFloodFillPrioritization::Heuristics;

		FCandidateHeapComparator() = default;

		explicit FCandidateHeapComparator(EPCGExFloodFillPrioritization InMode)
			: Mode(InMode)
		{
		}

		FORCEINLINE bool operator()(const FCandidate& A, const FCandidate& B) const
		{
			// Min-heap: return true when A has LOWER priority than B (A should sink, B rises)
			// HeapPop will return the element with lowest score (highest priority for spreading)
			if (Mode == EPCGExFloodFillPrioritization::Heuristics)
			{
				if (A.Score == B.Score)
				{
					return A.Depth < B.Depth;
				}
				return A.Score < B.Score;
			}
			// Depth
			if (A.Depth == B.Depth)
			{
				return A.Score < B.Score;
			}
			return A.Depth < B.Depth;
		}
	};

	class FFillControlsHandler;

	class FDiffusion : public TSharedFromThis<FDiffusion>
	{
		friend class FFillControlsHandler;

	protected:
		TArray<bool> Visited; // Indexed by node index, faster than TSet for membership checks

		int32 MaxDepth = 0;
		double MaxDistance = 0;

		TSharedPtr<FFillControlsHandler> FillControlsHandler;
		FDiffusionConfig Config;                 // Local config snapshot, set by FFillControlsHandler::PrepareForDiffusions
		FCandidateHeapComparator HeapComparator; // Cached comparator for heap operations

	public:
		int32 Index = -1;
		bool bStopped = false;
		const PCGExClusters::FNode* SeedNode = nullptr;
		int32 SeedIndex = -1;
		TSet<int32> Endpoints;

		TSharedPtr<PCGEx::FHashLookupMap> TravelStack; // Required for FillControls & Heuristics
		TSharedPtr<PCGExClusters::FCluster> Cluster;

		TArray<FCandidate> Candidates;
		TArray<FCandidate> Captured;

		FDiffusion(const TSharedPtr<FFillControlsHandler>& InFillControlsHandler, const TSharedPtr<PCGExClusters::FCluster>& InCluster, const PCGExClusters::FNode* InSeedNode);
		~FDiffusion() = default;

		FORCEINLINE const FDiffusionConfig& GetConfig() const
		{
			return Config;
		}

		FORCEINLINE int32 GetMaxDepth() const
		{
			return MaxDepth;
		}

		FORCEINLINE double GetMaxDistance() const
		{
			return MaxDistance;
		}

		int32 GetSettingsIndex(EPCGExFloodFillSettingSource Source) const;

		void Init(const int32 InSeedIndex);
		void Probe(const FCandidate& From);
		void Grow();
		void PostGrow();
	};

	class PCGEXELEMENTSFLOODFILL_API FFillControlsHandler : public TSharedFromThis<FFillControlsHandler>
	{
	protected:
		FPCGExContext* ExecutionContext = nullptr;
		bool bIsValidHandler = false;
		int32 NumDiffusions = 0;

		// Subselections by capability
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsScoring;
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsProbe;
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsCandidate;
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsCapture;
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsCaptureNotify;
		TArray<TSharedPtr<FPCGExFillControlOperation>> SubOpsProbeFanout;

	public:
		mutable FRWLock HandlerLock;

		// Set in BuildFrom -- whether any control opts into capture-notify / probe fan-out
		// limiting, so hot paths can skip the dispatch.
		bool bHasCaptureNotify = false;
		bool bHasProbeFanout = false;

		TSharedPtr<PCGExClusters::FCluster> Cluster;
		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;
		TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
		TWeakPtr<PCGExHeuristics::FHandler> HeuristicsHandler;

		TArray<TSharedPtr<FPCGExFillControlOperation>> Operations;

		TSharedPtr<TArray<int8>> InfluencesCount;

		TSharedPtr<TArray<int32>> SeedIndices;
		TSharedPtr<TArray<int32>> SeedNodeIndices;

		FDiffusionConfig DiffusionConfig; // Shared config for all diffusions in this handler

		FORCEINLINE bool IsValidHandler() const
		{
			return bIsValidHandler;
		}

		FORCEINLINE int32 GetNumDiffusions() const
		{
			return NumDiffusions;
		}

		FORCEINLINE const FDiffusionConfig& GetDiffusionConfig() const
		{
			return DiffusionConfig;
		}

		FFillControlsHandler(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataCache, const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache, const TSharedPtr<PCGExData::FFacade>& InSeedsDataCache, const TArray<TObjectPtr<const UPCGExFillControlsFactoryData>>& InFactories);

		~FFillControlsHandler();

		bool BuildFrom(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExFillControlsFactoryData>>& InFactories);

		bool PrepareForDiffusions(const TArray<TSharedPtr<FDiffusion>>& Diffusions, const FPCGExFloodFillFlowDetails& Details);

		// Scoring phase - called before validation
		void ScoreCandidate(const FDiffusion* Diffusion, const FCandidate& From, FCandidate& OutCandidate);

		// Validation phase
		bool TryCapture(const FDiffusion* Diffusion, const FCandidate& Candidate);
		bool IsValidProbe(const FDiffusion* Diffusion, const FCandidate& Candidate);
		bool IsValidCandidate(const FDiffusion* Diffusion, const FCandidate& From, const FCandidate& Candidate);

		// Aggregate probe-time fan-out limit across probe-limiting controls (MAX_int32 if none).
		int32 GetProbeFanoutLimit(const FDiffusion* Diffusion, const FCandidate& From);
	};

	/**
	 * Stateless helper class for writing diffusion paths to output collections.
	 * Separates path output concerns from FProcessor orchestration.
	 */
	class PCGEXELEMENTSFLOODFILL_API FDiffusionPathWriter
	{
	public:
		FDiffusionPathWriter(
			const TSharedRef<PCGExClusters::FCluster>& InCluster,
			const TSharedRef<PCGExData::FFacade>& InVtxDataFacade,
			const TSharedRef<PCGExData::FPointIOCollection>& InPaths,
			const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
			const TSharedPtr<TArray<int32>>& InDiffusionDepths);

		void WriteFullPath(
			const FDiffusion& Diffusion,
			int32 EndpointNodeIndex,
			int32 EndpointDepth,
			int32 MaxDiffusionDepth,
			FName NormalizedPathDepthName,
			EPCGExFloodFillNormalizedPathDepthMode NormalizedPathDepthMode,
			const FPCGExAttributeToTagDetails& SeedTags,
			const TSharedRef<PCGExData::FFacade>& SeedsDataFacade);

		void WritePartitionedPath(
			const FDiffusion& Diffusion,
			TArray<int32>& PathIndices,
			int32 EndpointDepth,
			int32 MaxDiffusionDepth,
			FName NormalizedPathDepthName,
			EPCGExFloodFillNormalizedPathDepthMode NormalizedPathDepthMode,
			const FPCGExAttributeToTagDetails& SeedTags,
			const TSharedRef<PCGExData::FFacade>& SeedsDataFacade,
			const TArray<double>* CascadeValues = nullptr);

	protected:
		void WriteNormalizedPathDepth(
			const TSharedRef<PCGExData::FFacade>& PathFacade,
			const TArray<int32>& PathIndices,
			int32 EndpointDepth,
			int32 MaxDiffusionDepth,
			FName NormalizedPathDepthName,
			EPCGExFloodFillNormalizedPathDepthMode Mode,
			const TArray<double>* CascadeValues = nullptr);

		TSharedRef<PCGExClusters::FCluster> Cluster;
		TSharedRef<PCGExData::FFacade> VtxDataFacade;
		TSharedRef<PCGExData::FPointIOCollection> Paths;
		TSharedPtr<PCGExMT::FTaskManager> TaskManager;
		TSharedPtr<TArray<int32>> DiffusionDepths;
	};
}
