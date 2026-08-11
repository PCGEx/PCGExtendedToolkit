// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPointPropertiesTraits.h"
#include "Core/PCGExMTCommon.h"
#include "Metadata/PCGMetadataCommon.h"

struct FPCGExContext;
struct FPCGExCarryOverDetails;
class FPCGExPointIOMerger;
class UPCGBasePointData;

template <typename T>
class FPCGMetadataAttribute;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FFacade;
	class FPointIO;
	class FPointIOCollection;
}

namespace PCGExGraphs
{
	/**
	 * Assembles a single working vtx dataset from multiple cluster vtx sources, for use with
	 * FGraphPatcher's merged-sources mode. Two-phase, because the attribute merge is async:
	 *
	 *   FVtxMerger Merger;
	 *   for (VtxGroup) Merger.AddSource(VtxIO);              // registers a source
	 *   Merger.MergeAsync(Context, TaskManager, CarryOver);  // phase 1 (async); fixes source offsets
	 *   ... let the merge finish ...
	 *   Facade = Merger.Finalize(Context, OutputPin);        // phase 2
	 *   const int32 Off = Merger.GetOffset(SourceIndex);     // valid after MergeAsync
	 *
	 * The finalized facade reads the merged data In-side (broadcaster/buffer friendly) and owns a
	 * mutable duplicate as Out - the shape FGraphPatcher expects. The merged In data is kept alive
	 * by this object; keep it around for as long as the facade is in use.
	 */
	class PCGEXGRAPHS_API FVtxMerger : public TSharedFromThis<FVtxMerger>
	{
	public:
		/** Register a vtx source; returns its source index. */
		int32 AddSource(const TSharedPtr<PCGExData::FPointIO>& InVtxIO);

		/** This source's first point index in the merged output. Authoritative only after MergeAsync. */
		int32 GetOffset(const int32 SourceIndex) const { return Sources[SourceIndex].Offset; }

		/** Phase 1: merge all sources' points, attributes & tags into an internal scratch data (async). */
		void MergeAsync(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const FPCGExCarryOverDetails* InCarryOver);

		/** Phase 2 (after the async merge completes): working facade with merged In + duplicated mutable Out. */
		TSharedPtr<PCGExData::FFacade> Finalize(FPCGExContext* InContext, const FName InOutputPin);

	protected:
		struct FSource
		{
			TSharedPtr<PCGExData::FPointIO> VtxIO;
			int32 Offset = 0; // filled from the merger's authoritative write scope in MergeAsync
		};

		TArray<FSource> Sources;

		TSharedPtr<PCGExData::FPointIO> ScratchIO;
		TSharedPtr<PCGExData::FFacade> ScratchFacade;
		TSharedPtr<FPCGExPointIOMerger> Merger;
		TSharedPtr<PCGExData::FPointIO> WorkingIO;
	};

	/**
	 * Merges input edge groups by CONNECTIVITY: groups linked (via AddEdge, or through an appended
	 * bridge vtx) share one output edge collection; unlinked groups stay separate. Yields one output
	 * cluster per connected component, all sharing the single vtx, by patching the PCGEx endpoint
	 * attributes (Attr_PCGExVtxIdx / Attr_PCGExEdgeIdx) directly -- no GraphBuilder recompile.
	 *
	 * Two-phase, because the edge merge is async and components are only known once edges are staged:
	 *
	 *   FGraphPatcher Patcher(VtxFacade);                       // vtx must be writable (e.g. Duplicate)
	 *   for (Cluster) Patcher.AddEdgeGroup(Cluster.EdgesIO, Cluster.VtxPointIndices);
	 *   const int32 M = Patcher.AddVtx(T, InheritFrom);         // optional new vtx
	 *   Patcher.AddEdge(A, B);                                  // link two vtx (existing and/or new)
	 *   Patcher.ResolveAndMergeAsync(OutEdges, TaskManager, CarryOver);   // phase 1 (async)
	 *   ... let the merges finish (e.g. between a batch's CompleteWork and Write) ...
	 *   Patcher.Commit();                                       // phase 2: patch staged edges + grow vtx
	 *
	 * Neither phase is re-entrant, and neither may run concurrently on one patcher. In merged-sources
	 * mode Commit fans its per-source and per-group work out over ParallelFor; with no registered vtx
	 * source it is entirely single-threaded. New-vtx domain data beyond the transform and whatever it
	 * inherits is the caller's to fill after Commit, via the index returned by AddVtx.
	 *
	 * Staged EDGES inherit nothing, by design: an appended edge has no single source edge to copy
	 * from. Only staged vtx inherit, from the source AddVtx names.
	 *
	 * Merged-sources mode: when the shared vtx is a merge of several vtx datasets (see FVtxMerger),
	 * endpoint ids collide across sources - each dataset numbered its own ids from 0 at compile time.
	 * Register each source with AddVtxSource and tag every edge group with its owning source index;
	 * Commit then renumbers Attr_PCGExVtxIdx to the merged point indices and rewrites each merged
	 * edge group's Attr_PCGExEdgeIdx accordingly (which the edge carry-over must preserve).
	 * With no registered vtx source, ids are trusted as-is (single-dataset behavior, unchanged).
	 */
	class PCGEXGRAPHS_API FGraphPatcher : public TSharedFromThis<FGraphPatcher>
	{
	public:
		explicit FGraphPatcher(const TSharedRef<PCGExData::FFacade>& InVtxFacade);

		/**
		 * Merged-sources mode: declare a vtx dataset occupying [InOffset, InOffset + count) in the
		 * shared vtx. Its In-side Attr_PCGExVtxIdx provides the old endpoint ids. Returns the source
		 * index to pass to AddEdgeGroup.
		 */
		int32 AddVtxSource(const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const int32 InOffset);

		/**
		 * Register an input edge group with the vtx point indices it owns. Returns the group index.
		 * In merged-sources mode, InVtxSourceIndex must identify the source the group's endpoints
		 * (and InVtxPointIndices) belong to; indices are in merged (offset) space.
		 */
		int32 AddEdgeGroup(const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TArray<int32>& InVtxPointIndices, const int32 InVtxSourceIndex = INDEX_NONE);

		/**
		 * Stage a new vtx at InTransform; returns the index it will occupy in the shared vtx.
		 * InInheritFromVtxPointIndex is a vtx POINT index (never a cluster node index) whose metadata
		 * attributes the new vtx copies at Commit, minus SetInheritExclusions. Naming a staged vtx
		 * resolves to ITS source, so callers must stage parents before their children.
		 */
		int32 AddVtx(const FTransform& InTransform, const int32 InInheritFromVtxPointIndex = INDEX_NONE);

		/** Attributes a staged vtx must NOT inherit, because the caller writes them itself after Commit. */
		void SetInheritExclusions(const TSet<FName>& InAttributeNames);

		/**
		 * Native point properties a staged vtx copies from its inherit source, alongside the metadata
		 * attributes (a separate system: this is the point struct, not the attribute table). Transform
		 * and MetadataEntry are dropped - the patcher owns both. Must be set before Commit, which
		 * allocates them as part of the grow. Default: none.
		 */
		void SetInheritedProperties(const EPCGPointNativeProperties InProperties);

		/** Stage an edge between two vtx point indices; returns a handle usable with GetEdgeOutput after Commit. */
		int32 AddEdge(const int32 VtxPointIndexA, const int32 VtxPointIndexB);

		/**
		 * Retarget an INPUT edge's endpoints at Commit: the merged copy of the group's edge at
		 * EdgeLocalIndex (its row in the group's source edge IO) gets its endpoint ids rewritten to
		 * (NewStartVtx, NewEndVtx), its point moved to their midpoint, and the touched vtx adjacency
		 * counts adjusted. Old endpoints must be the edge's current vtx point indices; new vtx may be
		 * initial or staged, and are unioned into the group's component. Built for edge SPLITS
		 * (retarget one end to a staged vtx + AddEdge the replacement half) -- it does not
		 * re-partition a group whose internal connectivity a retarget would sever.
		 * Returns a handle usable with GetRetargetOutput after ResolveAndMergeAsync.
		 */
		int32 RetargetEdge(
			const int32 GroupIndex, const int32 EdgeLocalIndex,
			const int32 OldStartVtx, const int32 OldEndVtx,
			const int32 NewStartVtx, const int32 NewEndVtx);

		/** Resolve a retarget handle to the retargeted edge's output IO + point index (valid after
		 *  ResolveAndMergeAsync). After Commit, a retarget that was dropped resolves false -- the
		 *  row's endpoints were never rewritten, so callers must not treat it as retargeted. */
		bool GetRetargetOutput(const int32 RetargetHandle, TSharedPtr<PCGExData::FPointIO>& OutEdgesIO, int32& OutEdgePointIndex) const;

		/**
		 * Phase 1: emit one merged edge IO per connected component into OutEdges (async on TaskManager).
		 * InCarryOver must be valid and should carry the edge attributes, incl. Attr_PCGExEdgeIdx.
		 */
		void ResolveAndMergeAsync(
			const TSharedRef<PCGExData::FPointIOCollection>& OutEdges,
			const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
			const FPCGExCarryOverDetails* InCarryOver);

		/**
		 * Phase 2 (after the async merges complete): append + patch staged edges, grow the shared vtx.
		 * False means nothing was appended (the shared vtx is not compiled cluster data), so the
		 * indices AddVtx handed out do not exist and callers must not write at them.
		 */
		bool Commit();

		/** The merged edge IOs, one per connected component (valid after ResolveAndMergeAsync). */
		const TArray<TSharedPtr<PCGExData::FPointIO>>& GetOutputEdges() const { return ComponentEdgeIOs; }

		/** Resolve a staged-edge handle to its output edge IO + point index (valid after Commit). */
		bool GetEdgeOutput(const int32 EdgeHandle, TSharedPtr<PCGExData::FPointIO>& OutEdgesIO, int32& OutEdgePointIndex) const;

	protected:
		TSharedRef<PCGExData::FFacade> VtxFacade;
		int32 NumInitialVtx = 0;

		struct FEdgeGroup
		{
			TSharedPtr<PCGExData::FPointIO> EdgesIO;
			TArray<int32> VtxPointIndices;
			int32 VtxSourceIndex = INDEX_NONE;      // merged-sources mode: owning vtx source
			int32 ComponentIndex = INDEX_NONE;      // filled during ResolveAndMergeAsync
			PCGExMT::FScope OutWriteScope;          // this group's range in its component's merged edges
		};

		struct FVtxSource
		{
			TSharedPtr<PCGExData::FPointIO> VtxIO;
			int32 Offset = 0;
		};

		TArray<FVtxSource> VtxSources;

		struct FPendingEdge
		{
			int32 A = -1;
			int32 B = -1;
			int32 ComponentIndex = -1; // filled during ResolveAndMergeAsync
			int32 EdgePointIndex = -1; // filled during Commit
		};

		struct FEdgeRetarget
		{
			int32 GroupIndex = -1;
			int32 EdgeLocalIndex = -1;
			int32 OldStart = -1;
			int32 OldEnd = -1;
			int32 NewStart = -1;
			int32 NewEnd = -1;
			bool bApplied = false; // set by ApplyRetargets; a dropped retarget must not resolve
		};

		TArray<FEdgeGroup> Groups;
		TArray<FTransform> NewVtxTransforms;
		TArray<int32> NewVtxInheritFrom; // parallel to NewVtxTransforms; AddVtx-resolved initial vtx point index, or INDEX_NONE
		TArray<FPendingEdge> PendingEdges;
		TArray<FEdgeRetarget> Retargets;

		TSet<FName> InheritExclusions;
		EPCGPointNativeProperties InheritedProperties = EPCGPointNativeProperties::None;

		/** One component's staged edge ids. Ids/Dirty are indexed by point index MINUS Base. */
		struct FComponentEdges
		{
			UPCGBasePointData* Data = nullptr;
			FPCGMetadataAttribute<int64>* IdxAttr = nullptr;
			TArray<int64> Ids;
			TArray<bool> Dirty;
			int32 Base = 0;
			int32 FirstNewEdge = 0;
		};

		/**
		 * State threaded through Commit's phases. Endpoint ids stage here and flush once per attribute:
		 * FPCGMetadataAttribute::SetValue takes two exclusive locks and heap-allocates on every call.
		 * Dirty flags are TArray<bool> (one byte each), so the parallel fills need no synchronization.
		 */
		struct FCommitScratch
		{
			FPCGMetadataAttribute<int64>* VtxIdxAttr = nullptr;

			// Ids/Dirty are indexed by vtx point index MINUS VtxBase. Merged mode renumbers every vtx
			// (VtxBase 0); otherwise only staged vtx are written, so the array starts at NumInitialVtx.
			TArray<int64> VtxIds;
			TArray<bool> VtxIdsDirty;
			int32 VtxBase = 0;

			// Endpoint ids of initial vtx below VtxBase that a staged edge names, read back in one query.
			TMap<int32, uint32> ResolvedEndpointIds;

			// Keyed by entry key, not point index: the attribute holds one value per entry, so points
			// sharing an entry must accumulate into it rather than overwrite each other. Signed:
			// retargets subtract from the endpoint they leave.
			TMap<PCGMetadataEntryKey, int32> EdgeCountDelta;

			TArray<TMap<uint32, int32>> SourceLookups;
			TArray<bool> SourceValid;

			TArray<TArray<int32>> ComponentPendingEdges;
			TArray<FComponentEdges> Components;
		};

		void GrowAndStageVtx(FCommitScratch& Scratch);
		void RenumberMergedVtx(FCommitScratch& Scratch);
		void StageComponentEdges(FCommitScratch& Scratch);
		void RenumberMergedEdges(FCommitScratch& Scratch);
		void ResolveNamedEndpointIds(FCommitScratch& Scratch);
		void ApplyRetargets(FCommitScratch& Scratch);
		void AppendStagedEdges(FCommitScratch& Scratch);
		void FlushEndpointIds(FCommitScratch& Scratch);

		/** Endpoint id of a vtx: from the staging arrays where covered, else the batched attribute
		 *  read ResolveNamedEndpointIds performed. */
		uint32 EndpointIdFor(const FCommitScratch& Scratch, const int32 VtxPointIndex) const;

		/** Copy each staged vtx's inheritable attributes from the source AddVtx resolved for it. */
		void InheritStagedVtxData(UPCGBasePointData* InVtxData) const;

		// Union-find over elements [0, Groups.Num()) = groups, [Groups.Num(), +NewVtx) = staged vtx.
		TArray<int32> DSU;
		int32 Find(const int32 X);
		void DSUUnion(const int32 A, const int32 B);
		int32 VtxElement(const int32 VtxPointIndex) const;

		TMap<int32, int32> VtxToElement; // vtx point index -> union-find element

		TArray<TSharedPtr<PCGExData::FPointIO>> ComponentEdgeIOs;
		TArray<TSharedPtr<PCGExData::FFacade>> ComponentEdgeFacades;
		TArray<TSharedPtr<FPCGExPointIOMerger>> Mergers;

		bool bResolved = false;
		bool bCommitted = false;
		bool bCommitOk = false;
	};

	/** Which connector flags to write, and under what attribute names. Named fields so the vtx/edge
	 *  pair can't be transposed at a call site (a positional-arg swap would compile silently). */
	struct FConnectorFlagsConfig
	{
		bool bFlagVtx = false;
		FName VtxFlagName = NAME_None;
		bool bFlagEdge = false;
		FName EdgeFlagName = NAME_None;

		bool IsEnabled() const { return bFlagVtx || bFlagEdge; }
	};

	/**
	 * Optional connector-flag output shared by the bridge/probe cluster nodes: stamps a per-edge bool
	 * on each staged edge's output data and/or bumps a per-vtx int32 count on the shared vtx. Resolves
	 * each edge output's attribute + entry range once per distinct IO and accumulates vtx counts before
	 * flushing. No-op when both flags are off. Call after Patcher.Commit().
	 */
	PCGEXGRAPHS_API void WriteConnectorFlags(
		FGraphPatcher& InPatcher,
		const TSharedRef<PCGExData::FFacade>& InVtxFacade,
		const FConnectorFlagsConfig& InConfig,
		const TArray<int32>& InEdgeHandles,
		const TArray<uint64>& InEndpoints);
}
