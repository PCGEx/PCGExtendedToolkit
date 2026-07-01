// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExFloodFill.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Containers/PCGExHashLookup.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForwardDetails.h"

#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/PCGExDataHelpers.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExFloodFill"
#define PCGEX_NAMESPACE FloodFill

#if WITH_EDITOR
void FPCGExFloodFillFlowDetails::ApplyDeprecation()
{
	FillRate.Update(FillRateInput_DEPRECATED, FillRateAttribute_DEPRECATED, FillRateConstant_DEPRECATED);
}
#endif

namespace PCGExFloodFill
{
	FDiffusion::FDiffusion(const TSharedPtr<FFillControlsHandler>& InFillControlsHandler, const TSharedPtr<PCGExClusters::FCluster>& InCluster, const PCGExClusters::FNode* InSeedNode)
		: FillControlsHandler(InFillControlsHandler)
		  , SeedNode(InSeedNode)
		  , Cluster(InCluster)
	{
		TravelStack = MakeShared<PCGEx::FHashLookupMap>(0, 0);

		// Pre-allocate visited array for O(1) lookups instead of TSet hashing
		const int32 NumNodes = InCluster->Nodes->Num();
		Visited.Init(false, NumNodes);

		// Pre-reserve arrays to avoid reallocations during growth
		Candidates.Reserve(NumNodes / 4); // Heuristic: ~25% of nodes as candidates at any time
		Captured.Reserve(NumNodes / 2);   // Heuristic: ~50% of nodes may be captured
	}

	int32 FDiffusion::GetSettingsIndex(EPCGExFloodFillSettingSource Source) const
	{
		return Source == EPCGExFloodFillSettingSource::Seed ? SeedIndex : SeedNode->PointIndex;
	}

	void FDiffusion::Init(const int32 InSeedIndex)
	{
		SeedIndex = InSeedIndex;

		// Initialize heap comparator with sorting mode from config
		HeapComparator = FCandidateHeapComparator(Config.Sorting);

		Visited[SeedNode->Index] = true;
		// Claiming is optional: with no InfluencesCount, diffusions overlap and never pre-claim their seed node (see FFillControlsHandler::TryCapture).
		if (FillControlsHandler->InfluencesCount)
		{
			*(FillControlsHandler->InfluencesCount->GetData() + SeedNode->PointIndex) = 1;
		}
		FCandidate& SeedCandidate = Captured.Emplace_GetRef();
		SeedCandidate.Link = PCGExGraphs::FLink(-1, -1);
		SeedCandidate.Node = SeedNode;
		SeedCandidate.CaptureIndex = 0;

		Probe(SeedCandidate);
	}

	void FDiffusion::Probe(const FCandidate& From)
	{
		if (!FillControlsHandler->IsValidProbe(this, From))
		{
			// Invalid as probe
			return;
		}

		const PCGExClusters::FNode& FromNode = *From.Node;
		const FVector FromPosition = Cluster->GetPos(FromNode);

		// Builds a scored candidate for a neighbor, without claiming (marking visited) it.
		const auto MakeCandidate = [&](PCGExClusters::FNode* OtherNode, const PCGExGraphs::FLink& Lk) -> FCandidate
		{
			const FVector OtherPosition = Cluster->GetPos(OtherNode);
			const double Dist = FVector::Dist(FromPosition, OtherPosition);

			FCandidate Candidate = FCandidate{};
			Candidate.CaptureIndex = From.CaptureIndex;
			Candidate.Link = PCGExGraphs::FLink(FromNode.Index, Lk.Edge);
			Candidate.Node = OtherNode;
			Candidate.Depth = From.Depth + 1;
			Candidate.Distance = Dist;
			Candidate.PathDistance = From.PathDistance + Dist;

			// Scoring via fill controls (use 'Heuristics Score' fill control for heuristics-based scoring)
			FillControlsHandler->ScoreCandidate(this, From, Candidate);
			return Candidate;
		};

		// Fan-out budget for this parent. MAX_int32 (the default when no Vtx+Reroute control is
		// active) means "unlimited" -- the claim-up-front path. bHasProbeFanout gates the handler call.
		const int32 FanoutLimit = FillControlsHandler->bHasProbeFanout ? FillControlsHandler->GetProbeFanoutLimit(this, From) : MAX_int32;

		if (FanoutLimit == MAX_int32)
		{
			// Unlimited: claim every first-seen neighbor up front (rejected ones stay visited -- legacy behavior).
			for (const PCGExGraphs::FLink& Lk : FromNode.Links)
			{
				PCGExClusters::FNode* OtherNode = Cluster->GetNode(Lk);
				const int32 OtherIndex = OtherNode->Index;

				// Fast array lookup instead of TSet hash lookup
				if (Visited[OtherIndex])
				{
					continue;
				}
				Visited[OtherIndex] = true;

				FCandidate Candidate = MakeCandidate(OtherNode, Lk);
				if (FillControlsHandler->IsValidCandidate(this, From, Candidate))
				{
					// O(log n) heap insertion instead of O(1) array add + O(n log n) sort later
					Candidates.HeapPush(Candidate, HeapComparator);
				}
			}
			return;
		}

		// Fan-out-limited (Vtx+Reroute): score all valid unvisited neighbors, claim only the best
		// 'FanoutLimit' by heap priority. Unclaimed ones stay unvisited for other nodes to adopt.
		TArray<FCandidate, TInlineAllocator<8>> Pending;
		for (const PCGExGraphs::FLink& Lk : FromNode.Links)
		{
			PCGExClusters::FNode* OtherNode = Cluster->GetNode(Lk);
			if (Visited[OtherNode->Index])
			{
				continue;
			}

			FCandidate Candidate = MakeCandidate(OtherNode, Lk);
			if (FillControlsHandler->IsValidCandidate(this, From, Candidate))
			{
				Pending.Add(Candidate);
			}
		}

		if (Pending.Num() > FanoutLimit)
		{
			Pending.Sort(HeapComparator);
			Pending.SetNum(FanoutLimit, EAllowShrinking::No);
		}

		for (const FCandidate& Candidate : Pending)
		{
			Visited[Candidate.Node->Index] = true;
			Candidates.HeapPush(Candidate, HeapComparator);
		}
	}

	void FDiffusion::Grow()
	{
		if (bStopped)
		{
			return;
		}

		bool bSearch = true;
		while (bSearch)
		{
			if (Candidates.IsEmpty())
			{
				bStopped = true;
				break;
			}

			// O(log n) heap pop instead of O(1) array pop (but we saved O(n log n) sort)
			FCandidate Candidate;
			Candidates.HeapPop(Candidate, HeapComparator, EAllowShrinking::No);

			if (!FillControlsHandler->TryCapture(this, Candidate))
			{
				continue;
			}

			// Update max depth & max distance
			MaxDepth = FMath::Max(MaxDepth, Candidate.Depth);
			MaxDistance = FMath::Max(MaxDistance, Candidate.PathDistance);

			FCandidate& CapturedCandidate = Captured.Add_GetRef(Candidate);
			CapturedCandidate.CaptureIndex = Captured.Num() - 1;

			TravelStack->Set(Candidate.Node->Index, PCGEx::NH64(Candidate.Link.Node, Candidate.Link.Edge));

			Endpoints.Add(CapturedCandidate.CaptureIndex);
			Endpoints.Remove(Candidate.CaptureIndex);

			PostGrow();

			bSearch = false;
		}
	}

	void FDiffusion::PostGrow()
	{
		// Probe from last captured candidate
		// New candidates are inserted via HeapPush, maintaining heap order - no sort needed
		Probe(Captured.Last());
	}

	void DiffuseAndBlend(
		const FDiffusion& Diffusion,
		const TSharedPtr<PCGExData::FFacade>& InVtxFacade,
		const TSharedPtr<PCGExBlending::FBlendOpsManager>& InBlendOps,
		TArray<int32>& OutIndices)
	{
		OutIndices.SetNumUninitialized(Diffusion.Captured.Num());
		const int32 SourceIndex = Diffusion.SeedNode->PointIndex;

		for (int i = 0; i < OutIndices.Num(); i++)
		{
			const FCandidate& Candidate = Diffusion.Captured[i];
			const int32 TargetIndex = Candidate.Node->PointIndex;

			OutIndices[i] = TargetIndex;

			if (TargetIndex != SourceIndex)
			{
				// TODO : Compute weight based on distance or depth
				InBlendOps->BlendAutoWeight(SourceIndex, TargetIndex);
			}
		}
	}

	FFillControlsHandler::FFillControlsHandler(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataCache, const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache, const TSharedPtr<PCGExData::FFacade>& InSeedsDataCache, const TArray<TObjectPtr<const UPCGExFillControlsFactoryData>>& InFactories)
		: ExecutionContext(InContext)
		  , Cluster(InCluster)
		  , VtxDataFacade(InVtxDataCache)
		  , EdgeDataFacade(InEdgeDataCache)
		  , SeedsDataFacade(InSeedsDataCache)
	{
		bIsValidHandler = BuildFrom(InContext, InFactories);
	}

	FFillControlsHandler::~FFillControlsHandler()
	{
	}

	bool FFillControlsHandler::BuildFrom(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExFillControlsFactoryData>>& InFactories)
	{
		if (!InFactories.IsEmpty())
		{
			Operations.Reserve(InFactories.Num());

			for (const TObjectPtr<const UPCGExFillControlsFactoryData>& Factory : InFactories)
			{
				TSharedPtr<FPCGExFillControlOperation> Op = Factory->CreateOperation(InContext);
				if (!Op)
				{
					return false;
				}

				Operations.Add(Op);
				if (Op->DoesScoring())
				{
					SubOpsScoring.Add(Op);
				}
				if (Op->ChecksProbe())
				{
					SubOpsProbe.Add(Op);
				}
				if (Op->ChecksCandidate())
				{
					SubOpsCandidate.Add(Op);
				}
				if (Op->ChecksCapture())
				{
					SubOpsCapture.Add(Op);
				}
				if (Op->WantsCaptureNotify())
				{
					SubOpsCaptureNotify.Add(Op);
				}
				if (Op->LimitsProbeFanout())
				{
					SubOpsProbeFanout.Add(Op);
				}
			}
		}

		bHasCaptureNotify = !SubOpsCaptureNotify.IsEmpty();
		bHasProbeFanout = !SubOpsProbeFanout.IsEmpty();

		return true;
	}

	bool FFillControlsHandler::PrepareForDiffusions(const TArray<TSharedPtr<FDiffusion>>& Diffusions, const FPCGExFloodFillFlowDetails& Details)
	{
		// Note: HeuristicsHandler is now optional - deprecated node-level heuristics
		// Use 'Heuristics Scoring' fill control instead for modern approach

		NumDiffusions = Diffusions.Num();

		SeedIndices = MakeShared<TArray<int32>>();
		SeedNodeIndices = MakeShared<TArray<int32>>();

		SeedIndices->SetNumUninitialized(NumDiffusions);
		SeedNodeIndices->SetNumUninitialized(NumDiffusions);

		// Create shared config from details
		DiffusionConfig = FDiffusionConfig(Details);

		for (int i = 0; i < NumDiffusions; i++)
		{
			*(SeedIndices->GetData() + i) = Diffusions[i]->SeedIndex;
			*(SeedNodeIndices->GetData() + i) = Diffusions[i]->SeedNode->PointIndex;

			// Set config on each diffusion
			Diffusions[i]->Config = DiffusionConfig;
		}

		PCGEX_SHARED_THIS_DECL
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : Operations)
		{
			Op->SettingsIndex = Op->Factory->ConfigBase.Source == EPCGExFloodFillSettingSource::Seed ? SeedIndices : SeedNodeIndices;
			if (!Op->PrepareForDiffusions(ExecutionContext, ThisPtr))
			{
				return false;
			}
		}

		return true;
	}

	void FFillControlsHandler::ScoreCandidate(const FDiffusion* Diffusion, const FCandidate& From, FCandidate& OutCandidate)
	{
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsScoring)
		{
			Op->ScoreCandidate(Diffusion, From, OutCandidate);
		}
	}

	bool FFillControlsHandler::TryCapture(const FDiffusion* Diffusion, const FCandidate& Candidate)
	{
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsCapture)
		{
			if (!Op->IsValidCapture(Diffusion, Candidate))
			{
				return false;
			}
		}
		// When claiming is disabled (no InfluencesCount), the && skips the atomic and capture is never gated on
		// exclusivity -- multiple diffusions may capture the same node.
		if (InfluencesCount && FPlatformAtomics::InterlockedCompareExchange((InfluencesCount->GetData() + Candidate.Node->PointIndex), 1, 0) == 1)
		{
			return false;
		}

		// Capture committed: notify opt-in controls so they can track per-capture state
		// (e.g. branch child counts). Runs on this diffusion's thread; one capturer per node.
		if (bHasCaptureNotify)
		{
			for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsCaptureNotify)
			{
				Op->OnCaptured(Diffusion, Candidate);
			}
		}

		return true;
	}

	bool FFillControlsHandler::IsValidProbe(const FDiffusion* Diffusion, const FCandidate& Candidate)
	{
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsProbe)
		{
			if (!Op->IsValidProbe(Diffusion, Candidate))
			{
				return false;
			}
		}
		return true;
	}

	bool FFillControlsHandler::IsValidCandidate(const FDiffusion* Diffusion, const FCandidate& From, const FCandidate& Candidate)
	{
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsCandidate)
		{
			if (!Op->IsValidCandidate(Diffusion, From, Candidate))
			{
				return false;
			}
		}
		return true;
	}

	int32 FFillControlsHandler::GetProbeFanoutLimit(const FDiffusion* Diffusion, const FCandidate& From)
	{
		int32 Limit = MAX_int32;
		for (const TSharedPtr<FPCGExFillControlOperation>& Op : SubOpsProbeFanout)
		{
			Limit = FMath::Min(Limit, Op->GetProbeFanoutLimit(Diffusion, From));
		}
		return Limit;
	}

#pragma region FDiffusionPathWriter

	FDiffusionPathWriter::FDiffusionPathWriter(
		const TSharedRef<PCGExClusters::FCluster>& InCluster,
		const TSharedRef<PCGExData::FFacade>& InVtxDataFacade,
		const TSharedRef<PCGExData::FPointIOCollection>& InPaths,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<TArray<int32>>& InDiffusionDepths)
		: Cluster(InCluster)
		  , VtxDataFacade(InVtxDataFacade)
		  , Paths(InPaths)
		  , TaskManager(InTaskManager)
		  , DiffusionDepths(InDiffusionDepths)
	{
	}

	void FDiffusionPathWriter::WriteNormalizedPathDepth(
		const TSharedRef<PCGExData::FFacade>& PathFacade,
		const TArray<int32>& PathIndices,
		const int32 EndpointDepth,
		const int32 MaxDiffusionDepth,
		const FName NormalizedPathDepthName,
		const EPCGExFloodFillNormalizedPathDepthMode Mode,
		const TArray<double>* CascadeValues)
	{
		if (NormalizedPathDepthName == NAME_None || !DiffusionDepths || PathIndices.IsEmpty())
		{
			return;
		}

		TSharedPtr<PCGExData::TBuffer<double>> NormBuffer = PathFacade->GetWritable<double>(FPCGAttributeIdentifier(NormalizedPathDepthName), 0.0, true, PCGExData::EBufferInit::New);
		if (!NormBuffer)
		{
			return;
		}

		const TArray<int32>& Depths = *DiffusionDepths;

		switch (Mode)
		{
		case EPCGExFloodFillNormalizedPathDepthMode::FullDiffusion:
			if (MaxDiffusionDepth > 0)
			{
				const double InvMax = 1.0 / static_cast<double>(MaxDiffusionDepth);
				for (int32 i = 0; i < PathIndices.Num(); i++)
				{
					NormBuffer->SetValue(i, static_cast<double>(Depths[PathIndices[i]]) * InvMax);
				}
			}
			break;

		case EPCGExFloodFillNormalizedPathDepthMode::FullPath:
			if (EndpointDepth > 0)
			{
				const double InvEndpoint = 1.0 / static_cast<double>(EndpointDepth);
				for (int32 i = 0; i < PathIndices.Num(); i++)
				{
					NormBuffer->SetValue(i, static_cast<double>(Depths[PathIndices[i]]) * InvEndpoint);
				}
			}
			break;

		case EPCGExFloodFillNormalizedPathDepthMode::Partition:
		{
			const int32 MinDepth = Depths[PathIndices[0]];
			const int32 MaxDepth = Depths[PathIndices.Last()];
			const int32 Range = MaxDepth - MinDepth;
			if (Range > 0)
			{
				const double InvRange = 1.0 / static_cast<double>(Range);
				for (int32 i = 0; i < PathIndices.Num(); i++)
				{
					NormBuffer->SetValue(i, static_cast<double>(Depths[PathIndices[i]] - MinDepth) * InvRange);
				}
			}
		}
		break;

		case EPCGExFloodFillNormalizedPathDepthMode::Cascade:
			if (CascadeValues)
			{
				for (int32 i = 0; i < PathIndices.Num(); i++)
				{
					NormBuffer->SetValue(i, (*CascadeValues)[PathIndices[i]]);
				}
			}
			break;
		}
	}

	void FDiffusionPathWriter::WriteFullPath(
		const FDiffusion& Diffusion,
		const int32 EndpointNodeIndex,
		const int32 EndpointDepth,
		const int32 MaxDiffusionDepth,
		const FName NormalizedPathDepthName,
		const EPCGExFloodFillNormalizedPathDepthMode NormalizedPathDepthMode,
		const FPCGExAttributeToTagDetails& SeedTags,
		const TSharedRef<PCGExData::FFacade>& SeedsDataFacade)
	{
		int32 PathNodeIndex = PCGEx::NH64A(Diffusion.TravelStack->Get(EndpointNodeIndex));
		int32 PathEdgeIndex = -1;

		TArray<int32> PathIndices;
		if (PathNodeIndex != -1)
		{
			PathIndices.Add(Cluster->GetNodePointIndex(EndpointNodeIndex));

			while (PathNodeIndex != -1)
			{
				const int32 CurrentIndex = PathNodeIndex;
				PCGEx::NH64(Diffusion.TravelStack->Get(CurrentIndex), PathNodeIndex, PathEdgeIndex);
				PathIndices.Add(Cluster->GetNodePointIndex(CurrentIndex));
			}
		}

		if (PathIndices.Num() < 2)
		{
			return;
		}

		Algo::Reverse(PathIndices);

		// Create a copy of the final vtx, so we get all the goodies
		TSharedPtr<PCGExData::FPointIO> PathIO = Paths->Emplace_GetRef<UPCGPointArrayData>(VtxDataFacade->Source->GetOut(), PCGExData::EIOInit::New);
		PCGExPaths::Helpers::SetClosedLoop(PathIO, false);

		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathIO->GetOut(), PathIndices.Num(), VtxDataFacade->Source->GetIn()->GetAllocatedProperties());
		PathIO->InheritPoints(PathIndices, 0);

		//Create a facade so we can do some manipulations
		TSharedRef<PCGExData::FFacade> PathFacade = MakeShared<PCGExData::FFacade>(PathIO.ToSharedRef());

		// Copy pending writable buffer values from vtx to path
		PCGExData::Helpers::CopyBuffersValues(VtxDataFacade, PathFacade, PathIndices, &PCGExClusters::Labels::ProtectedClusterAttributes);

		WriteNormalizedPathDepth(PathFacade, PathIndices, EndpointDepth, MaxDiffusionDepth, NormalizedPathDepthName, NormalizedPathDepthMode);

		PCGExClusters::Helpers::CleanupClusterData(PathIO);

		PathFacade->WriteFastest(TaskManager);
		SeedTags.Tag(SeedsDataFacade->GetInPoint(Diffusion.SeedIndex), PathIO);

		PathIO->IOIndex = Diffusion.SeedIndex * 1000000 + VtxDataFacade->Source->IOIndex * 1000000 + EndpointNodeIndex;
	}

	void FDiffusionPathWriter::WritePartitionedPath(
		const FDiffusion& Diffusion,
		TArray<int32>& PathIndices,
		const int32 EndpointDepth,
		const int32 MaxDiffusionDepth,
		const FName NormalizedPathDepthName,
		const EPCGExFloodFillNormalizedPathDepthMode NormalizedPathDepthMode,
		const FPCGExAttributeToTagDetails& SeedTags,
		const TSharedRef<PCGExData::FFacade>& SeedsDataFacade,
		const TArray<double>* CascadeValues)
	{
		if (PathIndices.Num() < 2)
		{
			return;
		}

		Algo::Reverse(PathIndices);

		// Create a copy of the final vtx, so we get all the goodies
		TSharedPtr<PCGExData::FPointIO> PathIO = Paths->Emplace_GetRef<UPCGPointArrayData>(VtxDataFacade->Source->GetOut(), PCGExData::EIOInit::New);
		PCGExPaths::Helpers::SetClosedLoop(PathIO, false);

		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathIO->GetOut(), PathIndices.Num(), VtxDataFacade->Source->GetIn()->GetAllocatedProperties());
		PathIO->InheritPoints(PathIndices, 0);

		//Create a facade so we can do some manipulations
		TSharedRef<PCGExData::FFacade> PathFacade = MakeShared<PCGExData::FFacade>(PathIO.ToSharedRef());

		// Copy pending writable buffer values from vtx to path
		PCGExData::Helpers::CopyBuffersValues(VtxDataFacade, PathFacade, PathIndices, &PCGExClusters::Labels::ProtectedClusterAttributes);

		WriteNormalizedPathDepth(PathFacade, PathIndices, EndpointDepth, MaxDiffusionDepth, NormalizedPathDepthName, NormalizedPathDepthMode, CascadeValues);

		PCGExClusters::Helpers::CleanupClusterData(PathIO);

		PathFacade->WriteFastest(TaskManager);
		SeedTags.Tag(SeedsDataFacade->GetInPoint(Diffusion.SeedIndex), PathIO);

		PathIO->IOIndex = Diffusion.SeedIndex * 1000000 + VtxDataFacade->Source->IOIndex * 1000000 + PathIndices[0];
	}

#pragma endregion
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
