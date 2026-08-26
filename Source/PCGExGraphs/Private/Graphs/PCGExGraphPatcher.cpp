// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExGraphPatcher.h"

#include "PCGExH.h"
#include "PCGExLog.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Data/Buffers/PCGExBuffer.h"
#include "Graphs/PCGExGraphHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Utils/PCGExPointIOMerger.h"

namespace PCGExGraphs
{
#pragma region FVtxMerger

	int32 FVtxMerger::AddSource(const TSharedPtr<PCGExData::FPointIO>& InVtxIO)
	{
		const int32 SourceIndex = Sources.Num();
		FSource& Source = Sources.Emplace_GetRef();
		Source.VtxIO = InVtxIO;
		return SourceIndex;
	}

	void FVtxMerger::MergeAsync(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const FPCGExCarryOverDetails* InCarryOver)
	{
		ScratchIO = PCGExData::NewPointIO(InContext);
		if (!ScratchIO->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			ScratchIO.Reset();
			return;
		}

		ScratchFacade = MakeShared<PCGExData::FFacade>(ScratchIO.ToSharedRef());

		Merger = MakeShared<FPCGExPointIOMerger>(ScratchFacade.ToSharedRef());
		for (FSource& Source : Sources)
		{
			// Take the offset from the merger's authoritative write scope rather than a parallel ledger,
			// so it stays correct even if the merger ever skips/clamps/reorders a source.
			Source.Offset = Merger->Append(Source.VtxIO).Write.Start;
		}

		Merger->MergeAsync(InTaskManager, InCarryOver, nullptr, true);
	}

	TSharedPtr<PCGExData::FFacade> FVtxMerger::Finalize(FPCGExContext* InContext, const FName InOutputPin)
	{
		if (!ScratchIO)
		{
			return nullptr;
		}

		const UPCGBasePointData* Merged = ScratchIO->GetOut();
		if (!Merged || Merged->IsEmpty())
		{
			return nullptr;
		}

		// The merged data becomes the working In (broadcaster-readable); Out is a mutable duplicate.
		// ScratchIO stays alive on this object so the managed In data survives until execution ends.
		WorkingIO = PCGExData::NewPointIO(InContext, Merged, InOutputPin);
		WorkingIO->Tags->Append(ScratchIO->Tags.ToSharedRef());

		if (!WorkingIO->InitializeOutput(PCGExData::EIOInit::Duplicate))
		{
			return nullptr;
		}

		return MakeShared<PCGExData::FFacade>(WorkingIO.ToSharedRef());
	}

#pragma endregion

#pragma region FGraphPatcher

	FGraphPatcher::FGraphPatcher(const TSharedRef<PCGExData::FFacade>& InVtxFacade)
		: VtxFacade(InVtxFacade)
	{
		NumInitialVtx = VtxFacade->GetOut()->GetNumPoints();
	}

	int32 FGraphPatcher::AddVtxSource(const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const int32 InOffset)
	{
		const int32 SourceIndex = VtxSources.Num();
		FVtxSource& Source = VtxSources.Emplace_GetRef();
		Source.VtxIO = InVtxIO;
		Source.Offset = InOffset;
		return SourceIndex;
	}

	int32 FGraphPatcher::AddEdgeGroup(const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TArray<int32>& InVtxPointIndices, const int32 InVtxSourceIndex)
	{
		const int32 GroupIndex = Groups.Num();
		FEdgeGroup& Group = Groups.Emplace_GetRef();
		Group.EdgesIO = InEdgesIO;
		Group.VtxPointIndices = InVtxPointIndices;
		Group.VtxSourceIndex = InVtxSourceIndex;
		return GroupIndex;
	}

	int32 FGraphPatcher::AddVtx(const FTransform& InTransform, const int32 InInheritFromVtxPointIndex)
	{
		// Staging after ResolveAndMergeAsync yields a vtx with no union-find element, so no component.
		check(!bResolved);

		const int32 Index = NumInitialVtx + NewVtxTransforms.Num();
		NewVtxTransforms.Add(InTransform);

		checkf(
			InInheritFromVtxPointIndex == INDEX_NONE || (InInheritFromVtxPointIndex >= 0 && InInheritFromVtxPointIndex < Index),
			TEXT("Graph patcher: vtx %d inherits from %d - expected INDEX_NONE or a vtx point index below %d."),
			Index, InInheritFromVtxPointIndex, Index);

		// Resolve to an initial vtx now, while the array still ends at the previous staged vtx: a staged
		// source is necessarily earlier and already resolved, so one hop suffices and a forward or self
		// reference fails IsValidIndex here rather than forming a cycle Commit would have to defend against.
		int32 Source = InInheritFromVtxPointIndex;
		if (Source >= NumInitialVtx)
		{
			const int32 StagedIndex = Source - NumInitialVtx;
			Source = NewVtxInheritFrom.IsValidIndex(StagedIndex) ? NewVtxInheritFrom[StagedIndex] : INDEX_NONE;
		}
		NewVtxInheritFrom.Add(Source);

		return Index;
	}

	void FGraphPatcher::SetInheritExclusions(const TSet<FName>& InAttributeNames)
	{
		InheritExclusions = InAttributeNames;
	}

	void FGraphPatcher::SetInheritedProperties(const EPCGPointNativeProperties InProperties)
	{
		InheritedProperties = InProperties;
		EnumRemoveFlags(InheritedProperties, EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::MetadataEntry);
	}

	int32 FGraphPatcher::AddEdge(const int32 VtxPointIndexA, const int32 VtxPointIndexB)
	{
		const int32 Handle = PendingEdges.Num();
		PendingEdges.Add(FPendingEdge{VtxPointIndexA, VtxPointIndexB});
		return Handle;
	}

	int32 FGraphPatcher::RetargetEdge(
		const int32 GroupIndex, const int32 EdgeLocalIndex,
		const int32 OldStartVtx, const int32 OldEndVtx,
		const int32 NewStartVtx, const int32 NewEndVtx)
	{
		// Same window as AddVtx/AddEdge: components are decided in ResolveAndMergeAsync.
		check(!bResolved);
		const int32 Handle = Retargets.Num();
		Retargets.Add(FEdgeRetarget{GroupIndex, EdgeLocalIndex, OldStartVtx, OldEndVtx, NewStartVtx, NewEndVtx});
		return Handle;
	}

	bool FGraphPatcher::GetRetargetOutput(const int32 RetargetHandle, TSharedPtr<PCGExData::FPointIO>& OutEdgesIO, int32& OutEdgePointIndex) const
	{
		if (!bResolved || !Retargets.IsValidIndex(RetargetHandle))
		{
			return false;
		}

		const FEdgeRetarget& R = Retargets[RetargetHandle];
		if (bCommitted && !R.bApplied)
		{
			return false;
		}
		if (!Groups.IsValidIndex(R.GroupIndex))
		{
			return false;
		}

		const FEdgeGroup& Group = Groups[R.GroupIndex];
		if (Group.ComponentIndex == INDEX_NONE || !ComponentEdgeIOs.IsValidIndex(Group.ComponentIndex) ||
			R.EdgeLocalIndex < 0 || R.EdgeLocalIndex >= Group.OutWriteScope.Count)
		{
			return false;
		}

		OutEdgesIO = ComponentEdgeIOs[Group.ComponentIndex];
		OutEdgePointIndex = Group.OutWriteScope.Start + R.EdgeLocalIndex;
		return true;
	}

	int32 FGraphPatcher::Find(const int32 X)
	{
		int32 Root = X;
		while (DSU[Root] != Root)
		{
			Root = DSU[Root];
		}
		int32 Cur = X;
		while (DSU[Cur] != Root)
		{
			const int32 Next = DSU[Cur];
			DSU[Cur] = Root;
			Cur = Next;
		}
		return Root;
	}

	void FGraphPatcher::DSUUnion(const int32 A, const int32 B)
	{
		const int32 RA = Find(A);
		const int32 RB = Find(B);
		if (RA != RB)
		{
			DSU[RA] = RB;
		}
	}

	int32 FGraphPatcher::VtxElement(const int32 VtxPointIndex) const
	{
		const int32* E = VtxToElement.Find(VtxPointIndex);
		return E ? *E : INDEX_NONE;
	}

	void FGraphPatcher::ResolveAndMergeAsync(
		const TSharedRef<PCGExData::FPointIOCollection>& OutEdges,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const FPCGExCarryOverDetails* InCarryOver)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::ResolveAndMergeAsync);
		
		if (bResolved)
		{
			return;
		}
		bResolved = true;

		const int32 G = Groups.Num();
		const int32 B = NewVtxTransforms.Num();
		if (G == 0)
		{
			// No edge groups means no component to anchor staged edges to. Staged edges must reference
			// registered group vtx, so this is only reachable on misuse.
			check(PendingEdges.IsEmpty());
			return;
		}

		// vtx point index -> union-find element (groups first, then staged vtx). First group to claim a
		// vtx owns its mapping; a vtx shared by two groups physically connects them, so we record the pair
		// and union their components once the DSU exists (below) instead of asserting on the collision.
		VtxToElement.Reserve(NumInitialVtx + B);
		TArray<TPair<int32, int32>> SharedVtxUnions;
		for (int32 g = 0; g < G; ++g)
		{
			for (const int32 P : Groups[g].VtxPointIndices)
			{
				if (const int32* Existing = VtxToElement.Find(P))
				{
					SharedVtxUnions.Emplace(*Existing, g);
				}
				else
				{
					VtxToElement.Add(P, g);
				}
			}
		}
		for (int32 i = 0; i < B; ++i)
		{
			VtxToElement.Add(NumInitialVtx + i, G + i);
		}

		// union groups linked by a shared vtx, then by staged edges (directly or through a staged vtx)
		DSU.SetNumUninitialized(G + B);
		for (int32 i = 0; i < DSU.Num(); ++i)
		{
			DSU[i] = i;
		}
		for (const TPair<int32, int32>& U : SharedVtxUnions)
		{
			DSUUnion(U.Key, U.Value);
		}
		for (const FPendingEdge& E : PendingEdges)
		{
			const int32 EA = VtxElement(E.A);
			const int32 EB = VtxElement(E.B);
			if (EA != INDEX_NONE && EB != INDEX_NONE)
			{
				DSUUnion(EA, EB);
			}
		}
		// A retargeted edge physically links its new endpoints into its group's component.
		for (const FEdgeRetarget& R : Retargets)
		{
			if (!Groups.IsValidIndex(R.GroupIndex))
			{
				continue;
			}
			for (const int32 V : {R.NewStart, R.NewEnd})
			{
				const int32 EV = VtxElement(V);
				if (EV != INDEX_NONE)
				{
					DSUUnion(R.GroupIndex, EV);
				}
			}
		}

		// one output edge collection per connected component (keyed by group-root)
		TMap<int32, int32> RootToComponent;
		RootToComponent.Reserve(G);
		auto GetComponent = [&](const int32 Element) -> int32
		{
			const int32 Root = Find(Element);
			if (const int32* C = RootToComponent.Find(Root))
			{
				return *C;
			}
			const int32 CompIdx = ComponentEdgeIOs.Num();
			RootToComponent.Add(Root, CompIdx);

			const TSharedPtr<PCGExData::FPointIO> IO = OutEdges->Emplace_GetRef<UPCGExClusterEdgesData>(PCGExData::EIOInit::New);
			const TSharedRef<PCGExData::FFacade> Facade = MakeShared<PCGExData::FFacade>(IO.ToSharedRef());

			ComponentEdgeIOs.Add(IO);
			ComponentEdgeFacades.Add(Facade);
			Mergers.Add(MakeShared<FPCGExPointIOMerger>(Facade));
			return CompIdx;
		};

		// route each input group's edges to its component's merger (merger carries the source tags over)
		for (int32 g = 0; g < G; ++g)
		{
			// Merged-sources mode: a group without a source association would keep stale endpoint ids.
			check(VtxSources.IsEmpty() || Groups[g].VtxSourceIndex != INDEX_NONE);

			const int32 CompIdx = GetComponent(g);
			Groups[g].ComponentIndex = CompIdx;
			Groups[g].OutWriteScope = Mergers[CompIdx]->Append(Groups[g].EdgesIO).Write;
		}

		// route each staged edge to its component
		for (FPendingEdge& E : PendingEdges)
		{
			const int32 Elem = VtxElement(E.A);
			if (Elem == INDEX_NONE)
			{
				continue;
			}
			E.ComponentIndex = RootToComponent[Find(Elem)];
		}

		// vtx/edge pairing happens in Commit(), which must run after these async merges (see Commit).
		for (const TSharedPtr<FPCGExPointIOMerger>& Merger : Mergers)
		{
			Merger->MergeAsync(InTaskManager, InCarryOver, nullptr, true);
		}
	}

	bool FGraphPatcher::Commit()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::Commit);

		if (bCommitted)
		{
			return bCommitOk;
		}
		bCommitted = true;

		FCommitScratch Scratch;
		Scratch.VtxIdxAttr = VtxFacade->GetOut()->MutableMetadata()->FindOrCreateAttribute<int64>(PCGExClusters::Labels::Attr_PCGExVtxIdx);
		if (!Scratch.VtxIdxAttr)
		{
			return false;
		} // no usable metadata domain; nothing is appended, so staged indices never come to exist

		// Merged mode renumbers every initial vtx; otherwise only staged vtx are ever written, so the
		// staging arrays start past them.
		Scratch.VtxBase = VtxSources.IsEmpty() ? NumInitialVtx : 0;

		GrowAndStageVtx(Scratch);
		RenumberMergedVtx(Scratch);
		StageComponentEdges(Scratch);
		RenumberMergedEdges(Scratch);
		ResolveNamedEndpointIds(Scratch);
		ApplyRetargets(Scratch); // after RenumberMergedEdges, so a retarget overrides its row's renumbered pair
		AppendStagedEdges(Scratch);
		FlushEndpointIds(Scratch);

		// Pair vtx <-> edges LAST: the async merges Append the source edges' old cluster tags onto each
		// output, which would clobber an earlier pairing. Derive the PairId from the vtx, then mark both.
		const PCGExDataId PairId = VtxFacade->Source->Tags->Set<int64>(
			PCGExClusters::Labels::TagStr_PCGExCluster, VtxFacade->Source->GetOutIn()->GetUniqueID());
		PCGExClusters::Helpers::MarkClusterVtx(VtxFacade->Source, PairId);
		for (const TSharedPtr<PCGExData::FPointIO>& IO : ComponentEdgeIOs)
		{
			PCGExClusters::Helpers::MarkClusterEdges(IO, PairId);
		}

		bCommitOk = true;
		return true;
	}

	void FGraphPatcher::GrowAndStageVtx(FCommitScratch& Scratch)
	{
		UPCGBasePointData* VtxData = VtxFacade->GetOut();

		const int32 NumNewVtx = NewVtxTransforms.Num();
		const int32 NumVtx = NumInitialVtx + NumNewVtx;

		Scratch.VtxIds.SetNumZeroed(NumVtx - Scratch.VtxBase);
		Scratch.VtxIdsDirty.Init(false, NumVtx - Scratch.VtxBase);

		if (NumNewVtx == 0)
		{
			return;
		}

		// InheritedProperties joins the grow: the self-copy in InheritStagedVtxData captures its read
		// ranges before allocating, so everything it touches has to be allocated by the time it runs.
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			VtxData, NumVtx,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin |
			EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | InheritedProperties);

		// The grow invalidates the Out key snapshot FPointIO caches; In-side keys are unaffected.
		VtxFacade->Source->ClearCachedOutKeys();

		TPCGValueRange<int64> NewEntries = VtxData->GetMetadataEntryValueRange();
		TPCGValueRange<FTransform> NewTransforms = VtxData->GetTransformValueRange(false);

		// Same predicate UPCGMetadata::InitializeOnSet applies per key, hoisted so the whole batch takes
		// the metadata domain's item lock once instead of once per staged vtx.
		const int64 ItemKeyOffset = VtxData->Metadata->GetItemKeyCountForParent();
		TArray<int64*> EntriesNeedingInit;
		EntriesNeedingInit.Reserve(NumNewVtx);

		for (int32 i = 0; i < NumNewVtx; ++i)
		{
			const int32 P = NumInitialVtx + i;
			NewTransforms[P] = NewVtxTransforms[i];

			int64& Entry = NewEntries[P];
			if (Entry == PCGInvalidEntryKey || Entry < ItemKeyOffset)
			{
				EntriesNeedingInit.Add(&Entry);
			}
		}

		if (!EntriesNeedingInit.IsEmpty())
		{
			VtxData->Metadata->AddEntriesInPlace(EntriesNeedingInit);
		}

		// Inheriting copies the source's Attr_PCGExVtxIdx onto the staged vtx; the flush overwrites it,
		// because the id a staged vtx ships with is the patcher's to decide, not the source's.
		InheritStagedVtxData(VtxData);

		for (int32 i = 0; i < NumNewVtx; ++i)
		{
			const int32 P = NumInitialVtx + i;
			// Endpoint id of a new vtx is its own point index; edge counts land in EdgeCountDelta.
			Scratch.VtxIds[P - Scratch.VtxBase] = static_cast<int64>(PCGEx::H64(static_cast<uint32>(P), 0));
			Scratch.VtxIdsDirty[P - Scratch.VtxBase] = true;
		}
	}

	void FGraphPatcher::RenumberMergedVtx(FCommitScratch& Scratch)
	{
		const int32 NumVtxSources = VtxSources.Num();
		if (NumVtxSources == 0)
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::Commit::RenumberVtx);

		// Per-source endpoint ids collide once datasets are merged (each source numbered its vtx from 0).
		// Renumber every initial vtx to H64(mergedPointIndex, adjacency), which is globally unique. Old
		// ids are read from the SOURCE vtx data (authoritative), never from the merged output whose copy
		// the carry-over may have stripped.
		Scratch.SourceLookups.SetNum(NumVtxSources);
		Scratch.SourceValid.Init(true, NumVtxSources);

		// FPointIO::GetInKeys builds lazily under a write lock; warm every source up front or all but
		// the first of the lookups below spin on it.
		for (const FVtxSource& Source : VtxSources)
		{
			(void)Source.VtxIO->GetInKeys();
		}

		PCGExMT::ParallelOrSequential(
			NumVtxSources,
			[&](const int32 s)
			{
				const int32 Offset = VtxSources[s].Offset;
				const int32 Count = VtxSources[s].VtxIO->GetNum();

				TArray<int32> Adjacency;
				const bool bLookupOk = PCGExGraphs::Helpers::BuildEndpointsLookup(VtxSources[s].VtxIO, Scratch.SourceLookups[s], Adjacency);
				Scratch.SourceValid[s] = bLookupOk;

				// Always assign each merged vtx an id equal to its point index (unique across the whole
				// merged set) so a single source's read failure can never make ids collide across sources.
				for (int32 Local = 0; Local < Count; ++Local)
				{
					const int32 P = Offset + Local;
					const uint32 Adj = (bLookupOk && Adjacency.IsValidIndex(Local)) ? static_cast<uint32>(Adjacency[Local]) : 0;
					Scratch.VtxIds[P] = static_cast<int64>(PCGEx::H64(static_cast<uint32>(P), Adj));
					Scratch.VtxIdsDirty[P] = true;
				}
			}, /*Threshold=*/2, EParallelForFlags::Unbalanced);
	}

	void FGraphPatcher::StageComponentEdges(FCommitScratch& Scratch)
	{
		const int32 NumComponents = ComponentEdgeIOs.Num();
		const int32 NumVtx = NumInitialVtx + NewVtxTransforms.Num();

		Scratch.ComponentPendingEdges.SetNum(NumComponents);

		// Endpoints are range-checked once, here, so every later index into the vtx staging arrays is
		// known good; AddEdge cannot do it because staged vtx may still be added after it runs.
		bool bDroppedEdges = false;
		for (int32 h = 0; h < PendingEdges.Num(); ++h)
		{
			const FPendingEdge& E = PendingEdges[h];
			if (!Scratch.ComponentPendingEdges.IsValidIndex(E.ComponentIndex))
			{
				continue;
			}

			if (E.A < 0 || E.A >= NumVtx || E.B < 0 || E.B >= NumVtx)
			{
				bDroppedEdges = true;
				continue;
			}

			Scratch.ComponentPendingEdges[E.ComponentIndex].Add(h);
		}

		UE_CLOG(bDroppedEdges, LogPCGEx, Warning, TEXT("Graph patcher: staged edges naming a vtx outside the shared vtx were dropped."));

		// Which components the merged pass will rewrite; only components some pass actually writes get
		// their endpoint attribute created.
		TArray<bool> HasMergedWrite;
		HasMergedWrite.Init(false, NumComponents);
		if (!VtxSources.IsEmpty())
		{
			for (const FEdgeGroup& Group : Groups)
			{
				if (Group.VtxSourceIndex != INDEX_NONE && Group.OutWriteScope.Count > 0 && HasMergedWrite.IsValidIndex(Group.ComponentIndex))
				{
					HasMergedWrite[Group.ComponentIndex] = true;
				}
			}
		}

		// Retargets write below FirstNewEdge, so their component's staging arrays must cover from 0
		// even without a merged pass (untouched rows stay non-dirty and are never flushed).
		TArray<bool> HasRetarget;
		HasRetarget.Init(false, NumComponents);
		for (const FEdgeRetarget& R : Retargets)
		{
			if (Groups.IsValidIndex(R.GroupIndex) && HasRetarget.IsValidIndex(Groups[R.GroupIndex].ComponentIndex))
			{
				HasRetarget[Groups[R.GroupIndex].ComponentIndex] = true;
			}
		}

		Scratch.Components.SetNum(NumComponents);

		for (int32 c = 0; c < NumComponents; ++c)
		{
			const int32 NumNewEdges = Scratch.ComponentPendingEdges[c].Num();
			if (NumNewEdges == 0 && !HasMergedWrite[c] && !HasRetarget[c])
			{
				continue;
			}

			FComponentEdges& Comp = Scratch.Components[c];
			Comp.Data = ComponentEdgeFacades[c]->GetOut();
			Comp.IdxAttr = Comp.Data->MutableMetadata()->FindOrCreateAttribute<int64>(PCGExClusters::Labels::Attr_PCGExEdgeIdx);
			if (!Comp.IdxAttr)
			{
				Comp.Data = nullptr;
				continue;
			}

			Comp.FirstNewEdge = Comp.Data->GetNumPoints();

			if (NumNewEdges > 0)
			{
				Comp.Data->SetNumPoints(Comp.FirstNewEdge + NumNewEdges);
				Comp.Data->AllocateProperties(EPCGPointNativeProperties::Transform);
				ComponentEdgeFacades[c]->Source->ClearCachedOutKeys();

				TPCGValueRange<int64> EdgeEntries = Comp.Data->GetMetadataEntryValueRange();
				const int64 ItemKeyOffset = Comp.Data->Metadata->GetItemKeyCountForParent();

				TArray<int64*> EntriesNeedingInit;
				EntriesNeedingInit.Reserve(NumNewEdges);

				for (int32 i = 0; i < NumNewEdges; ++i)
				{
					int64& Entry = EdgeEntries[Comp.FirstNewEdge + i];
					if (Entry == PCGInvalidEntryKey || Entry < ItemKeyOffset)
					{
						EntriesNeedingInit.Add(&Entry);
					}
				}

				if (!EntriesNeedingInit.IsEmpty())
				{
					Comp.Data->Metadata->AddEntriesInPlace(EntriesNeedingInit);
				}
			}

			// The merged pass (and any retarget) rewrites pre-existing edges; without either, only the
			// appended tail is written, so the staging arrays need cover nothing below it.
			Comp.Base = (HasMergedWrite[c] || HasRetarget[c]) ? 0 : Comp.FirstNewEdge;

			const int32 NumStaged = Comp.Data->GetNumPoints() - Comp.Base;
			Comp.Ids.SetNumZeroed(NumStaged);
			Comp.Dirty.Init(false, NumStaged);
		}
	}

	void FGraphPatcher::RenumberMergedEdges(FCommitScratch& Scratch)
	{
		if (VtxSources.IsEmpty())
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::Commit::RenumberEdges);

		// An endpoint that cannot be resolved gets this sentinel id - no merged vtx owns it, so the
		// edge fails visibly downstream (Sanitize Cluster) instead of silently aliasing a real vtx.
		constexpr uint32 InvalidEndpoint = TNumericLimits<uint32>::Max();

		TArray<bool> GroupUnresolved;
		GroupUnresolved.Init(false, Groups.Num());

		for (const FEdgeGroup& Group : Groups)
		{
			if (Group.EdgesIO) { (void)Group.EdgesIO->GetInKeys(); }
		}

		// Parallel across groups: the merger hands every group a disjoint write scope, so two groups
		// sharing a component still touch disjoint elements of its staging arrays.
		PCGExMT::ParallelOrSequential(
			Groups.Num(),
			[&](const int32 g)
			{
				const FEdgeGroup& Group = Groups[g];
				if (!Group.EdgesIO || Group.VtxSourceIndex == INDEX_NONE || Group.ComponentIndex == INDEX_NONE || Group.OutWriteScope.Count <= 0)
				{
					return;
				}

				FComponentEdges& Comp = Scratch.Components[Group.ComponentIndex];
				if (!Comp.IdxAttr)
				{
					return;
				}

				const TMap<uint32, int32>& Lookup = Scratch.SourceLookups[Group.VtxSourceIndex];
				const int32 Offset = VtxSources[Group.VtxSourceIndex].Offset;

				// Read the old (pre-merge) endpoints straight from the source edge IO - the merged output's
				// own copy may have been stripped, which would decode to a bogus (0,0) self-pair.
				const TUniquePtr<PCGExData::TArrayBuffer<int64>> SrcEdgeIds = MakeUnique<PCGExData::TArrayBuffer<int64>>(Group.EdgesIO.ToSharedRef(), PCGExClusters::Labels::Attr_PCGExEdgeIdx);
				const bool bEdgeReadOk = Scratch.SourceValid[Group.VtxSourceIndex] && SrcEdgeIds->InitForRead();
				const TArray<int64>* OldEdgeValues = bEdgeReadOk ? SrcEdgeIds->GetInValues().Get() : nullptr;

				bool bUnresolved = false;

				for (int32 i = 0; i < Group.OutWriteScope.Count; ++i)
				{
					const int32 P = Group.OutWriteScope.Start + i;

					uint32 NewA = InvalidEndpoint;
					uint32 NewB = InvalidEndpoint;

					if (OldEdgeValues && OldEdgeValues->IsValidIndex(i))
					{
						uint32 OldA;
						uint32 OldB;
						PCGEx::H64(static_cast<uint64>((*OldEdgeValues)[i]), OldA, OldB);

						if (const int32* LA = Lookup.Find(OldA)) { NewA = static_cast<uint32>(Offset + *LA); }
						if (const int32* LB = Lookup.Find(OldB)) { NewB = static_cast<uint32>(Offset + *LB); }
					}

					if (NewA == InvalidEndpoint || NewB == InvalidEndpoint) { bUnresolved = true; }

					Comp.Ids[P - Comp.Base] = static_cast<int64>(PCGEx::H64(NewA, NewB));
					Comp.Dirty[P - Comp.Base] = true;
				}

				GroupUnresolved[g] = bUnresolved;
			}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		UE_CLOG(GroupUnresolved.Contains(true), LogPCGEx, Warning, TEXT("Graph patcher: some merged edge endpoints could not be resolved and were marked invalid; input vtx/edges pairing is corrupt (run Cluster : Repair)."));
	}

	void FGraphPatcher::ResolveNamedEndpointIds(FCommitScratch& Scratch)
	{
		// Endpoint ids of vtx the staging arrays don't cover come from the attribute in one batched
		// read -- staged edges AND retargets both name such vtx, hence the shared pass.
		const int32 NumVtx = NumInitialVtx + NewVtxTransforms.Num();
		TArray<int32> ToRead;

		auto Gather = [&](const int32 V)
		{
			if (V >= 0 && V < Scratch.VtxBase && !Scratch.ResolvedEndpointIds.Contains(V))
			{
				Scratch.ResolvedEndpointIds.Add(V, 0);
				ToRead.Add(V);
			}
		};

		for (const TArray<int32>& Handles : Scratch.ComponentPendingEdges)
		{
			for (const int32 Handle : Handles)
			{
				Gather(PendingEdges[Handle].A);
				Gather(PendingEdges[Handle].B);
			}
		}
		for (const FEdgeRetarget& R : Retargets)
		{
			if (R.NewStart < NumVtx) { Gather(R.NewStart); }
			if (R.NewEnd < NumVtx) { Gather(R.NewEnd); }
		}

		if (ToRead.IsEmpty())
		{
			return;
		}

		const TConstPCGValueRange<int64> VtxEntries = VtxFacade->GetOut()->GetConstMetadataEntryValueRange();

		TArray<PCGMetadataEntryKey> Keys;
		Keys.SetNumUninitialized(ToRead.Num());
		for (int32 i = 0; i < ToRead.Num(); ++i) { Keys[i] = VtxEntries[ToRead[i]]; }

		TArray<int64> Values;
		Values.SetNumZeroed(ToRead.Num());
		Scratch.VtxIdxAttr->GetValuesFromItemKeys(TConstArrayView<PCGMetadataEntryKey>(Keys), TArrayView<int64>(Values));

		for (int32 i = 0; i < ToRead.Num(); ++i)
		{
			Scratch.ResolvedEndpointIds[ToRead[i]] = PCGEx::H64A(static_cast<uint64>(Values[i]));
		}
	}

	uint32 FGraphPatcher::EndpointIdFor(const FCommitScratch& Scratch, const int32 VtxPointIndex) const
	{
		return VtxPointIndex >= Scratch.VtxBase
			       ? PCGEx::H64A(static_cast<uint64>(Scratch.VtxIds[VtxPointIndex - Scratch.VtxBase]))
			       : Scratch.ResolvedEndpointIds.FindRef(VtxPointIndex);
	}

	void FGraphPatcher::ApplyRetargets(FCommitScratch& Scratch)
	{
		if (Retargets.IsEmpty())
		{
			return;
		}

		UPCGBasePointData* VtxData = VtxFacade->GetOut();
		const TConstPCGValueRange<int64> VtxEntries = VtxData->GetConstMetadataEntryValueRange();
		const TConstPCGValueRange<FTransform> VtxTransforms = VtxData->GetConstTransformValueRange();
		const int32 NumVtx = NumInitialVtx + NewVtxTransforms.Num();

		TMap<int32, TPCGValueRange<FTransform>> ComponentTransforms;
		bool bDropped = false;

		for (FEdgeRetarget& R : Retargets)
		{
			if (!Groups.IsValidIndex(R.GroupIndex) ||
				R.NewStart < 0 || R.NewStart >= NumVtx || R.NewEnd < 0 || R.NewEnd >= NumVtx)
			{
				bDropped = true;
				continue;
			}

			const FEdgeGroup& Group = Groups[R.GroupIndex];
			if (Group.ComponentIndex == INDEX_NONE ||
				R.EdgeLocalIndex < 0 || R.EdgeLocalIndex >= Group.OutWriteScope.Count)
			{
				bDropped = true;
				continue;
			}

			FComponentEdges& Comp = Scratch.Components[Group.ComponentIndex];
			if (!Comp.IdxAttr)
			{
				bDropped = true;
				continue;
			}

			const int32 EdgeP = Group.OutWriteScope.Start + R.EdgeLocalIndex;
			Comp.Ids[EdgeP - Comp.Base] = static_cast<int64>(PCGEx::H64(EndpointIdFor(Scratch, R.NewStart), EndpointIdFor(Scratch, R.NewEnd)));
			Comp.Dirty[EdgeP - Comp.Base] = true;

			// Keep the edge point where downstream expects it: midway between its (new) endpoints.
			TPCGValueRange<FTransform>* EdgeTransforms = ComponentTransforms.Find(Group.ComponentIndex);
			if (!EdgeTransforms)
			{
				Comp.Data->AllocateProperties(EPCGPointNativeProperties::Transform);
				EdgeTransforms = &ComponentTransforms.Add(Group.ComponentIndex, Comp.Data->GetTransformValueRange(false));
			}
			(*EdgeTransforms)[EdgeP].SetLocation(FMath::Lerp(VtxTransforms[R.NewStart].GetLocation(), VtxTransforms[R.NewEnd].GetLocation(), 0.5));

			R.bApplied = true;

			// Adjacency deltas per side; a side whose vtx is unchanged contributes nothing.
			if (R.OldStart != R.NewStart)
			{
				if (R.OldStart >= 0 && R.OldStart < NumVtx) { Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[R.OldStart])--; }
				Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[R.NewStart])++;
			}
			if (R.OldEnd != R.NewEnd)
			{
				if (R.OldEnd >= 0 && R.OldEnd < NumVtx) { Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[R.OldEnd])--; }
				Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[R.NewEnd])++;
			}
		}

		UE_CLOG(bDropped, LogPCGEx, Warning, TEXT("Graph patcher: retargets naming an unknown group, edge, or vtx were dropped."));
	}

	void FGraphPatcher::AppendStagedEdges(FCommitScratch& Scratch)
	{
		if (PendingEdges.IsEmpty())
		{
			return;
		}

		UPCGBasePointData* VtxData = VtxFacade->GetOut();
		const TConstPCGValueRange<int64> VtxEntries = VtxData->GetConstMetadataEntryValueRange();
		const TConstPCGValueRange<FTransform> VtxTransforms = VtxData->GetConstTransformValueRange();

		for (int32 c = 0; c < Scratch.Components.Num(); ++c)
		{
			FComponentEdges& Comp = Scratch.Components[c];
			const TArray<int32>& Handles = Scratch.ComponentPendingEdges[c];
			if (!Comp.IdxAttr || Handles.IsEmpty())
			{
				continue;
			}

			TPCGValueRange<FTransform> EdgeTransforms = Comp.Data->GetTransformValueRange(false);

			for (int32 i = 0; i < Handles.Num(); ++i)
			{
				FPendingEdge& E = PendingEdges[Handles[i]];
				const int32 EdgeP = Comp.FirstNewEdge + i;
				E.EdgePointIndex = EdgeP;

				Comp.Ids[EdgeP - Comp.Base] = static_cast<int64>(PCGEx::H64(EndpointIdFor(Scratch, E.A), EndpointIdFor(Scratch, E.B)));
				Comp.Dirty[EdgeP - Comp.Base] = true;

				EdgeTransforms[EdgeP].SetLocation(FMath::Lerp(VtxTransforms[E.A].GetLocation(), VtxTransforms[E.B].GetLocation(), 0.5));

				Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[E.A])++;
				Scratch.EdgeCountDelta.FindOrAdd(VtxEntries[E.B])++;
			}
		}
	}

	void FGraphPatcher::FlushEndpointIds(FCommitScratch& Scratch)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::Commit::FlushIds);

		auto Flush = [](FPCGMetadataAttribute<int64>* Attr, const TConstPCGValueRange<int64>& Entries, const int32 Base, const TArray<int64>& Ids, const TArray<bool>& Dirty)
		{
			int32 NumDirty = 0;
			for (const bool bDirty : Dirty) { NumDirty += bDirty ? 1 : 0; }

			if (NumDirty == 0)
			{
				return;
			}

			// Whole-range case: hand the entry range over as-is rather than compacting a copy of it.
			if (Base == 0 && NumDirty == Ids.Num() && Entries.Num() == Ids.Num())
			{
				Attr->SetValues(Entries, TConstArrayView<int64>(Ids));
				return;
			}

			TArray<PCGMetadataEntryKey> Keys;
			TArray<int64> Values;
			Keys.Reserve(NumDirty);
			Values.Reserve(NumDirty);

			for (int32 i = 0; i < Ids.Num(); ++i)
			{
				if (!Dirty[i]) { continue; }
				Keys.Add(Entries[Base + i]);
				Values.Add(Ids[i]);
			}

			Attr->SetValues(TConstArrayView<PCGMetadataEntryKey>(Keys), TConstArrayView<int64>(Values));
		};

		Flush(Scratch.VtxIdxAttr, VtxFacade->GetOut()->GetConstMetadataEntryValueRange(), Scratch.VtxBase, Scratch.VtxIds, Scratch.VtxIdsDirty);

		for (const FComponentEdges& Comp : Scratch.Components)
		{
			if (!Comp.IdxAttr) { continue; }
			Flush(Comp.IdxAttr, Comp.Data->GetConstMetadataEntryValueRange(), Comp.Base, Comp.Ids, Comp.Dirty);
		}

		if (Scratch.EdgeCountDelta.IsEmpty())
		{
			return;
		}

		// Counts land last, read back against what the flush above just wrote: the attribute holds one
		// value per entry, so vtx sharing an entry must accumulate into it, not overwrite each other.
		TArray<PCGMetadataEntryKey> Keys;
		TArray<int32> Deltas;
		Keys.Reserve(Scratch.EdgeCountDelta.Num());
		Deltas.Reserve(Scratch.EdgeCountDelta.Num());

		for (const TPair<PCGMetadataEntryKey, int32>& It : Scratch.EdgeCountDelta)
		{
			if (It.Value == 0)
			{
				continue;
			}
			Keys.Add(It.Key);
			Deltas.Add(It.Value);
		}

		if (Keys.IsEmpty())
		{
			return;
		}

		TArray<int64> Values;
		Values.SetNumZeroed(Keys.Num());
		Scratch.VtxIdxAttr->GetValuesFromItemKeys(TConstArrayView<PCGMetadataEntryKey>(Keys), TArrayView<int64>(Values));

		for (int32 i = 0; i < Values.Num(); ++i)
		{
			const uint64 Current = static_cast<uint64>(Values[i]);
			// Signed accumulate, floored at 0: retargets can subtract.
			const int64 NewCount = FMath::Max<int64>(0, static_cast<int64>(PCGEx::H64B(Current)) + Deltas[i]);
			Values[i] = static_cast<int64>(PCGEx::H64(PCGEx::H64A(Current), static_cast<uint32>(NewCount)));
		}

		Scratch.VtxIdxAttr->SetValues(TConstArrayView<PCGMetadataEntryKey>(Keys), TConstArrayView<int64>(Values));
	}

	void FGraphPatcher::InheritStagedVtxData(UPCGBasePointData* InVtxData) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::InheritStagedVtxData);
		
		const int32 NumNewVtx = NewVtxTransforms.Num();
		const TConstPCGValueRange<int64> Entries = InVtxData->GetConstMetadataEntryValueRange();

		// AddVtx already resolved every source to an initial vtx, or to INDEX_NONE.
		PCGExPointArrayDataHelpers::FReadWriteScope InheritScope(NumNewVtx, false);
		TArray<PCGMetadataEntryKey> SourceKeys;
		TArray<PCGMetadataEntryKey> TargetKeys;
		SourceKeys.Reserve(NumNewVtx);
		TargetKeys.Reserve(NumNewVtx);
		for (int32 i = 0; i < NumNewVtx; ++i)
		{
			const int32 Source = NewVtxInheritFrom[i];
			if (Source < 0)
			{
				continue;
			}
			const int32 Target = NumInitialVtx + i;
			InheritScope.Add(Source, Target);
			SourceKeys.Add(Entries[Source]);
			TargetKeys.Add(Entries[Target]);
		}

		if (SourceKeys.IsEmpty())
		{
			return;
		}

		// Self-copy: safe because Commit allocated these up front, so nothing reallocates mid-copy.
		if (InheritedProperties != EPCGPointNativeProperties::None)
		{
			InheritScope.CopyProperties(InVtxData, InVtxData, InheritedProperties, /*bClean=*/false);
		}

		// Per attribute rather than UPCGMetadata::SetAttributes, so exclusions can be honoured. Sharing
		// the source's value keys is the only mechanism that works within one metadata: entry-key
		// parenting resolves into a PARENT metadata, never to a sibling entry. Targets need entries first.
		UPCGMetadata* Metadata = InVtxData->Metadata;
		TArray<FName> AttributeNames;
		TArray<EPCGMetadataTypes> AttributeTypes;
		Metadata->GetAttributes(AttributeNames, AttributeTypes);

		// Const views: GetValueKeys overloads on both TArrayView<const T> and TArrayView<T>, so a plain
		// TArray argument is ambiguous.
		const TConstArrayView<PCGMetadataEntryKey> SourceView(SourceKeys);
		const TConstArrayView<PCGMetadataEntryKey> TargetView(TargetKeys);

		TArray<PCGMetadataValueKey> ValueKeys;
		for (const FName& AttributeName : AttributeNames)
		{
			if (InheritExclusions.Contains(AttributeName))
			{
				continue;
			}
			if (FPCGMetadataAttributeBase* Attribute = Metadata->GetMutableAttribute(AttributeName))
			{
				Attribute->GetValueKeys(SourceView, ValueKeys);
				Attribute->SetValuesFromValueKeys(TargetView, ValueKeys);
			}
		}
	}

	bool FGraphPatcher::GetEdgeOutput(const int32 EdgeHandle, TSharedPtr<PCGExData::FPointIO>& OutEdgesIO, int32& OutEdgePointIndex) const
	{
		if (!PendingEdges.IsValidIndex(EdgeHandle))
		{
			return false;
		}
		const FPendingEdge& E = PendingEdges[EdgeHandle];
		if (E.ComponentIndex == INDEX_NONE || E.EdgePointIndex == INDEX_NONE)
		{
			return false;
		}

		// Commit() stamps ComponentIndex and EdgePointIndex together, and only ever for a component it created.
		checkf(
			ComponentEdgeIOs.IsValidIndex(E.ComponentIndex),
			TEXT("Graph patcher: staged edge %d resolved to component %d of %d (edge point %d)."),
			EdgeHandle, E.ComponentIndex, ComponentEdgeIOs.Num(), E.EdgePointIndex);

		OutEdgesIO = ComponentEdgeIOs[E.ComponentIndex];
		OutEdgePointIndex = E.EdgePointIndex;
		return true;
	}

#pragma endregion

	void WriteConnectorFlags(
		FGraphPatcher& InPatcher,
		const TSharedRef<PCGExData::FFacade>& InVtxFacade,
		const FConnectorFlagsConfig& InConfig,
		const TArray<int32>& InEdgeHandles,
		const TArray<uint64>& InEndpoints)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FGraphPatcher::WriteConnectorFlags);
		
		if (!InConfig.IsEnabled())
		{
			return;
		}

		// Per-edge bool flag: resolve the attribute + entry range once per distinct output edge IO,
		// then flush each IO's keys in one batched write.
		if (InConfig.bFlagEdge)
		{
			struct FEdgeTarget
			{
				FPCGMetadataAttribute<bool>* Attr = nullptr;
				TConstPCGValueRange<int64> Entries;
				TArray<PCGMetadataEntryKey> Keys;
			};
			TMap<PCGExData::FPointIO*, FEdgeTarget> EdgeTargets;

			for (const int32 Handle : InEdgeHandles)
			{
				TSharedPtr<PCGExData::FPointIO> EdgesIO;
				int32 EdgePointIndex = -1;
				if (!InPatcher.GetEdgeOutput(Handle, EdgesIO, EdgePointIndex) || !EdgesIO)
				{
					continue;
				}

				FEdgeTarget* Target = EdgeTargets.Find(EdgesIO.Get());
				if (!Target)
				{
					UPCGBasePointData* Out = EdgesIO->GetOut();
					Target = &EdgeTargets.Add(EdgesIO.Get());
					Target->Attr = Out->MutableMetadata()->FindOrCreateAttribute<bool>(InConfig.EdgeFlagName, false);
					Target->Entries = Out->GetConstMetadataEntryValueRange();
				}

				if (Target->Attr)
				{
					Target->Keys.Add(Target->Entries[EdgePointIndex]);
				}
			}

			for (TPair<PCGExData::FPointIO*, FEdgeTarget>& It : EdgeTargets)
			{
				FEdgeTarget& Target = It.Value;
				if (!Target.Attr || Target.Keys.IsEmpty())
				{
					continue;
				}

				TArray<bool> Values;
				Values.Init(true, Target.Keys.Num());
				Target.Attr->SetValues(TConstArrayView<PCGMetadataEntryKey>(Target.Keys), TConstArrayView<bool>(Values));
			}
		}

		// Per-vtx int32 count: accumulate across all endpoints, then one batched read-modify-write.
		if (InConfig.bFlagVtx && !InEndpoints.IsEmpty())
		{
			UPCGBasePointData* VtxOut = InVtxFacade->GetOut();
			FPCGMetadataAttribute<int32>* VtxAttr = VtxOut->MutableMetadata()->FindOrCreateAttribute<int32>(InConfig.VtxFlagName, 0);
			if (!VtxAttr)
			{
				return;
			}

			const TConstPCGValueRange<int64> VtxEntries = VtxOut->GetConstMetadataEntryValueRange();

			// Keyed by entry key, not point index: the attribute holds one value per entry, so vtx
			// sharing an entry must accumulate into it rather than overwrite each other.
			TMap<PCGMetadataEntryKey, int32> Counts;
			Counts.Reserve(InEndpoints.Num());
			for (const uint64 Endpoint : InEndpoints)
			{
				Counts.FindOrAdd(VtxEntries[static_cast<int32>(PCGEx::H64A(Endpoint))])++;
				Counts.FindOrAdd(VtxEntries[static_cast<int32>(PCGEx::H64B(Endpoint))])++;
			}

			TArray<PCGMetadataEntryKey> Keys;
			TArray<int32> Values;
			Keys.Reserve(Counts.Num());
			Values.Reserve(Counts.Num());

			for (const TPair<PCGMetadataEntryKey, int32>& It : Counts)
			{
				Keys.Add(It.Key);
				Values.Add(It.Value);
			}

			TArray<int32> Current;
			Current.SetNumZeroed(Keys.Num());
			VtxAttr->GetValuesFromItemKeys(TConstArrayView<PCGMetadataEntryKey>(Keys), TArrayView<int32>(Current));

			for (int32 i = 0; i < Values.Num(); ++i) { Values[i] += Current[i]; }

			VtxAttr->SetValues(TConstArrayView<PCGMetadataEntryKey>(Keys), TConstArrayView<int32>(Values));
		}
	}
}
