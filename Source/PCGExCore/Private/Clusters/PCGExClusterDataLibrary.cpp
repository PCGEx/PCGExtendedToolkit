// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/PCGExClusterDataLibrary.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"

namespace PCGExClusters
{
	FDataLibrary::FDataLibrary(const bool InDisableInvalidData)
	{
		bDisableInvalidData = InDisableInvalidData;
		ProblemsTracker.Init(0, EProblemLogs.Num() + 1);
		InputDictionary = MakeShared<PCGExData::FPointIOTaggedDictionary>(Labels::TagStr_PCGExCluster);
	}

	bool FDataLibrary::Build(const TSharedPtr<PCGExData::FPointIOCollection>& InMixedCollection)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDataLibrary::Build_Mixed);

		if (InMixedCollection->Pairs.IsEmpty())
		{
			return false;
		}

		// First, cache all "valid" vtx & edge data from the collection

		for (const TSharedPtr<PCGExData::FPointIO>& MainIO : InMixedCollection->Pairs)
		{
			// Vtx ?

			if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
			{
				if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
				{
					Invalidate(MainIO, EProblem::DoubleMarking);
					continue;
				}

				if (!Helpers::IsPointDataVtxReady(MainIO->GetIn()->Metadata))
				{
					Invalidate(MainIO, EProblem::VtxTagButNoMeta);
					continue;
				}

				TaggedVtx.Add(MainIO);
				continue;
			}

			// Edge ?

			if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
			{
				if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
				{
					Invalidate(MainIO, EProblem::DoubleMarking);
					continue;
				}

				if (!Helpers::IsPointDataEdgeReady(MainIO->GetIn()->Metadata))
				{
					Invalidate(MainIO, EProblem::EdgeTagButNoMeta);
					continue;
				}

				TaggedEdges.Add(MainIO);
				continue;
			}

			Invalidate(MainIO, EProblem::NoTags);
		}

		return BuildDictionary();
	}

	bool FDataLibrary::Build(const TSharedPtr<PCGExData::FPointIOCollection>& InVtxCollection, const TSharedPtr<PCGExData::FPointIOCollection>& InEdgeCollection)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDataLibrary::Build);

		if (InVtxCollection->IsEmpty() || InEdgeCollection->IsEmpty())
		{
			return false;
		}

		// Gather Vtx inputs
		for (const TSharedPtr<PCGExData::FPointIO>& VtxIO : InVtxCollection->Pairs)
		{
			if (VtxIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
			{
				if (VtxIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
				{
					Invalidate(VtxIO, EProblem::DoubleMarking);
					continue;
				}

				if (!Helpers::IsPointDataVtxReady(VtxIO->GetIn()->Metadata))
				{
					Invalidate(VtxIO, EProblem::VtxTagButNoMeta);
					continue;
				}

				TaggedVtx.Add(VtxIO);
				continue;
			}

			if (VtxIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
			{
				if (VtxIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
				{
					Invalidate(VtxIO, EProblem::DoubleMarking);
					continue;
				}

				Invalidate(VtxIO, EProblem::EdgeWrongPin);
				continue;
			}

			Invalidate(VtxIO, EProblem::NoTags);
		}

		// Gather Edge inputs
		for (const TSharedPtr<PCGExData::FPointIO>& MainIO : InEdgeCollection->Pairs)
		{
			if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
			{
				if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
				{
					Invalidate(MainIO, EProblem::DoubleMarking);
					continue;
				}

				if (!Helpers::IsPointDataEdgeReady(MainIO->GetIn()->Metadata))
				{
					Invalidate(MainIO, EProblem::EdgeTagButNoMeta);
					continue;
				}

				TaggedEdges.Add(MainIO);
				continue;
			}

			if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExVtx))
			{
				if (MainIO->Tags->IsTagged(Labels::TagStr_PCGExEdges))
				{
					Invalidate(MainIO, EProblem::DoubleMarking);
					continue;
				}

				Invalidate(MainIO, EProblem::VtxWrongPin);
				continue;
			}

			Invalidate(MainIO, EProblem::NoTags);
		}

		return BuildDictionary();
	}

	bool FDataLibrary::IsDataValid(const TSharedPtr<PCGExData::FPointIO>& InPointIO) const
	{
		return !Invalidated.Contains(InPointIO);
	}

	TSharedPtr<PCGExData::FPointIOTaggedEntries> FDataLibrary::GetAssociatedEdges(const TSharedPtr<PCGExData::FPointIO>& InVtxIO) const
	{
		if (const PCGExDataId CurrentPairId = PCGEX_GET_DATAIDTAG(InVtxIO->Tags, PCGExClusters::Labels::TagStr_PCGExCluster))
		{
			if (TSharedPtr<PCGExData::FPointIOTaggedEntries> EdgesEntries = InputDictionary->GetEntries(CurrentPairId->Value);
				EdgesEntries && !EdgesEntries->Entries.IsEmpty())
			{
				return EdgesEntries;
			}
		}

		return nullptr;
	}

	void FDataLibrary::PrintLogs(FPCGExContext* InContext, bool bSkipTrivial, bool bSkipImportant, const TSet<EProblem>* InMuted)
	{
		for (int i = 0; i < ProblemsTracker.Num(); i++)
		{
			if (ProblemsTracker[i] <= 0)
			{
				continue;
			}

			if (InMuted && InMuted->Contains(static_cast<EProblem>(i)))
			{
				continue;
			}

			const FProblem& Problem = EProblemLogs[static_cast<EProblem>(i)];
			if ((bSkipTrivial && !Problem.Get<0>()) || (bSkipImportant && Problem.Get<0>()))
			{
				continue;
			}

			PCGE_LOG_C(Warning, GraphAndLog, InContext, Problem.Get<1>());
		}
	}

	bool FDataLibrary::BuildDictionary()
	{
		// Try to rebuild valid relationships between the valid data

		TArray<TSharedPtr<PCGExData::FPointIO>> Keys;
		Keys.Reserve(TaggedVtx.Num());

		// Insert Vtx as keys		
		for (const TSharedPtr<PCGExData::FPointIO>& Vtx : TaggedVtx)
		{
			if (!InputDictionary->CreateKey(Vtx.ToSharedRef()))
			{
				Invalidate(Vtx, EProblem::VtxDupes);
			}
			Keys.Add(Vtx);
		}

		// Assign edges to their Vtx group
		for (const TSharedPtr<PCGExData::FPointIO>& Edges : TaggedEdges)
		{
			if (!InputDictionary->TryAddEntry(Edges.ToSharedRef()))
			{
				Invalidate(Edges, EProblem::RoamingEdges);
			}
		}

		// Cleanup Vtx keys that have no edges
		for (const TSharedPtr<PCGExData::FPointIO>& Key : Keys)
		{
			// Try and find paired Vtx/Edges
			if (const TSharedPtr<PCGExData::FPointIOTaggedEntries> EdgesEntries = GetAssociatedEdges(Key);
				!EdgesEntries)
			{
				Invalidate(Key, EProblem::RoamingVtx);
				InputDictionary->RemoveKey(Key.ToSharedRef());
			}
		}

		return InputDictionary->TagMap.Num() > 0;
	}

	void FDataLibrary::Invalidate(const TSharedPtr<PCGExData::FPointIO>& InPointData, const EProblem Problem)
	{
		bool bAlreadyInvalid = false;
		Invalidated.Add(InPointData, &bAlreadyInvalid);

		if (bDisableInvalidData)
		{
			InPointData->Disable();
		}

		if (bAlreadyInvalid)
		{
			return;
		}

		if (Problem != EProblem::None)
		{
			Log(Problem);
		}
	}

	void FDataLibrary::Log(const EProblem Problem)
	{
		ProblemsTracker[static_cast<int32>(Problem)]++;
	}

	FClusterDataForwardHandler::FClusterDataForwardHandler(const TSharedPtr<FCluster>& InCluster, const TSharedPtr<PCGExData::FDataForwardHandler>& InVtxDataForwardHandler, const TSharedPtr<PCGExData::FDataForwardHandler>& InEdgeDataForwardHandler)
		: Cluster(InCluster)
		  , VtxDataForwardHandler(InVtxDataForwardHandler)
		  , EdgeDataForwardHandler(InEdgeDataForwardHandler)
	{
	}
}
