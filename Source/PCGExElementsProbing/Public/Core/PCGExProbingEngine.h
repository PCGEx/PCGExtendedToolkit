// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOctree.h"
#include "Math/PCGExProjectionDetails.h"
#include "UObject/ObjectPtr.h"

struct FPCGExContext;
class UPCGExProbeFactoryData;
class FPCGExProbeOperation;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExMT
{
	struct FScope;
	class FTaskManager;
	template <typename T>
	class TScopedSet;
}

namespace PCGExProbing
{
	/** Constrains which point pairs probes may connect, based on per-point group ids. */
	enum class EEdgeRelation : uint8
	{
		Any            = 0, // No constraint
		DifferentGroup = 1, // Only points with differing group ids may connect
		SameGroup      = 2, // Only points sharing a group id may connect
	};

	/**
	 * Shared probing core: owns the probe operations, working transforms/positions, search octree,
	 * coincidence handling and edge accumulation. Extracted from Connect Points so cluster-aware
	 * nodes can run the same probing over any point facade (e.g. cluster vtx).
	 *
	 * Usage:
	 *   Engine = MakeShared<FProbingEngine>(Facade);
	 *   Engine->SetCoincidence(...); Engine->SetProjection(...);   // optional, before Init
	 *   if (!Engine->Init(Context, Factories)) { ... no work ... }
	 *   // fill Engine->CanGenerate / Engine->AcceptConnections (sized by Init)
	 *   Engine->RunAsync(TaskManager, [Engine]{ read Engine->GetUniqueEdges() });
	 *
	 * RunAsync owns the whole schedule: it builds working data, runs the local (radius/direct) scoped
	 * loop and the global-probe passes concurrently, collapses the scoped edge sets, then fires the
	 * completion callback exactly once on the worker that finishes last. Callers never touch the
	 * per-phase steps directly - keeping the completion/ordering contract in one place.
	 *
	 * With a group constraint (EEdgeRelation != Any), radius-based probes exclude non-matching
	 * candidates at gathering time (so "closest"-style probes re-pick), while direct & global probe
	 * outputs are post-filtered on accumulation - those probes name explicit targets and cannot re-pick.
	 */
	class PCGEXELEMENTSPROBING_API FProbingEngine : public TSharedFromThis<FProbingEngine>
	{
	public:
		explicit FProbingEngine(const TSharedRef<PCGExData::FFacade>& InDataFacade);
		~FProbingEngine();

		//~ Configuration - set before Init()

		void SetCoincidence(const bool bInPreventCoincidence, const FVector& InTolerance);
		void SetProjection(const FPCGExGeo2DProjectionDetails& InDetails);

		/** Optional: per-point group ids consumed by EdgeRelation. */
		const TArray<int32>* PointGroupIds = nullptr;
		EEdgeRelation EdgeRelation = EEdgeRelation::Any;

		/** Creates & buckets operations. Returns false if no operation can produce edges. */
		bool Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExProbeFactoryData>>& InFactories);

		//~ Participation masks - sized by Init (uninitialized), fill before RunAsync()

		TArray<int8> CanGenerate;
		TArray<int8> AcceptConnections;

		/**
		 * Build working data, run all probing (local + global) on InTaskManager, collapse the results,
		 * then call InOnComplete once from the finishing worker. No-op-safe: if no operation produces
		 * work the callback still fires. GetUniqueEdges() is valid from inside the callback onward.
		 */
		void RunAsync(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, TFunction<void()>&& InOnComplete);

		TSet<uint64>& GetUniqueEdges() { return UniqueEdges; }
		const TArray<FVector>& GetWorkingPositions() const { return WorkingPositions; }
		const TArray<FTransform>& GetWorkingTransforms() const { return WorkingTransforms; }

	protected:
		TSharedRef<PCGExData::FFacade> DataFacade;
		int32 NumPoints = 0;

		TArray<TSharedPtr<FPCGExProbeOperation>> AllOperations;

		TArray<FPCGExProbeOperation*> RadiusSources;
		TArray<FPCGExProbeOperation*> DirectOperations;
		TArray<FPCGExProbeOperation*> ChainedOperations;
		TArray<FPCGExProbeOperation*> SharedOperations;
		TArray<FPCGExProbeOperation*> GlobalOperations;

		int32 NumRadiusSources = 0;
		int32 NumDirectOps = 0;
		int32 NumChainedOps = 0;
		int32 NumSharedOps = 0;

		bool bOnlyGlobalOps = false;
		bool bWantsOctree = false;

		bool bUseVariableRadius = false;
		double SharedSearchRadius = 0;

		TUniquePtr<PCGExOctree::FItemOctree> Octree;

		TArray<FTransform> WorkingTransforms;
		TArray<FVector> WorkingPositions;

		mutable FRWLock UniqueEdgesLock;
		TSharedPtr<PCGExMT::TScopedSet<uint64>> ScopedEdges;
		TSet<uint64> UniqueEdges;

		FPCGExGeo2DProjectionDetails ProjectionDetails;
		bool bUseProjection = false;

		bool bPreventCoincidence = false;
		FVector CWCoincidenceTolerance = FVector::OneVector;

		//~ RunAsync internals

		TFunction<void()> OnCompleteFn;
		int8 RunCountdown = 0;

		bool HasLocalWork() const { return !bOnlyGlobalOps; }
		bool HasGlobalWork() const { return !GlobalOperations.IsEmpty(); }

		void PrepareWorkingData();
		void PrepareScopes(const TArray<PCGExMT::FScope>& Loops);
		void ProcessScope(const PCGExMT::FScope& Scope);
		void CollapseScopedEdges();

		/** Thread-safe accumulation of global-probe outputs (relation post-filter applies). */
		void AppendEdges(const TSet<uint64>& InUniqueEdges);

		/** Decrement the phase countdown; the worker that reaches zero collapses + fires OnComplete. */
		void AdvanceRun();

		bool RequiresEdgePostFilter() const { return EdgeRelation != EEdgeRelation::Any && PointGroupIds; }
		void AppendEdgesFiltered_Unsafe(const TSet<uint64>& InEdges);
	};
}
