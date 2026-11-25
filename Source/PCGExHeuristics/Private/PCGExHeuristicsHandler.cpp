// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExHeuristicsHandler.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExHeuristicOperation.h"
#include "Heuristics/PCGExHeuristicFeedback.h"

#define PCGEX_INIT_HEURISTIC_OPERATION(_OP, _FACTORY)\
_OP->PrimaryDataFacade = VtxDataFacade;\
_OP->SecondaryDataFacade = EdgeDataFacade;\
_OP->WeightFactor = _FACTORY->WeightFactor;\
_OP->ReferenceWeight = _FACTORY->WeightFactor;

namespace PCGExHeuristics
{
#pragma region FLocalFeedbackHandler

	double FLocalFeedbackHandler::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
	{
		double GScore = 0;
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Feedback : Feedbacks)
		{
			GScore += Feedback->GetGlobalScore(From, Seed, Goal);
		}
		return GScore;
	}

	double FLocalFeedbackHandler::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
	{
		double EScore = 0;
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Feedback : Feedbacks)
		{
			EScore += Feedback->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack);
		}
		return EScore;
	}

	void FLocalFeedbackHandler::FeedbackPointScore(const PCGExClusters::FNode& Node)
	{
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Feedback : Feedbacks)
		{
			Feedback->FeedbackPointScore(Node);
		}
	}

	void FLocalFeedbackHandler::FeedbackScore(const PCGExClusters::FNode& Node, const PCGExGraphs::FEdge& Edge)
	{
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Feedback : Feedbacks)
		{
			Feedback->FeedbackScore(Node, Edge);
		}
	}

	void FLocalFeedbackHandler::ResetFeedback()
	{
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Feedback : Feedbacks)
		{
			Feedback->ResetFeedback();
		}
	}

#pragma endregion

#pragma region ScoreModes

	// One struct per EPCGExHeuristicScoreMode, consumed by TScoreHandler.
	// Contribution maps an op's weighted score and its weight into the mode's accumulation domain.
	// The two bSkip flags are per-mode on purpose: they are each mode's own zero-weight guards.
	namespace ScoreModes
	{
		/** Clamp floor for modes that divide by, or take the log of, scores */
		constexpr double MinClampedScore = 1e-10;

		/** sum(score x weight) / sum(weight) */
		struct FWeightedAverage
		{
			static constexpr bool bSkipZeroDynamicWeight = false;
			static constexpr bool bSkipZeroFeedbackWeight = false;

			FORCEINLINE static double Identity() { return 0; }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return WeightedScore; }
			FORCEINLINE static double Reduce(const double A, const double B) { return A + B; }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return TotalWeight > 0 ? Accumulated / TotalWeight : 0; }
		};

		/** product(score^weight)^(1/sum(weight)), accumulated as a weighted log sum */
		struct FGeometricMean
		{
			static constexpr bool bSkipZeroDynamicWeight = true;
			static constexpr bool bSkipZeroFeedbackWeight = false;

			FORCEINLINE static double Identity() { return 0; }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return Weight * FMath::Loge(FMath::Max(MinClampedScore, WeightedScore) / Weight); }
			FORCEINLINE static double Reduce(const double A, const double B) { return A + B; }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return TotalWeight > 0 ? FMath::Exp(Accumulated / TotalWeight) : 0; }
		};

		/** sum(score x weight), no normalization */
		struct FWeightedSum
		{
			static constexpr bool bSkipZeroDynamicWeight = false;
			static constexpr bool bSkipZeroFeedbackWeight = false;

			FORCEINLINE static double Identity() { return 0; }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return WeightedScore; }
			FORCEINLINE static double Reduce(const double A, const double B) { return A + B; }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return Accumulated; }
		};

		/** sum(weight) / sum(weight/score), a single low score dominates */
		struct FHarmonicMean
		{
			static constexpr bool bSkipZeroDynamicWeight = false;
			static constexpr bool bSkipZeroFeedbackWeight = false;

			FORCEINLINE static double Identity() { return 0; }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return Weight / (FMath::Max(MinClampedScore, WeightedScore) / Weight); }
			FORCEINLINE static double Reduce(const double A, const double B) { return A + B; }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return Accumulated > 0 ? TotalWeight / Accumulated : 0; }
		};

		/** Lowest weight-normalized score: any heuristic can allow passage */
		struct FMin
		{
			static constexpr bool bSkipZeroDynamicWeight = true;
			static constexpr bool bSkipZeroFeedbackWeight = true;

			FORCEINLINE static double Identity() { return TNumericLimits<double>::Max(); }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return WeightedScore / Weight; }
			FORCEINLINE static double Reduce(const double A, const double B) { return FMath::Min(A, B); }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return Accumulated == TNumericLimits<double>::Max() ? 0 : Accumulated; }
		};

		/** Highest weight-normalized score: any heuristic can block passage */
		struct FMax
		{
			static constexpr bool bSkipZeroDynamicWeight = true;
			static constexpr bool bSkipZeroFeedbackWeight = true;

			FORCEINLINE static double Identity() { return TNumericLimits<double>::Lowest(); }
			FORCEINLINE static double Contribution(const double WeightedScore, const double Weight) { return WeightedScore / Weight; }
			FORCEINLINE static double Reduce(const double A, const double B) { return FMath::Max(A, B); }
			FORCEINLINE static double Finish(const double Accumulated, const double TotalWeight) { return Accumulated == TNumericLimits<double>::Lowest() ? 0 : Accumulated; }
		};
	}

#pragma endregion

#pragma region TScoreHandler

	/** Handler for one aggregation mode; Mode is a ScoreModes struct. */
	template <typename Mode>
	class TScoreHandler final : public FHandler
	{
	public:
		using FHandler::FHandler;

		virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback) const override
		{
			double Score = Mode::Identity();
			double TotalWeight = TotalStaticWeight;

			for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
			{
				Score = Mode::Reduce(Score, Mode::Contribution(Op->GetGlobalScore(From, Seed, Goal), Op->WeightFactor));
			}

			if (LocalFeedback && (!Mode::bSkipZeroFeedbackWeight || LocalFeedback->TotalStaticWeight > 0))
			{
				Score = Mode::Reduce(Score, Mode::Contribution(LocalFeedback->GetGlobalScore(From, Seed, Goal), LocalFeedback->TotalStaticWeight));
				TotalWeight += LocalFeedback->TotalStaticWeight;
			}

			return Mode::Finish(Score, TotalWeight);
		}

		virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, const FLocalFeedbackHandler* LocalFeedback, PCGEx::FHashLookup* TravelStack) const override
		{
			double Score = Mode::Identity();
			double TotalWeight = TotalStaticWeight;

			if (!bUseDynamicWeight)
			{
				if (bHasBakedEdgeScores)
				{
					Score = GetBakedEdgeScore(Edge, From);
					for (const FPCGExHeuristicOperation* Op : DynamicEdgeOps)
					{
						Score = Mode::Reduce(Score, Mode::Contribution(Op->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack), Op->WeightFactor));
					}
				}
				else
				{
					for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
					{
						Score = Mode::Reduce(Score, Mode::Contribution(Op->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack), Op->WeightFactor));
					}
				}
			}
			else
			{
				// Per-edge custom weight multipliers scale both the score and the weight of each op
				TotalWeight = 0;
				for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
				{
					const double Multiplier = Op->GetCustomWeightMultiplier(To.Index, Edge.PointIndex);
					const double EffectiveWeight = Op->WeightFactor * Multiplier;
					if (!Mode::bSkipZeroDynamicWeight || EffectiveWeight > 0)
					{
						Score = Mode::Reduce(Score, Mode::Contribution(Op->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack) * Multiplier, EffectiveWeight));
						TotalWeight += EffectiveWeight;
					}
				}
			}

			if (LocalFeedback && (!Mode::bSkipZeroFeedbackWeight || LocalFeedback->TotalStaticWeight > 0))
			{
				Score = Mode::Reduce(Score, Mode::Contribution(LocalFeedback->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack), LocalFeedback->TotalStaticWeight));
				TotalWeight += LocalFeedback->TotalStaticWeight;
			}

			return Mode::Finish(Score, TotalWeight);
		}

	protected:
		virtual double BakeContribution(const double WeightedScore, const double Weight) const override { return Mode::Contribution(WeightedScore, Weight); }
		virtual double BakeReduce(const double A, const double B) const override { return Mode::Reduce(A, B); }
		virtual double BakeIdentity() const override { return Mode::Identity(); }
	};

#pragma endregion

#pragma region FHandler

	FHandler::FHandler(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InVtxDataCache, const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories)
		: ExecutionContext(InContext)
		  , VtxDataFacade(InVtxDataCache)
		  , EdgeDataFacade(InEdgeDataCache)
	{
		bIsValidHandler = BuildFrom(InContext, InFactories);
	}

	FHandler::~FHandler()
	{
		Operations.Empty();
		Feedbacks.Empty();
	}

	bool FHandler::BuildFrom(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories)
	{
		for (const UPCGExHeuristicsFactoryData* OperationFactory : InFactories)
		{
			TSharedPtr<FPCGExHeuristicOperation> Operation = nullptr;
			bool bIsFeedback = false;
			if (const UPCGExHeuristicsFactoryFeedback* FeedbackFactory = Cast<UPCGExHeuristicsFactoryFeedback>(OperationFactory))
			{
				bIsFeedback = true;

				if (!FeedbackFactory->IsGlobal())
				{
					LocalFeedbackFactories.Add(OperationFactory);
					continue;
				}
			}

			Operation = OperationFactory->CreateOperation(InContext);

			if (bIsFeedback)
			{
				Feedbacks.Add(StaticCastSharedPtr<FPCGExHeuristicFeedback>(Operation));
			}
			Operations.Add(Operation);

			PCGEX_INIT_HEURISTIC_OPERATION(Operation, OperationFactory)

			Operation->BindContext(InContext);
		}

		if (Operations.IsEmpty())
		{
			if (!LocalFeedbackFactories.IsEmpty())
			{
				PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing valid base heuristics : cannot work with feedback alone."))
			}
			else
			{
				PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing valid base heuristics"))
			}

			return false;
		}

		return true;
	}

	void FHandler::PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster)
	{
		// Raw world-space lengths; each op normalizes explicitly (Distance by cluster bounds,
		// Gradient by MaxEdgeLength) so the scale is independent of who computed them first.
		InCluster->ComputeEdgeLengths();

		Cluster = InCluster;
		bUseDynamicWeight = false;
		for (const TSharedPtr<FPCGExHeuristicOperation>& Operation : Operations)
		{
			Operation->PrepareForCluster(InCluster);
			if (Operation->bHasCustomLocalWeightMultiplier)
			{
				bUseDynamicWeight = true;
			}
		}
	}

	void FHandler::CompleteClusterPreparation()
	{
		TotalStaticWeight = 0;
		StaticEdgeOps.Reset();
		DynamicEdgeOps.Reset();
		BakedStaticEdgeScores.Empty();
		bHasBakedEdgeScores = false;

		for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
		{
			TotalStaticWeight += Op->WeightFactor;

			if (Op->HasStaticEdgeScore())
			{
				StaticEdgeOps.Add(Op.Get());
			}
			else
			{
				DynamicEdgeOps.Add(Op.Get());
			}
		}
	}

	bool FHandler::UsesTravelHistory() const
	{
		for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
		{
			if (Op->UsesTravelHistory())
			{
				return true;
			}
		}
		return false;
	}

	void FHandler::BakeStaticEdgeScores()
	{
		// Per-edge local weight multipliers make per-op contributions vary per edge in ways the
		// bake can't capture -- fall back to the full per-op path in that case.
		if (bHasBakedEdgeScores || StaticEdgeOps.IsEmpty() || bUseDynamicWeight || !Cluster)
		{
			return;
		}

		const TArray<PCGExGraphs::FEdge>& EdgesRef = *Cluster->Edges;
		const int32 NumEdges = EdgesRef.Num();

		BakedStaticEdgeScores.SetNumUninitialized(NumEdges * 2);

		for (int32 i = 0; i < NumEdges; i++)
		{
			const PCGExGraphs::FEdge& Edge = EdgesRef[i];
			const PCGExClusters::FNode& Start = *Cluster->GetEdgeStart(Edge);
			const PCGExClusters::FNode& End = *Cluster->GetEdgeEnd(Edge);

			double Forward = BakeIdentity();
			double Backward = BakeIdentity();

			for (const FPCGExHeuristicOperation* Op : StaticEdgeOps)
			{
				// Static ops ignore Seed/Goal/TravelStack by contract; endpoints stand in for them.
				Forward = BakeReduce(Forward, BakeContribution(Op->GetEdgeScore(Start, End, Edge, Start, End), Op->WeightFactor));
				Backward = BakeReduce(Backward, BakeContribution(Op->GetEdgeScore(End, Start, Edge, End, Start), Op->WeightFactor));
			}

			BakedStaticEdgeScores[Edge.Index << 1] = Forward;
			BakedStaticEdgeScores[(Edge.Index << 1) | 1] = Backward;
		}

		bHasBakedEdgeScores = true;
	}

	void FHandler::FeedbackPointScore(const PCGExClusters::FNode& Node)
	{
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Op : Feedbacks)
		{
			Op->FeedbackPointScore(Node);
		}
	}

	void FHandler::FeedbackScore(const PCGExClusters::FNode& Node, const PCGExGraphs::FEdge& Edge)
	{
		for (const TSharedPtr<FPCGExHeuristicFeedback>& Op : Feedbacks)
		{
			Op->FeedbackScore(Node, Edge);
		}
	}

	FVector FHandler::GetSeedUVW() const
	{
		FVector UVW = FVector::ZeroVector;
		for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
		{
			UVW += Op->GetSeedUVW();
		}
		return UVW;
	}

	FVector FHandler::GetGoalUVW() const
	{
		FVector UVW = FVector::ZeroVector;
		for (const TSharedPtr<FPCGExHeuristicOperation>& Op : Operations)
		{
			UVW += Op->GetGoalUVW();
		}
		return UVW;
	}

	const PCGExClusters::FNode* FHandler::GetRoamingSeed()
	{
		if (RoamingSeedNode)
		{
			return RoamingSeedNode;
		}
		RoamingSeedNode = Cluster->GetRoamingNode(GetSeedUVW());
		return RoamingSeedNode;
	}

	const PCGExClusters::FNode* FHandler::GetRoamingGoal()
	{
		if (RoamingGoalNode)
		{
			return RoamingGoalNode;
		}
		RoamingGoalNode = Cluster->GetRoamingNode(GetGoalUVW());
		return RoamingGoalNode;
	}

	TSharedPtr<FLocalFeedbackHandler> FHandler::MakeLocalFeedbackHandler(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
	{
		if (LocalFeedbackFactories.IsEmpty())
		{
			return nullptr;
		}

		PCGEX_MAKE_SHARED(NewLocalFeedbackHandler, FLocalFeedbackHandler, ExecutionContext)

		for (const UPCGExHeuristicsFactoryData* Factory : LocalFeedbackFactories)
		{
			TSharedPtr<FPCGExHeuristicFeedback> Feedback = StaticCastSharedPtr<FPCGExHeuristicFeedback>(Factory->CreateOperation(ExecutionContext));

			PCGEX_INIT_HEURISTIC_OPERATION(Feedback, Factory)

			NewLocalFeedbackHandler->Feedbacks.Add(Feedback);
			NewLocalFeedbackHandler->TotalStaticWeight += Factory->WeightFactor;

			Feedback->PrepareForCluster(InCluster);
		}

		return NewLocalFeedbackHandler;
	}

	TSharedPtr<FLocalFeedbackHandler> FHandler::AcquireLocalFeedbackHandler(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
	{
		if (LocalFeedbackFactories.IsEmpty())
		{
			return nullptr;
		}

		{
			FScopeLock Lock(&PoolLock);
			if (!LocalFeedbackHandlerPool.IsEmpty())
			{
				TSharedPtr<FLocalFeedbackHandler> Handler = LocalFeedbackHandlerPool.Pop();
				Handler->ResetFeedback();
				return Handler;
			}
		}

		// Pool is empty, create new handler
		return MakeLocalFeedbackHandler(InCluster);
	}

	void FHandler::ReleaseLocalFeedbackHandler(const TSharedPtr<FLocalFeedbackHandler>& Handler)
	{
		if (!Handler)
		{
			return;
		}

		FScopeLock Lock(&PoolLock);
		LocalFeedbackHandlerPool.Add(Handler);
	}

	TSharedPtr<FHandler> FHandler::CreateHandler(
		EPCGExHeuristicScoreMode ScoreMode,
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FFacade>& InVtxDataCache,
		const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache,
		const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories)
	{
		switch (ScoreMode)
		{
		case EPCGExHeuristicScoreMode::WeightedAverage:
			return MakeShared<TScoreHandler<ScoreModes::FWeightedAverage>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		case EPCGExHeuristicScoreMode::GeometricMean:
			return MakeShared<TScoreHandler<ScoreModes::FGeometricMean>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		case EPCGExHeuristicScoreMode::WeightedSum:
			return MakeShared<TScoreHandler<ScoreModes::FWeightedSum>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		case EPCGExHeuristicScoreMode::HarmonicMean:
			return MakeShared<TScoreHandler<ScoreModes::FHarmonicMean>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		case EPCGExHeuristicScoreMode::Min:
			return MakeShared<TScoreHandler<ScoreModes::FMin>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		case EPCGExHeuristicScoreMode::Max:
			return MakeShared<TScoreHandler<ScoreModes::FMax>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		default:
			return MakeShared<TScoreHandler<ScoreModes::FWeightedAverage>>(InContext, InVtxDataCache, InEdgeDataCache, InFactories);
		}
	}

#pragma endregion
}

#undef PCGEX_INIT_HEURISTIC_OPERATION
