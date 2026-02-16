// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWriteValencyOrbitals.h"

#include "PCGParamData.h"
#include "Core/PCGExCachedOrbitalCache.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExValencyOrbitalCache.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Clusters/PCGExCluster.h"

#define LOCTEXT_NAMESPACE "PCGExWriteValencyOrbitals"
#define PCGEX_NAMESPACE WriteValencyOrbitals

PCGExData::EIOInit UPCGExWriteValencyOrbitalsSettings::GetMainOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }
PCGExData::EIOInit UPCGExWriteValencyOrbitalsSettings::GetEdgeOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }

TArray<FPCGPinProperties> UPCGExWriteValencyOrbitalsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExWriteValencyOrbitalsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_PARAMS(PCGExValency::Labels::OutputValencyMapLabel, "Valency map for downstream nodes", Required)
	return PinProperties;
}

void FPCGExWriteValencyOrbitalsContext::RegisterAssetDependencies()
{
	FPCGExClustersProcessorContext::RegisterAssetDependencies();

	const UPCGExWriteValencyOrbitalsSettings* Settings = GetInputSettings<UPCGExWriteValencyOrbitalsSettings>();
	if (!Settings) { return; }

	if (!Settings->BondingRules.IsNull())
	{
		AddAssetDependency(Settings->BondingRules.ToSoftObjectPath());
	}
}

PCGEX_INITIALIZE_ELEMENT(WriteValencyOrbitals)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(WriteValencyOrbitals)

bool FPCGExWriteValencyOrbitalsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(WriteValencyOrbitals)

	Context->AssignmentMode = Settings->AssignmentMode;
	Context->Suffix = Settings->Suffix;

	if (Settings->BondingRules.IsNull())
	{
		if (!Settings->bQuietMissingBondingRules) { PCGE_LOG(Error, GraphAndLog, FTEXT("No Bonding Rules provided.")); }
		return false;
	}

	return true;
}

void FPCGExWriteValencyOrbitalsElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExClustersProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(WriteValencyOrbitals)

	if (!Settings->BondingRules.IsNull())
	{
		Context->BondingRules = Settings->BondingRules.Get();
	}

	if (!Context->BondingRules) { return; }

	if (Settings->AssignmentMode == EPCGExOrbitalAssignmentMode::Direction)
	{
		if (Context->BondingRules->OrbitalSet)
		{
			Context->OrbitalSet = Context->BondingRules->OrbitalSet;
		}
	}
	else if (Settings->AssignmentMode == EPCGExOrbitalAssignmentMode::Connector)
	{
		Context->ConnectorSet = Context->BondingRules->ConnectorSet;
	}
}

bool FPCGExWriteValencyOrbitalsElement::PostBoot(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(WriteValencyOrbitals)

	if (!FPCGExClustersProcessorElement::PostBoot(InContext)) { return false; }

	if (!Context->BondingRules)
	{
		if (!Settings->bQuietMissingBondingRules) { PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to load Bonding Rules.")); }
		return false;
	}

	// Create packer for output Valency Map
	Context->ValencyPacker = MakeShared<PCGExValency::FValencyPacker>(Context);
	Context->ValencyPacker->RegisterBondingRules(Context->BondingRules);

	if (Settings->AssignmentMode == EPCGExOrbitalAssignmentMode::Direction)
	{
		if (!Context->OrbitalSet)
		{
			if (!Settings->bQuietMissingBondingRules) { PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules has no OrbitalSet.")); }
			return false;
		}

		// Validate orbital set
		TArray<FText> ValidationErrors;
		if (!Context->OrbitalSet->Validate(ValidationErrors))
		{
			for (const FText& Error : ValidationErrors) { PCGE_LOG(Error, GraphAndLog, Error); }
			return false;
		}

		// Build orbital cache for fast processing
		if (!Context->OrbitalResolver.BuildFrom(Context->OrbitalSet))
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to build orbital cache from orbital set."));
			return false;
		}
	}
	else if (Settings->AssignmentMode == EPCGExOrbitalAssignmentMode::Connector)
	{
		if (!Context->ConnectorSet)
		{
			if (!Settings->bQuietMissingBondingRules) { PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules has no Connector Set.")); }
			return false;
		}

		// Validate connector set
		TArray<FText> ValidationErrors;
		if (!Context->ConnectorSet->Validate(ValidationErrors))
		{
			for (const FText& Error : ValidationErrors) { PCGE_LOG(Error, GraphAndLog, Error); }
			return false;
		}

		// Compile connector set to assign bit indices
		Context->ConnectorSet->Compile();

		// Build connector type to orbital index mapping
		const int32 NumConnectorTypes = Context->ConnectorSet->Num();
		Context->ConnectorToOrbitalMap.SetNum(NumConnectorTypes);
		for (int32 i = 0; i < NumConnectorTypes; ++i)
		{
			Context->ConnectorToOrbitalMap[i] = i;
		}
	}

	// Store orbital info in packer
	Context->ValencyPacker->SetOrbitalInfo(Context->BondingRules, Context->GetOrbitalCount());

	return true;
}

bool FPCGExWriteValencyOrbitalsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(WriteValencyOrbitals)

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	// Output Valency Map
	UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();
	Context->ValencyPacker->PackToDataset(ParamData);
	Context->StageOutput(ParamData, PCGExValency::Labels::OutputValencyMapLabel, PCGExData::EStaging::None);

	return Context->TryComplete();
}

namespace PCGExWriteValencyOrbitals
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteValencyOrbitals::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		// Determine attribute name based on suffix
		const FName IdxAttributeName = PCGExValency::Attributes::GetOrbitalAttributeName(Context->Suffix);
		if (Context->AssignmentMode == EPCGExOrbitalAssignmentMode::Connector)
		{

			// Create connector reader for input packed connector references
			ConnectorReader = EdgeDataFacade->GetReadable<int64>(Settings->ConnectorAttributeName);
			if (!ConnectorReader)
			{
				if (!Settings->bQuietMissingConnectorAttribute)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
						FTEXT("Connector attribute '{0}' not found on edges. Using fallback behavior."),
						FText::FromName(Settings->ConnectorAttributeName)));
				}
			}
		}

		// Initialize all edge indices to sentinel values (no match)
		IdxWriter = EdgeDataFacade->GetWritable<int64>(IdxAttributeName, PCGExValency::EdgeOrbital::NoMatchSentinel(), false, PCGExData::EBufferInit::New);
		StartParallelLoopForNodes();

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		TConstPCGValueRange<FTransform> InTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();

		TSharedPtr<PCGExData::TArrayBuffer<int64>> IdxArrayWriter = StaticCastSharedPtr<PCGExData::TArrayBuffer<int64>>(IdxWriter);
		TArray<int64>* EdgeIndices = IdxArrayWriter->GetOutValues().Get();
		TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges.Get();

		if (Context->AssignmentMode == EPCGExOrbitalAssignmentMode::Direction)
		{
			// ========== DIRECTION MODE ==========
			// Use cached orbital data for fast lookup
			const PCGExValency::FOrbitalDirectionResolver& Cache = Context->OrbitalResolver;
			const bool bUseTransform = Cache.bTransformOrbital;

			PCGEX_SCOPE_LOOP(Index)
			{
				PCGExClusters::FNode& Node = Nodes[Index];

				const FTransform& DirTransform = bUseTransform ? InTransforms[Node.PointIndex] : FTransform::Identity;

				int64 OrbitalMask = 0;

				// Process each link from this node
				for (const PCGExClusters::FLink& Link : Node.Links)
				{
					const int32 EdgeIndex = Link.Edge;
					const int32 NeighborNodeIndex = Link.Node;

					// Get direction to neighbor
					const FVector Direction = Cluster->GetDir(Node.Index, NeighborNodeIndex);

					// Find matching orbital using cached data
					const uint8 OrbitalIndex = Cache.FindMatchingOrbital(Direction, bUseTransform, DirTransform);

					// Write orbital index directly to the appropriate byte offset
					// Start node writes to byte 0, end node writes to byte 1
					// This avoids race conditions since each node writes to a different byte
					const PCGExGraphs::FEdge& Edge = Edges[EdgeIndex];
					uint8* PackedBytes = reinterpret_cast<uint8*>(EdgeIndices->GetData() + EdgeIndex);
					const int32 ByteOffset = (Edge.Start == Node.PointIndex) ? 0 : 1;

					if (OrbitalIndex != PCGExValency::NO_ORBITAL_MATCH)
					{
						// Get the bitmask directly from cache
						OrbitalMask |= Cache.GetBitmask(OrbitalIndex);
						PackedBytes[ByteOffset] = OrbitalIndex;
					}
					else
					{
						PackedBytes[ByteOffset] = PCGExValency::NO_ORBITAL_MATCH;
						FPlatformAtomics::InterlockedIncrement(&NoMatchCount);
					}
				}

				// Write vertex orbital mask
				if (VertexMasks)
				{
					(*VertexMasks)[Node.PointIndex] = OrbitalMask;
				}
			}
		}
		else // Connector mode
		{
			// ========== CONNECTOR MODE ==========
			// Read connector references from edge attribute and map to orbital indices
			const bool bHasConnectorData = ConnectorReader != nullptr;

			const TArray<int32>& ConnectorToOrbital = Context->ConnectorToOrbitalMap;
			const int32 MaxConnectorTypes = Context->ConnectorSet ? Context->ConnectorSet->Num() : 0;

			PCGEX_SCOPE_LOOP(Index)
			{
				PCGExClusters::FNode& Node = Nodes[Index];

				int64 OrbitalMask = 0;

				// Process each link from this node
				for (const PCGExClusters::FLink& Link : Node.Links)
				{
					const int32 EdgeIndex = Link.Edge;
					const PCGExGraphs::FEdge& Edge = Edges[EdgeIndex];

					// Determine which byte offset this node writes to
					uint8* PackedBytes = reinterpret_cast<uint8*>(EdgeIndices->GetData() + EdgeIndex);
					const int32 ByteOffset = (Edge.Start == Node.PointIndex) ? 0 : 1;

					uint8 OrbitalIndex = PCGExValency::NO_ORBITAL_MATCH;

					if (bHasConnectorData)
					{
						// Read the packed connector reference for this edge
						const int64 PackedConnector = ConnectorReader->Read(EdgeIndex);

						if (PCGExValencyConnector::IsValid(PackedConnector))
						{
							// Extract connector index from packed reference
							// Note: RulesIndex is ignored here - we assume single ConnectorSet context
							const uint16 ConnectorTypeIndex = PCGExValencyConnector::GetConnectorIndex(PackedConnector);

							// Map connector type to orbital index
							if (ConnectorTypeIndex < MaxConnectorTypes && ConnectorToOrbital.IsValidIndex(ConnectorTypeIndex))
							{
								OrbitalIndex = static_cast<uint8>(ConnectorToOrbital[ConnectorTypeIndex]);
							}
							else
							{
								FPlatformAtomics::InterlockedIncrement(&InvalidConnectorCount);
							}
						}
						else
						{
							FPlatformAtomics::InterlockedIncrement(&InvalidConnectorCount);
						}
					}
					else
					{
						// No connector data - mark as invalid
						FPlatformAtomics::InterlockedIncrement(&InvalidConnectorCount);
					}

					// Write orbital index
					if (OrbitalIndex != PCGExValency::NO_ORBITAL_MATCH)
					{
						// Build orbital mask (connector type index = orbital index, so bitmask = 1 << index)
						OrbitalMask |= (1LL << OrbitalIndex);
						PackedBytes[ByteOffset] = OrbitalIndex;
					}
					else
					{
						PackedBytes[ByteOffset] = PCGExValency::NO_ORBITAL_MATCH;
					}
				}

				// Write vertex orbital mask
				if (VertexMasks)
				{
					(*VertexMasks)[Node.PointIndex] = OrbitalMask;
				}
			}
		}
	}

	void FProcessor::OnNodesProcessingComplete()
	{
		EdgeDataFacade->WriteFastest(TaskManager);

		// Direction mode warnings
		if (Context->AssignmentMode == EPCGExOrbitalAssignmentMode::Direction)
		{
			if (NoMatchCount > 0 && Settings->bWarnOnNoMatch)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					FTEXT("Valency Orbitals: {0} edge directions did not match any orbital."),
					FText::AsNumber(NoMatchCount)));
			}
		}
		// Connector mode warnings
		else if (Context->AssignmentMode == EPCGExOrbitalAssignmentMode::Connector)
		{
			if (InvalidConnectorCount > 0 && Settings->bWarnOnNoMatch)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					FTEXT("Valency Connectors: {0} edges had invalid or missing connector references."),
					FText::AsNumber(InvalidConnectorCount)));
			}
		}

		// Build and cache OrbitalCache for downstream nodes
		if (Settings->bBuildOrbitalCache && Cluster && VertexMasks && IdxWriter)
		{
			const int32 MaxOrbitals = Context->GetOrbitalCount();
			const FName LayerName = Context->GetLayerName();

			if (MaxOrbitals > 0)
			{
				const uint32 ContextHash = PCGExValency::FOrbitalCacheFactory::ComputeContextHash(LayerName, MaxOrbitals);

				// Get the raw edge indices array from the writer
				TSharedPtr<PCGExData::TArrayBuffer<int64>> IdxArrayWriter = StaticCastSharedPtr<PCGExData::TArrayBuffer<int64>>(IdxWriter);
				TSharedPtr<TArray<int64>> EdgeIndices = IdxArrayWriter->GetOutValues();

				TSharedPtr<PCGExValency::FOrbitalCache> OrbitalCache = MakeShared<PCGExValency::FOrbitalCache>();
				if (OrbitalCache->BuildFromArrays(Cluster, *VertexMasks, *EdgeIndices, MaxOrbitals))
				{
					TSharedPtr<PCGExValency::FCachedOrbitalCache> Cached = MakeShared<PCGExValency::FCachedOrbitalCache>();
					Cached->ContextHash = ContextHash;
					Cached->OrbitalCache = OrbitalCache;
					Cached->LayerName = LayerName;
					Cluster->SetCachedData(PCGExValency::FOrbitalCacheFactory::CacheKey, Cached);
				}
			}
		}
	}

	//////// BATCH

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteValencyOrbitals)

		// Use suffix-based attribute name
		const FName MaskAttributeName = PCGExValency::Attributes::GetMaskAttributeName(Context->Suffix);

		// Create vertex mask writer
		MaskWriter = VtxDataFacade->GetWritable<int64>(MaskAttributeName, 0, false, PCGExData::EBufferInit::Inherit);
		const TSharedPtr<PCGExData::TArrayBuffer<int64>> MaskArrayWriter = StaticCastSharedPtr<PCGExData::TArrayBuffer<int64>>(MaskWriter);
		VertexMasks = MaskArrayWriter->GetOutValues();

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }

		FProcessor* TypedProcessor = static_cast<FProcessor*>(InProcessor.Get());
		TypedProcessor->VertexMasks = VertexMasks;
		TypedProcessor->MaskWriter = MaskWriter;

		return true;
	}

	void FBatch::CompleteWork()
	{
		VtxDataFacade->WriteFastest(TaskManager);
		TBatch<FProcessor>::CompleteWork();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
