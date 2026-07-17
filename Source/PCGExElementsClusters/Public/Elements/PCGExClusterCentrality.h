// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExDetailsNoise.h"
#include "Math/PCGExMathContrast.h"

#include "PCGExClusterCentrality.generated.h"

namespace PCGEx
{
	class FScoredQueue;
}

class UPCGExSearchInstancedFactory;

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

UENUM()
enum class EPCGExCentralityType : uint8
{
	Betweenness       = 0 UMETA(DisplayName = "Betweenness", ToolTip="Brandes' algorithm. Measures how often a node lies on shortest paths between other nodes."),
	Closeness         = 1 UMETA(DisplayName = "Closeness", ToolTip="1 / sum(distances). Measures how close a node is to all other nodes."),
	HarmonicCloseness = 2 UMETA(DisplayName = "Harmonic Closeness", ToolTip="sum(1/distance). Like closeness but handles disconnected graphs gracefully."),
	Degree            = 3 UMETA(DisplayName = "Degree", ToolTip="Link count. Trivial O(N) measure of local connectivity."),
	Eigenvector       = 4 UMETA(DisplayName = "Eigenvector", ToolTip="Power iteration on adjacency matrix. High score = connected to other high-score nodes."),
	Katz              = 5 UMETA(DisplayName = "Katz", ToolTip="Attenuated walk count. Considers all paths with exponential decay."),
};

UENUM()
enum class EPCGExCentralityDownsampling : uint8
{
	None    = 0 UMETA(DisplayName = "None", ToolTip="All connected filters must pass."),
	Ratio   = 1 UMETA(DisplayName = "Random ratio", ToolTip="Sample using a random subset of the nodes."),
	Filters = 2 UMETA(DisplayName = "Filters", ToolTip="Use filters to drive which nodes are added to the subset")
};

/**
 * Use PCGExTransform to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-centrality"))
class UPCGExClusterCentralitySettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(ClusterCentrality, "Cluster : Centrality", "Compute centrality (betweenness, closeness, degree, eigenvector, katz).");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(NeighborSampler);
	}
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	/** Centrality measure to compute */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExCentralityType CentralityType = EPCGExCentralityType::Betweenness;

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(EditCondition="CentralityType == EPCGExCentralityType::Betweenness || CentralityType == EPCGExCentralityType::Closeness || CentralityType == EPCGExCentralityType::HarmonicCloseness", EditConditionHides))
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** Name of the attribute */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName CentralityValueAttributeName = FName("Centrality");

	/** Discrete mode write the number as-is, relative will normalize against the highest number of overlaps found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Normalize", EditConditionHides))
	bool bNormalize = true;

	/** Whether to do a OneMinus on the normalized overlap count value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ OneMinus", EditCondition="bNormalize", EditConditionHides))
	bool bOutputOneMinus = false;

	/** Apply a contrast curve to reshape the value distribution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Contrast"))
	bool bApplyContrast = false;

	/** Contrast curve type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Curve", EditCondition="bApplyContrast", EditConditionHides))
	EPCGExContrastCurve ContrastCurve = EPCGExContrastCurve::SCurve;

	/** Contrast amount. 1.0 = no change, >1 = more contrast, <1 = less contrast */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Amount", EditCondition="bApplyContrast", EditConditionHides, ClampMin=0.01))
	double ContrastAmount = 1.5;

	/** Maximum iterations for iterative centrality types (Eigenvector, Katz) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CentralityType == EPCGExCentralityType::Eigenvector || CentralityType == EPCGExCentralityType::Katz", EditConditionHides, ClampMin=1))
	int32 MaxIterations = 100;

	/** Convergence tolerance for iterative centrality types (Eigenvector, Katz) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CentralityType == EPCGExCentralityType::Eigenvector || CentralityType == EPCGExCentralityType::Katz", EditConditionHides, ClampMin=0.0))
	double Tolerance = 1e-6;

	/** Attenuation factor for Katz centrality. Must be less than 1/lambda_max (largest eigenvalue). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CentralityType == EPCGExCentralityType::Katz", EditConditionHides, ClampMin=0.001, ClampMax=0.999))
	double KatzAlpha = 0.1;

	/** Downsampling strategy to reduce processing time on large clusters. Only applies to path-based centrality types. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="CentralityType == EPCGExCentralityType::Betweenness || CentralityType == EPCGExCentralityType::Closeness || CentralityType == EPCGExCentralityType::HarmonicCloseness", EditConditionHides))
	EPCGExCentralityDownsampling DownsamplingMode = EPCGExCentralityDownsampling::None;

	/** If enabled, only compute centrality on a subset of the nodes to get a rough approximation. This is useful for large clusters, or if you want to tradeoff precision for speed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Ratio", EditCondition="DownsamplingMode == EPCGExCentralityDownsampling::Ratio", EditConditionHides))
	FPCGExRandomRatioDetails RandomDownsampling;

	bool IsPathBased() const
	{
		return CentralityType == EPCGExCentralityType::Betweenness ||
			CentralityType == EPCGExCentralityType::Closeness ||
			CentralityType == EPCGExCentralityType::HarmonicCloseness;
	}
};

struct FPCGExClusterCentralityContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExClusterCentralityElement;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExClusterCentralityElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ClusterCentrality)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExClusterCentrality
{
	using NodePred = TArray<int32, TInlineAllocator<4>>;

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExClusterCentralityContext, UPCGExClusterCentralitySettings>
	{
		friend class FBatch;

	protected:
		bool bDownsample = false;

		FRWLock CompletionLock;
		bool bVtxComplete = true;
		bool bEdgeComplete = false;

		TArray<int32> RandomSamples;
		TArray<double> DirectedEdgeScores;
		TArray<double> CentralityScores;
		TSharedPtr<PCGExMT::TScopedArray<double>> ScopedCentralityScores;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;
		virtual void OnEdgesProcessingComplete() override;

		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;
		virtual void OnNodesProcessingComplete() override;

		void TryStartCompute();

		virtual void PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		void WriteResults();

		void ProcessSingleNode_Betweenness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<double>& Sigma, TArray<double>& Delta, TArray<NodePred>& Pred, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue);
		void ProcessSingleNode_Closeness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue);
		void ProcessSingleNode_HarmonicCloseness(const int32 Index, TArray<double>& LocalScores, TArray<double>& Score, TArray<int32>& Stack, const TSharedPtr<PCGEx::FScoredQueue>& Queue);

		void ComputeEigenvector();
		void ComputeKatz();
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void Write() override;
	};
}
