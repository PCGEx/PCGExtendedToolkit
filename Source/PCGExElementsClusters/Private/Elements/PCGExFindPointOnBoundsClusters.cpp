// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFindPointOnBoundsClusters.h"


#include "Clusters/PCGExCluster.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExBlendingHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Utils/PCGExPointIOMerger.h"

#define LOCTEXT_NAMESPACE "PCGExFindPointOnBoundsClusters"
#define PCGEX_NAMESPACE FindPointOnBoundsClusters

#if WITH_EDITOR
void UPCGExFindPointOnBoundsClustersSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		// Rewire UVW
		PCGEX_SHORTHAND_RENAME_PIN(LocalUVW, UVW, UVWValue)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFindPointOnBoundsClustersSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		UVWValue.Update(UVWInput_DEPRECATED, LocalUVW_DEPRECATED, UVW_DEPRECATED);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExData::EIOInit UPCGExFindPointOnBoundsClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindPointOnBoundsClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

void FPCGExFindPointOnBoundsClustersContext::ClusterProcessing_InitialProcessingDone()
{
	FPCGExClustersProcessorContext::ClusterProcessing_InitialProcessingDone();
	PCGEX_SETTINGS_LOCAL(FindPointOnBoundsClusters)
}

PCGEX_INITIALIZE_ELEMENT(FindPointOnBoundsClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindPointOnBoundsClusters)

TArray<FPCGPinProperties> UPCGExFindPointOnBoundsClustersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PinProperties.Pop();
	return PinProperties;
}

bool FPCGExFindPointOnBoundsClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}
	PCGEX_CONTEXT_AND_SETTINGS(FindPointOnBoundsClusters)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
	{
		TSharedPtr<PCGExData::FPointIOCollection> Collection = Settings->SearchMode == EPCGExClusterClosestSearchMode::Vtx ? Context->MainPoints : Context->MainEdges;
		TSet<FName> AttributeMismatches;

		Context->BestIndices.Init(-1, Context->MainEdges->Num());
		Context->IOMergeSources.Init(nullptr, Context->MainEdges->Num());

		Context->MergedOut = PCGExData::NewPointIO(Context, Settings->GetMainOutputPin(), 0);
		Context->MergedAttributesInfos = PCGExData::FAttributesInfos::Get(Collection, AttributeMismatches);

		Context->CarryOverDetails.Attributes.Prune(*Context->MergedAttributesInfos);
		Context->CarryOverDetails.Attributes.Prune(AttributeMismatches);

		Context->MergedOut->InitializeOutput(PCGExData::EIOInit::New);
		// There is a risk we allocate too many points here if there's less valid clusters than expected
		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->MergedOut->GetOut(), Context->MainEdges->Num());
		Context->MergedOut->GetOutKeys(true);

		if (!AttributeMismatches.IsEmpty() && !Settings->bQuietAttributeMismatchWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Some attributes on incoming data share the same name but not the same type. Whatever type was discovered first will be used."));
		}
	}

	return true;
}

bool FPCGExFindPointOnBoundsClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindPointOnBoundsClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindPointOnBoundsClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
	{
		TSharedPtr<PCGExData::FPointIOCollection> Collection = Settings->SearchMode == EPCGExClusterClosestSearchMode::Vtx ? Context->MainPoints : Context->MainEdges;
		PCGExBlending::Helpers::MergeBestCandidatesAttributes(Context->MergedOut, Context->IOMergeSources, Context->BestIndices, *Context->MergedAttributesInfos);

		(void)Context->MergedOut->StageOutput(Context);
	}
	else
	{
		if (Settings->SearchMode == EPCGExClusterClosestSearchMode::Vtx)
		{
			Context->MainPoints->StageOutputs();
		}
		else
		{
			Context->MainEdges->StageOutputs();
		}
	}

	return Context->TryComplete();
}


namespace PCGExFindPointOnBoundsClusters
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		FBox Bounds = FBox(ForceInit);
		FVector UVW = Settings->UVWValue.GetValueSetting(Context, Settings->ClusterElement == EPCGExClusterElement::Edge ? EdgeDataFacade->GetIn() : VtxDataFacade->GetIn())->Read(0);

		if (Settings->bBestFitBounds)
		{
			const TConstPCGValueRange<FTransform> InVtxTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();
			const TUniquePtr<PCGExClusters::FCluster::TConstVtxLookup> IdxLookup = MakeUnique<PCGExClusters::FCluster::TConstVtxLookup>(Cluster);
			TArray<int32> PtIndices;
			IdxLookup->Dump(PtIndices);

			PCGExMath::FBestFitPlane BestFitPlane(InVtxTransforms, PtIndices);

			FTransform T = BestFitPlane.GetTransform(Settings->AxisOrder);
			UVW = T.TransformVector(UVW);
			Bounds = FBox(BestFitPlane.Centroid - BestFitPlane.Extents, BestFitPlane.Centroid + BestFitPlane.Extents).TransformBy(T);
		}
		else
		{
			Bounds = Cluster->Bounds;
		}

		SearchPosition = Bounds.GetCenter() + Bounds.GetExtent() * UVW;
		Cluster->RebuildOctree(Settings->SearchMode);

		if (Settings->SearchMode == EPCGExClusterClosestSearchMode::Vtx)
		{
			StartParallelLoopForNodes();
		}
		else
		{
			StartParallelLoopForEdges();
		}

		return true;
	}

	void FProcessor::UpdateCandidate(const FVector& InPosition, const int32 InIndex)
	{
		const double Dist = FVector::Dist(InPosition, SearchPosition);

		{
			FWriteScopeLock WriteLock(BestIndexLock);
			if (Dist > BestDistance)
			{
				return;
			}
		}

		{
			FWriteScopeLock WriteLock(BestIndexLock);

			if (Dist > BestDistance)
			{
				return;
			}

			BestPosition = InPosition;
			BestIndex = InIndex;
			BestDistance = Dist;
		}
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];

			UpdateCandidate(Cluster->GetPos(Node), Node.PointIndex);
		}
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			UpdateCandidate(Cluster->GetClosestPointOnEdge(Index, SearchPosition), Index);
		}
	}

	void FProcessor::CompleteWork()
	{
		const TSharedPtr<PCGExData::FPointIO> IORef = Settings->SearchMode == EPCGExClusterClosestSearchMode::Vtx ? VtxDataFacade->Source : EdgeDataFacade->Source;

		const FVector Offset = (BestPosition - Cluster->Bounds.GetCenter()).GetSafeNormal() * Settings->Offset;

		if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
		{
			const int32 TargetIndex = EdgeDataFacade->Source->IOIndex;
			Context->IOMergeSources[TargetIndex] = IORef;

			TPCGValueRange<FTransform> OutTransforms = Context->MergedOut->GetOut()->GetTransformValueRange(false);
			TPCGValueRange<int64> OutMetadataEntries = Context->MergedOut->GetOut()->GetMetadataEntryValueRange(false);

			const PCGMetadataEntryKey OriginalKey = OutMetadataEntries[TargetIndex];

			IORef->GetIn()->CopyPointsTo(Context->MergedOut->GetOut(), BestIndex, TargetIndex, 1);

			OutTransforms[TargetIndex].AddToTranslation(Offset);
			OutMetadataEntries[TargetIndex] = OriginalKey;
		}
		else
		{
			PCGEX_INIT_IO_VOID(IORef, PCGExData::EIOInit::New)
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(IORef->GetOut(), 1);

			IORef->InheritPoints(BestIndex, 0, 1);

			TPCGValueRange<FTransform> OutTransforms = IORef->GetOut()->GetTransformValueRange(false);
			TPCGValueRange<int64> OutMetadataEntries = IORef->GetOut()->GetMetadataEntryValueRange(false);

			OutTransforms[0].AddToTranslation(Offset);
			IORef->GetOut()->Metadata->InitializeOnSet(OutMetadataEntries[0]);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
