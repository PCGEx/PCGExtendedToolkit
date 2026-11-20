// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsCommon.h"
#include "Clusters/PCGExEdge.h"
#include "Clusters/PCGExNode.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"

class FPCGExHeuristicFeedback;
class FPCGExHeuristicOperation;

namespace PCGEx
{
	class FHashLookup;
}

namespace PCGExHeuristics
{
	class PCGEXHEURISTICS_API FLocalFeedbackHandler : public TSharedFromThis<FLocalFeedbackHandler>
	{
	public:
		FPCGExContext* ExecutionContext = nullptr;

		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

		TArray<TSharedPtr<FPCGExHeuristicFeedback>> Feedbacks;
		double TotalStaticWeight = 0;

		explicit FLocalFeedbackHandler(FPCGExContext* InContext)
			: ExecutionContext(InContext)
		{
		}

		~FLocalFeedbackHandler() = default;

		double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const;

		double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const;

		void FeedbackPointScore(const PCGExClusters::FNode& Node);

		void FeedbackScore(const PCGExClusters::FNode& Node, const PCGExGraphs::FEdge& Edge);

		/** Reset all feedback counts for reuse */
		void ResetFeedback();
	};

	/**
	 * Base handler class for heuristics. Subclasses implement different score aggregation modes.
	 */
	class PCGEXHEURISTICS_API FHandler : public TSharedFromThis<FHandler>
	{
	protected:
		FPCGExContext* ExecutionContext = nullptr;
		bool bIsValidHandler = false;

	public:
		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

		TArray<TSharedPtr<FPCGExHeuristicOperation>> Operations;
		TArray<TSharedPtr<FPCGExHeuristicFeedback>> Feedbacks;
		TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> LocalFeedbackFactories;

		TSharedPtr<PCGExClusters::FCluster> Cluster;

		double TotalStaticWeight = 0;
		bool bUseDynamicWeight = false;

		bool IsValidHandler() const
		{
			return bIsValidHandler;
		}

		bool HasGlobalFeedback() const
		{
			return !Feedbacks.IsEmpty();
		};

		bool HasLocalFeedback() const
		{
			return !LocalFeedbackFactories.IsEmpty();
		};

		bool HasAnyFeedback() const
		{
			return HasGlobalFeedback() || HasLocalFeedback();
		};

		FHandler(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InVtxDataCache, const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories);
		virtual ~FHandler();

		bool BuildFrom(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories);
		void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster);
		void CompleteClusterPreparation();

		/** Pre-aggregates the static portion of edge scores once per directed edge, so GetEdgeScore only
		 * evaluates goal/travel-dependent ops afterwards. Costs one full sweep of all directed edges x static
		 * ops -- call it only when the expected number of edge-score evaluations justifies it (multiple
		 * queries, iterative growth, diffusion...). No-op when nothing is bakeable or dynamic weights are used. */
		void BakeStaticEdgeScores();

		FORCEINLINE bool HasBakedEdgeScores() const
		{
			return bHasBakedEdgeScores;
		}

		/** Override in subclasses to implement different score aggregation modes */
		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const = 0;

		/** Override in subclasses to implement different score aggregation modes */
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const = 0;

		void FeedbackPointScore(const PCGExClusters::FNode& Node);
		void FeedbackScore(const PCGExClusters::FNode& Node, const PCGExGraphs::FEdge& Edge);

		FVector GetSeedUVW() const;
		FVector GetGoalUVW() const;

		/** Lazily resolves & caches the roaming seed/goal nodes. The first call runs a closest-node
		 * search and writes the cache unsynchronized -- call once from single-threaded prep before
		 * any parallel use (see Refine/Decomp PrepareForCluster, FloodFill PrepareForDiffusions). */
		const PCGExClusters::FNode* GetRoamingSeed();
		const PCGExClusters::FNode* GetRoamingGoal();

		TSharedPtr<FLocalFeedbackHandler> MakeLocalFeedbackHandler(const TSharedPtr<const PCGExClusters::FCluster>& InCluster);

		/** Acquire a local feedback handler from pool (creates new if pool is empty) */
		TSharedPtr<FLocalFeedbackHandler> AcquireLocalFeedbackHandler(const TSharedPtr<const PCGExClusters::FCluster>& InCluster);

		/** Release a local feedback handler back to pool for reuse */
		void ReleaseLocalFeedbackHandler(const TSharedPtr<FLocalFeedbackHandler>& Handler);

		/** Factory function to create a handler with the specified scoring mode */
		static TSharedPtr<FHandler> CreateHandler(
			EPCGExHeuristicScoreMode ScoreMode,
			FPCGExContext* InContext,
			const TSharedPtr<PCGExData::FFacade>& InVtxDataCache,
			const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache,
			const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories);

	protected:
		PCGExClusters::FNode* RoamingSeedNode = nullptr;
		PCGExClusters::FNode* RoamingGoalNode = nullptr;

		/** Pool of reusable local feedback handlers */
		TArray<TSharedPtr<FLocalFeedbackHandler>> LocalFeedbackHandlerPool;
		FCriticalSection PoolLock;

		/** Clamp floor shared by aggregation modes that divide by, or take the log of, scores */
		static constexpr double MinClampedScore = 1e-10;

		/** Operations split by HasStaticEdgeScore, built by CompleteClusterPreparation.
		 * Raw pointers -- lifetime owned by Operations. */
		TArray<FPCGExHeuristicOperation*> StaticEdgeOps;
		TArray<FPCGExHeuristicOperation*> DynamicEdgeOps;

		/** Per-directed-edge pre-aggregated static contributions, in this mode's accumulation domain.
		 * Two entries per edge: [Index*2] start-to-end, [Index*2+1] end-to-start. */
		TArray<double> BakedStaticEdgeScores;
		bool bHasBakedEdgeScores = false;

		/** From must be one of the edge endpoints -- the same contract heuristics already rely on. */
		FORCEINLINE double GetBakedEdgeScore(const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& From) const
		{
			return BakedStaticEdgeScores[(Edge.Index << 1) | (Edge.Start == static_cast<uint32>(From.PointIndex) ? 0 : 1)];
		}

		// Per-mode hooks driving the static edge-score bake; each must mirror the per-op math
		// its GetEdgeScore applies to weighted op scores.

		/** Transforms a single op's weighted edge score into this mode's accumulation domain */
		virtual double BakeContribution(const double WeightedScore, const double Weight) const = 0;
		/** Combines two values in this mode's accumulation domain */
		virtual double BakeReduce(const double A, const double B) const = 0;
		/** Neutral element of BakeReduce */
		virtual double BakeIdentity() const = 0;
	};

	//
	// Concrete handler implementations
	//

	/** Weighted average: sum(score x weight) / sum(weight) */
	class PCGEXHEURISTICS_API FHandlerWeightedAverage final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return WeightedScore; }
		virtual double BakeReduce(const double A, const double B) const override { return A + B; }
		virtual double BakeIdentity() const override { return 0; }
	};

	/** Geometric mean: product(score^weight)^(1/sum(weight)) */
	class PCGEXHEURISTICS_API FHandlerGeometricMean final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return Weight * FMath::Loge(FMath::Max(MinClampedScore, WeightedScore) / Weight); }
		virtual double BakeReduce(const double A, const double B) const override { return A + B; }
		virtual double BakeIdentity() const override { return 0; }
	};

	/** Weighted sum: sum(score x weight) - no normalization */
	class PCGEXHEURISTICS_API FHandlerWeightedSum final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return WeightedScore; }
		virtual double BakeReduce(const double A, const double B) const override { return A + B; }
		virtual double BakeIdentity() const override { return 0; }
	};

	/** Harmonic mean: sum(weight) / sum(weight/score) - heavily emphasizes low scores */
	class PCGEXHEURISTICS_API FHandlerHarmonicMean final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return Weight / (FMath::Max(MinClampedScore, WeightedScore) / Weight); }
		virtual double BakeReduce(const double A, const double B) const override { return A + B; }
		virtual double BakeIdentity() const override { return 0; }
	};

	/** Minimum: returns the lowest weighted score - most permissive */
	class PCGEXHEURISTICS_API FHandlerMin final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return WeightedScore / Weight; }
		virtual double BakeReduce(const double A, const double B) const override { return FMath::Min(A, B); }
		virtual double BakeIdentity() const override { return TNumericLimits<double>::Max(); }
	};

	/** Maximum: returns the highest weighted score - most restrictive */
	class PCGEXHEURISTICS_API FHandlerMax final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr) const override;
		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback = nullptr, PCGEx::FHashLookup* TravelStack = nullptr) const override;

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return WeightedScore / Weight; }
		virtual double BakeReduce(const double A, const double B) const override { return FMath::Max(A, B); }
		virtual double BakeIdentity() const override { return TNumericLimits<double>::Lowest(); }
	};
}
