// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExValencyPatternReplacement.h"

#include "Clusters/PCGExCluster.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Matchers/PCGExDefaultPatternMatcher.h"
#include "Matchers/PCGExConnectorPatternMatcher.h"

#define LOCTEXT_NAMESPACE "PCGExValencyPatternReplacement"
#define PCGEX_NAMESPACE ValencyPatternReplacement

TArray<FPCGPinProperties> UPCGExValencyPatternReplacementSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	// Valency Map pin is auto-added by base class via WantsValencyMap()
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExValencyPatternReplacementSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();

	if (bOutputMatchedPoints)
	{
		PCGEX_PIN_POINTS(TEXT("Matched"), "Points that were matched by patterns (for Remove/Fork strategies)", Required)
	}

	return PinProperties;
}

FPCGElementPtr UPCGExValencyPatternReplacementSettings::CreateElement() const
{
	return MakeShared<FPCGExValencyPatternReplacementElement>();
}

PCGExData::EIOInit UPCGExValencyPatternReplacementSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExValencyPatternReplacementSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ValencyPatternReplacement)

bool FPCGExValencyPatternReplacementElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExValencyProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyPatternReplacement)

	PCGEX_VALIDATE_INSTANCED_FACTORY(Matcher)

	return true;
}

bool FPCGExValencyPatternReplacementElement::PostBoot(FPCGExContext* InContext) const
{
	// Base class: ConsumeValencyMap -> BondingRules, OrbitalSet, OrbitalResolver, ConnectorSet, Suffix, MaxOrbitals
	if (!FPCGExValencyProcessorElement::PostBoot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyPatternReplacement)

	// Get compiled data
	if (!Context->BondingRules->IsCompiled())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Bonding Rules has no compiled data. Please rebuild."));
		return false;
	}

	const FPCGExValencyBondingRulesCompiled& CompiledData = Context->BondingRules->CompiledData;

	// Check for patterns - Pattern Replacement requires patterns to function
	if (!CompiledData.CompiledPatterns.HasPatterns())
	{
		if (!Settings->bQuietNoPatterns)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Bonding Rules has no patterns. Pattern Replacement requires patterns to be defined."));
		}
		return false;
	}

	Context->CompiledPatterns = &CompiledData.CompiledPatterns;

	// Register matcher factory from Settings
	if (Settings->Matcher)
	{
		Context->MatcherFactory = PCGEX_REGISTER_INSTANCED_FACTORY_C(Context, UPCGExPatternMatcherFactory, Settings->Matcher, NAME_None);

		// If this is a connector pattern matcher, resolve its asset on game thread
		if (auto* ConnFactory = Cast<UPCGExConnectorPatternMatcherFactory>(const_cast<UPCGExPatternMatcherFactory*>(Context->MatcherFactory.Get())))
		{
			if (Context->ConnectorSet)
			{
				const FName EdgeConnectorAttrName = Context->ConnectorSet->GetConnectorAttributeName();
				if (!ConnFactory->ResolveAsset(Context, &CompiledData, Context->ConnectorSet, EdgeConnectorAttrName))
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Connector Pattern Matcher: failed to resolve ConnectorPatternAsset."));
					return false;
				}
			}
			else
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Connector Pattern Matcher requires a ConnectorSet in BondingRules."));
				return false;
			}
		}
	}

	if (!Context->MatcherFactory && !Settings->bQuietNoMatcher)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("No pattern matcher configured. Node will only annotate patterns using default subgraph matching."));
	}

	return true;
}

bool FPCGExValencyPatternReplacementElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(ValencyPatternReplacement)

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

namespace PCGExValencyPatternReplacement
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		// Parent handles: edge indices reader, BuildOrbitalCache(), InitializeValencyStates()
		if (!TProcessor::Process(InTaskManager)) { return false; }

		// Validate ValencyEntry reader (required for pattern matching)
		if (!ValencyEntryReader)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("ValencyEntry attribute not found. Run Valency : Solve first."));
			return false;
		}

		if (!Context->CompiledPatterns || !Context->CompiledPatterns->HasPatterns())
		{
			return true; // No patterns = nothing to match, but not an error
		}

		if (!OrbitalCache)
		{
			return false;
		}

		// Finalize matcher allocations with cluster and edge data (e.g., build ConnectorCache)
		if (MatcherAllocations)
		{
			MatcherAllocations->FinalizeAllocations(Cluster, EdgeDataFacade);
		}

		// Run pattern matching
		RunMatching();

		Write();

		return true;
	}

	void FProcessor::RunMatching()
	{
		const int32 NodesCount = OrbitalCache->GetNumNodes();
		const int32 Seed = VtxDataFacade->Source->IOIndex;

		// Create matcher operation from factory (or use default if none provided)
		if (Context->MatcherFactory)
		{
			MatcherOperation = Context->MatcherFactory->CreateOperation();
		}

		// If no factory provided or factory failed, create a default subgraph matcher
		if (!MatcherOperation)
		{
			TSharedPtr<FPCGExDefaultPatternMatcherOperation> DefaultOp = MakeShared<FPCGExDefaultPatternMatcherOperation>();
			DefaultOp->bExclusive = true;
			DefaultOp->OverlapResolution = EPCGExPatternOverlapResolution::WeightBased;
			MatcherOperation = DefaultOp;
		}

		// Initialize the operation with shared state (ValencyEntry reader provides module indices)
		MatcherOperation->Initialize(
			Cluster,
			Context->CompiledPatterns,
			OrbitalCache.Get(),
			ValencyEntryReader,
			NodesCount,
			&ClaimedNodes,
			Seed, MatcherAllocations);

		// Set NodeIndex to PointIndex mapping (REQUIRED for buffer access)
		// Also set debug node positions for detailed logging
		{
			TConstPCGValueRange<FTransform> Transforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();
			TArray<int32> NodeToPointMapping;
			TArray<FVector> NodePositions;
			NodeToPointMapping.SetNum(NodesCount);
			NodePositions.SetNum(NodesCount);
			const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
			for (int32 i = 0; i < NodesCount; ++i)
			{
				NodeToPointMapping[i] = Nodes[i].PointIndex;
				NodePositions[i] = Transforms[Nodes[i].PointIndex].GetLocation();
			}
			MatcherOperation->SetNodeToPointMapping(NodeToPointMapping);
			MatcherOperation->SetDebugNodePositions(NodePositions);
		}

		// Run matching
		PCGExPatternMatcher::FMatchResult Result = MatcherOperation->Match();

		if (Result.bSuccess)
		{
			// Run annotation (writes PatternName and MatchIndex attributes)
			MatcherOperation->Annotate(PatternNameWriter, PatternMatchIndexWriter);

			// Track annotated nodes for flag writing
			MatcherOperation->CollectAnnotatedNodes(AnnotatedNodes);
		}

		// Apply matches (topology-altering - to be moved to separate nodes in Phase 4)
		ApplyMatches();
	}

	void FProcessor::ApplyMatches()
	{
		if (!MatcherOperation) { return; }

		// Apply output strategies from matcher's matches
		// Note: Annotation is already done by the matcher operation during Annotate()
		// This method handles topology-altering operations (to be moved in Phase 4)
		for (const FPCGExValencyPatternMatch& Match : MatcherOperation->GetMatches())
		{
			if (!Match.IsValid()) { continue; }

			// Get pattern settings via matcher (handles both orbital and connector patterns)
			const FPCGExValencyPatternSettingsCompiled* PatternSettings = MatcherOperation->GetMatchPatternSettings(Match);
			if (!PatternSettings) { continue; }

			// Skip unclaimed exclusive matches
			if (!Match.bClaimed && PatternSettings->bExclusive) { continue; }

			// Apply output strategy
			switch (PatternSettings->OutputStrategy)
			{
			case EPCGExPatternOutputStrategy::Remove:
			case EPCGExPatternOutputStrategy::Fork:
				// Mark active nodes for removal/forking
				for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
				{
					if (MatcherOperation->IsMatchEntryActive(Match, EntryIdx))
					{
						NodesToRemove.Add(Match.EntryToNode[EntryIdx]);
					}
				}
				break;

			case EPCGExPatternOutputStrategy::Collapse:
				// Mark all active nodes for removal except one (which gets the replacement transform)
				{
					bool bFirstActive = true;
					for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
					{
						if (!MatcherOperation->IsMatchEntryActive(Match, EntryIdx)) { continue; }

						const int32 NodeIdx = Match.EntryToNode[EntryIdx];
						if (bFirstActive)
						{
							// First active node becomes the collapsed point
							CollapseReplacements.Add(NodeIdx, Match.ReplacementTransform);
							bFirstActive = false;
						}
						else
						{
							// Other active nodes are removed
							NodesToRemove.Add(NodeIdx);
						}
					}
				}
				break;

			case EPCGExPatternOutputStrategy::Swap:
				{
					const int32 SwapTarget = MatcherOperation->GetMatchSwapTarget(Match);
					if (SwapTarget >= 0)
					{
						for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
						{
							if (MatcherOperation->IsMatchEntryActive(Match, EntryIdx))
							{
								SwapTargets.Add(Match.EntryToNode[EntryIdx], SwapTarget);
							}
						}
					}
				}
				break;

			case EPCGExPatternOutputStrategy::Annotate:
				// Already done by matcher operations, nothing more to do
				break;
			}
		}
	}

	FTransform FProcessor::ComputeReplacementTransform(const FPCGExValencyPatternMatch& Match, const FPCGExValencyPatternSettingsCompiled& PatternSettings)
	{
		TConstPCGValueRange<FTransform> Transforms = VtxDataFacade->Source->GetIn()->GetConstTransformValueRange();

		switch (PatternSettings.TransformMode)
		{
		case EPCGExPatternTransformMode::Centroid:
			{
				FVector Centroid = FVector::ZeroVector;
				int32 ActiveCount = 0;

				for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
				{
					if (!MatcherOperation->IsMatchEntryActive(Match, EntryIdx)) { continue; }

					const int32 NodeIdx = Match.EntryToNode[EntryIdx];
					Centroid += Transforms[NodeIdx].GetLocation();
					++ActiveCount;
				}

				if (ActiveCount > 0)
				{
					Centroid /= ActiveCount;
				}

				return FTransform(Centroid);
			}

		case EPCGExPatternTransformMode::PatternRoot:
			{
				// Use root entry's node transform
				const int32 RootNodeIdx = Match.EntryToNode[0];
				return Transforms[RootNodeIdx];
			}

		case EPCGExPatternTransformMode::FirstMatch:
			{
				// Use first active entry's node transform
				for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
				{
					if (MatcherOperation->IsMatchEntryActive(Match, EntryIdx))
					{
						const int32 NodeIdx = Match.EntryToNode[EntryIdx];
						return Transforms[NodeIdx];
					}
				}
				return FTransform::Identity;
			}
		}

		return FTransform::Identity;
	}

	void FProcessor::Write()
	{
		TProcessor::Write();

		// Helper to convert NodeIndex to PointIndex
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		auto GetPointIdx = [&Nodes](int32 NodeIdx) -> int32
		{
			return Nodes.IsValidIndex(NodeIdx) ? Nodes[NodeIdx].PointIndex : -1;
		};

		// Apply collapse replacement transforms
		if (!CollapseReplacements.IsEmpty())
		{
			TPCGValueRange<FTransform> OutTransforms = VtxDataFacade->GetOut()->GetTransformValueRange();
			for (const auto& Pair : CollapseReplacements)
			{
				const int32 PointIdx = GetPointIdx(Pair.Key);
				if (PointIdx >= 0)
				{
					OutTransforms[PointIdx] = Pair.Value;
				}
			}
		}

		// Update ValencyEntry attribute with pattern flags
		if (ValencyEntryReader && ValencyEntryWriter)
		{
			// Annotated flag
			for (const int32 NodeIdx : AnnotatedNodes)
			{
				const int32 PointIdx = GetPointIdx(NodeIdx);
				if (PointIdx < 0) { continue; }
				int64 EntryHash = ValencyEntryReader->Read(PointIdx);
				if (EntryHash == PCGExValency::EntryData::INVALID_ENTRY) { continue; }
				EntryHash = PCGExValency::EntryData::SetFlag(static_cast<uint64>(EntryHash), PCGExValency::EntryData::Flags::Annotated);
				ValencyEntryWriter->SetValue(PointIdx, static_cast<int64>(EntryHash));
			}

			// Consumed flag (for removed nodes)
			for (const int32 NodeIdx : NodesToRemove)
			{
				const int32 PointIdx = GetPointIdx(NodeIdx);
				if (PointIdx < 0) { continue; }
				int64 EntryHash = ValencyEntryWriter->GetValue(PointIdx);
				if (EntryHash == PCGExValency::EntryData::INVALID_ENTRY) { continue; }
				EntryHash = PCGExValency::EntryData::SetFlag(static_cast<uint64>(EntryHash), PCGExValency::EntryData::Flags::Consumed);
				ValencyEntryWriter->SetValue(PointIdx, static_cast<int64>(EntryHash));
			}

			// Collapsed flag (for kept collapse nodes)
			for (const auto& Pair : CollapseReplacements)
			{
				const int32 PointIdx = GetPointIdx(Pair.Key);
				if (PointIdx < 0) { continue; }
				int64 EntryHash = ValencyEntryWriter->GetValue(PointIdx);
				if (EntryHash == PCGExValency::EntryData::INVALID_ENTRY) { continue; }
				EntryHash = PCGExValency::EntryData::SetFlag(static_cast<uint64>(EntryHash), PCGExValency::EntryData::Flags::Collapsed);
				ValencyEntryWriter->SetValue(PointIdx, static_cast<int64>(EntryHash));
			}

			// Swapped flag + updated module index (for swap targets)
			for (const auto& Pair : SwapTargets)
			{
				const int32 PointIdx = GetPointIdx(Pair.Key);
				if (PointIdx < 0) { continue; }
				int64 EntryHash = ValencyEntryWriter->GetValue(PointIdx);
				if (EntryHash == PCGExValency::EntryData::INVALID_ENTRY) { continue; }
				// Reconstruct hash with new module index and updated flags
				const uint32 BondingRulesMapId = PCGExValency::EntryData::GetBondingRulesMapId(static_cast<uint64>(EntryHash));
				uint16 ExistingFlags = PCGExValency::EntryData::GetPatternFlags(static_cast<uint64>(EntryHash));
				ExistingFlags |= PCGExValency::EntryData::Flags::Swapped;
				EntryHash = static_cast<int64>(PCGExValency::EntryData::Pack(BondingRulesMapId, static_cast<uint16>(Pair.Value), ExistingFlags));
				ValencyEntryWriter->SetValue(PointIdx, EntryHash);
			}
		}

		// Mark nodes for removal (actual removal happens in batch Write via point filtering)
		// The NodesToRemove set is used by the batch to filter points
	}

	// FBatch 

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch::RegisterBuffersDependencies(FacadePreloader);

		const FPCGExValencyPatternReplacementContext* Context = GetContext<FPCGExValencyPatternReplacementContext>();

		// Register ValencyEntry attribute
		if (const UPCGExValencyPatternReplacementSettings* TypedSettings = Context ? Context->GetInputSettings<UPCGExValencyPatternReplacementSettings>() : nullptr)
		{
			const FName EntryAttrName = PCGExValency::EntryData::GetEntryAttributeName(TypedSettings->Suffix);
			FacadePreloader.Register<int64>(ExecutionContext, EntryAttrName);
		}

		// Register buffer dependencies from matcher factory
		if (Context && Context->MatcherFactory)
		{
			Context->MatcherFactory->RegisterPrimaryBuffersDependencies(ExecutionContext, FacadePreloader);
		}
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		const UPCGExValencyPatternReplacementSettings* Settings = ExecutionContext->GetInputSettings<UPCGExValencyPatternReplacementSettings>();
		const FPCGExValencyPatternReplacementContext* Context = GetContext<FPCGExValencyPatternReplacementContext>();

		// Create ValencyEntry reader/writer
		const FName EntryAttrName = PCGExValency::EntryData::GetEntryAttributeName(Settings->Suffix);
		ValencyEntryReader = VtxDataFacade->GetReadable<int64>(EntryAttrName);
		if (ValencyEntryReader)
		{
			ValencyEntryWriter = VtxDataFacade->GetWritable<int64>(EntryAttrName, 0, true, PCGExData::EBufferInit::Inherit);
		}

		// Create pattern name writer
		PatternNameWriter = VtxDataFacade->GetWritable<FName>(Settings->PatternNameAttributeName, FName(NAME_None), true, PCGExData::EBufferInit::New);

		// Create pattern match index writer
		PatternMatchIndexWriter = VtxDataFacade->GetWritable<int32>(Settings->PatternMatchIndexAttributeName, -1, true, PCGExData::EBufferInit::New);

		// Create matcher-specific allocations from factory
		if (Context && Context->MatcherFactory)
		{
			MatcherAllocations = Context->MatcherFactory->CreateAllocations(VtxDataFacade);

			// Pass ValencyEntry reader to matcher allocations if applicable
			if (MatcherAllocations && ValencyEntryReader)
			{
				MatcherAllocations->SetValencyEntryReader(ValencyEntryReader);
			}
		}

		TBatch::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch::PrepareSingle(InProcessor)) { return false; }

		FProcessor* Processor = static_cast<FProcessor*>(InProcessor.Get());
		Processor->ValencyEntryReader = ValencyEntryReader;
		Processor->ValencyEntryWriter = ValencyEntryWriter;
		Processor->PatternNameWriter = PatternNameWriter;
		Processor->PatternMatchIndexWriter = PatternMatchIndexWriter;
		Processor->MatcherAllocations = MatcherAllocations;

		return true;
	}

	void FBatch::CompleteWork()
	{
		TBatch<FProcessor>::CompleteWork();
		VtxDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
