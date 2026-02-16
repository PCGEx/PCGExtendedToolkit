// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExValencyProcessor.h"

#include "PCGParamData.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExCachedOrbitalCache.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Helpers/PCGExStreamingHelpers.h"

TArray<FPCGPinProperties> UPCGExValencyProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (WantsValencyMap())
	{
		PCGEX_PIN_PARAM(PCGExValency::Labels::SourceValencyMapLabel, "Valency map.", Required)
	}
	return PinProperties;
}

void FPCGExValencyProcessorContext::RegisterAssetDependencies()
{
	FPCGExClustersProcessorContext::RegisterAssetDependencies();

	const UPCGExValencyProcessorSettings* Settings = GetInputSettings<UPCGExValencyProcessorSettings>();
	if (!Settings) { return; }

	// WantsValencyMap nodes have no soft ptrs to register
	if (Settings->WantsValencyMap()) { return; }

	// Register OrbitalSet if wanted and provided
	if (Settings->WantsOrbitalSet() && !Settings->OrbitalSet.IsNull())
	{
		AddAssetDependency(Settings->OrbitalSet.ToSoftObjectPath());
	}

	// Register BondingRules if wanted and provided
	if (Settings->WantsBondingRules() && !Settings->BondingRules.IsNull())
	{
		AddAssetDependency(Settings->BondingRules.ToSoftObjectPath());
	}
}

bool FPCGExValencyProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyProcessor)

	// WantsValencyMap nodes have no soft ptrs to validate — map is consumed in PostBoot
	if (Settings->WantsValencyMap()) { return true; }

	// Validate OrbitalSet if wanted
	if (Settings->WantsOrbitalSet() && Settings->OrbitalSet.IsNull())
	{
		// OrbitalSet might come from BondingRules, so only fail if we don't also want BondingRules
		if (!Settings->WantsBondingRules())
		{
			if (!Settings->bQuietMissingOrbitalSet)
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("No Valency Orbital Set provided."));
			}
			return false;
		}
	}

	// Validate BondingRules if wanted
	if (Settings->WantsBondingRules() && Settings->BondingRules.IsNull())
	{
		if (!Settings->bQuietMissingBondingRules)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("No Bonding Rules provided."));
		}
		return false;
	}

	// Start loading assets
	if (!Settings->OrbitalSet.IsNull())
	{
		PCGExHelpers::LoadBlocking_AnyThreadTpl(Settings->OrbitalSet, InContext);
	}
	if (!Settings->BondingRules.IsNull())
	{
		PCGExHelpers::LoadBlocking_AnyThreadTpl(Settings->BondingRules, InContext);
	}

	return true;
}

void FPCGExValencyProcessorElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExClustersProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(ValencyProcessor)

	// WantsValencyMap nodes skip soft ptr loading
	if (Settings->WantsValencyMap()) { return; }

	// Load BondingRules first (OrbitalSet may come from it)
	if (!Settings->BondingRules.IsNull())
	{
		Context->BondingRules = Settings->BondingRules.Get();
	}

	// Load OrbitalSet - prefer explicit setting, fall back to BondingRules
	if (!Settings->OrbitalSet.IsNull())
	{
		Context->OrbitalSet = Settings->OrbitalSet.Get();
	}
	else if (Context->BondingRules && Context->BondingRules->OrbitalSet)
	{
		// Auto-populate from BondingRules if OrbitalSet not explicitly set
		Context->OrbitalSet = Context->BondingRules->OrbitalSet;
	}
}

bool FPCGExValencyProcessorElement::PostBoot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::PostBoot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyProcessor)

	if (Settings->WantsValencyMap())
	{
		if (!ConsumeValencyMap(InContext)) { return false; }
		return true;
	}

	// Validate BondingRules if wanted
	if (Settings->WantsBondingRules())
	{
		if (!Context->BondingRules)
		{
			if (!Settings->bQuietMissingBondingRules)
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to load Bonding Rules."));
			}
			return false;
		}
	}

	// Validate OrbitalSet if wanted
	if (Settings->WantsOrbitalSet())
	{
		if (!Context->OrbitalSet)
		{
			if (Context->BondingRules && !Context->BondingRules->OrbitalSet)
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules has no OrbitalSet. Rebuild the Bonding Rules asset."));
			}
			else if (!Settings->bQuietMissingOrbitalSet)
			{
				PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to load Valency Orbital Set."));
			}
			return false;
		}

		// Validate orbital set
		TArray<FText> ValidationErrors;
		if (!Context->OrbitalSet->Validate(ValidationErrors))
		{
			for (const FText& Error : ValidationErrors)
			{
				PCGE_LOG(Error, GraphAndLog, Error);
			}
			return false;
		}

		// Build orbital direction cache
		if (!Context->OrbitalResolver.BuildFrom(Context->OrbitalSet))
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to build orbital cache from orbital set."));
			return false;
		}

		// Set Suffix and MaxOrbitals on context for downstream use
		Context->Suffix = Context->OrbitalSet->LayerName;
		Context->MaxOrbitals = Context->OrbitalSet->Num();
	}

	return true;
}

bool FPCGExValencyProcessorElement::ConsumeValencyMap(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(ValencyProcessor)

	// 1. Set suffix from settings
	Context->Suffix = Settings->Suffix;

	// 2. Collect raw input param data for output duplication, then unpack
	for (const FPCGTaggedData& InTaggedData : InContext->InputData.GetParamsByPin(PCGExValency::Labels::SourceValencyMapLabel))
	{
		if (const UPCGParamData* ParamData = Cast<UPCGParamData>(InTaggedData.Data))
		{
			Context->InputValencyMapData.Add(ParamData);
		}
	}

	Context->ValencyUnpacker = MakeShared<PCGExValency::FValencyUnpacker>();
	Context->ValencyUnpacker->UnpackPin(InContext, PCGExValency::Labels::SourceValencyMapLabel);

	if (!Context->ValencyUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid Valency Map from input."));
		return false;
	}

	// 3. Resolve BondingRules (first entry)
	for (const auto& Pair : Context->ValencyUnpacker->GetBondingRules())
	{
		Context->BondingRules = Pair.Value;
		break;
	}

	if (!Context->BondingRules)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No Bonding Rules found in Valency Map."));
		return false;
	}

	if (!Context->BondingRules->IsCompiled())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules from Valency Map are not compiled."));
		return false;
	}

	// 4. Resolve OrbitalSet from BondingRules
	if (Context->BondingRules->OrbitalSet)
	{
		Context->OrbitalSet = Context->BondingRules->OrbitalSet;
	}

	if (!Context->OrbitalSet)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules in Valency Map has no OrbitalSet. Rebuild the Bonding Rules asset."));
		return false;
	}

	// 5. MaxOrbitals from map metadata, fallback to OrbitalSet
	Context->MaxOrbitals = Context->ValencyUnpacker->GetOrbitalCount(Context->BondingRules);
	if (Context->MaxOrbitals <= 0) { Context->MaxOrbitals = Context->OrbitalSet->Num(); }

	// 6. Validate OrbitalSet + build OrbitalResolver
	TArray<FText> ValidationErrors;
	if (!Context->OrbitalSet->Validate(ValidationErrors))
	{
		for (const FText& Error : ValidationErrors) { PCGE_LOG(Error, GraphAndLog, Error); }
		return false;
	}

	if (!Context->OrbitalResolver.BuildFrom(Context->OrbitalSet))
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to build orbital cache from orbital set."));
		return false;
	}

	// 7. Resolve ConnectorSet from BondingRules (optional, may be null)
	Context->ConnectorSet = Context->BondingRules->ConnectorSet;

	return true;
}

namespace PCGExValencyMT
{
	//////// IProcessor

	IProcessor::IProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade,
	                       const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
		: PCGExClusterMT::IProcessor(InVtxDataFacade, InEdgeDataFacade)
	{
	}

	bool IProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!PCGExClusterMT::IProcessor::Process(InTaskManager)) { return false; }

		// Get context for orbital set access
		FPCGExValencyProcessorContext* Context = static_cast<FPCGExValencyProcessorContext*>(ExecutionContext);
		if (!Context->OrbitalSet) { return false; }

		FilterVtxScope(PCGExMT::FScope(0, NumNodes));

		// Get edge indices reader for this processor's edge facade
		const FName IdxAttributeName = PCGExValency::Attributes::GetOrbitalAttributeName(Context->Suffix);
		EdgeIndicesReader = EdgeDataFacade->GetReadable<int64>(IdxAttributeName);

		if (!EdgeIndicesReader)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context,
			           FText::Format(FTEXT("Edge indices attribute '{0}' not found. Run 'Write Valency Orbitals' first."),
				           FText::FromName(IdxAttributeName)));
			return false;
		}

		// Build orbital cache now that cluster is available
		// (Readers were forwarded from batch in PrepareSingle)
		if (!BuildOrbitalCache())
		{
			// Specific error already logged by BuildOrbitalCache
			return false;
		}

		// Initialize valency states from cache
		InitializeValencyStates();

		return true;
	}
	

	bool IProcessor::BuildOrbitalCache()
	{
		// Check each requirement and log specific failure reason
		if (!Cluster)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("BuildOrbitalCache: Cluster is null."));
			return false;
		}
		if (!OrbitalMaskReader)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("BuildOrbitalCache: OrbitalMaskReader is null. Check that orbital mask attribute exists on vertices."));
			return false;
		}
		if (!EdgeIndicesReader)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("BuildOrbitalCache: EdgeIndicesReader is null. Check that orbital indices attribute exists on edges."));
			return false;
		}
		if (MaxOrbitals <= 0)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FText::Format(FTEXT("BuildOrbitalCache: MaxOrbitals is {0}, must be > 0. Check OrbitalSet configuration."), FText::AsNumber(MaxOrbitals)));
			return false;
		}

		// Use Suffix for cache identification
		FPCGExValencyProcessorContext* Context = static_cast<FPCGExValencyProcessorContext*>(ExecutionContext);
		const FName LayerName = Context->Suffix;
		const uint32 ContextHash = PCGExValency::FOrbitalCacheFactory::ComputeContextHash(LayerName, MaxOrbitals);

		// Try cluster cache first
		if (TSharedPtr<PCGExValency::FCachedOrbitalCache> Cached = Cluster->GetCachedData<PCGExValency::FCachedOrbitalCache>(
			PCGExValency::FOrbitalCacheFactory::CacheKey, ContextHash))
		{
			OrbitalCache = Cached->OrbitalCache;
			return OrbitalCache && OrbitalCache->IsValid();
		}

		// Build fresh
		OrbitalCache = MakeShared<PCGExValency::FOrbitalCache>();

		if (!OrbitalCache->BuildFrom(Cluster, OrbitalMaskReader, EdgeIndicesReader, MaxOrbitals))
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("BuildOrbitalCache: FOrbitalCache::BuildFrom failed."));
			OrbitalCache.Reset();
			return false;
		}

		// Store in cluster cache for downstream reuse
		TSharedPtr<PCGExValency::FCachedOrbitalCache> NewCached = MakeShared<PCGExValency::FCachedOrbitalCache>();
		NewCached->ContextHash = ContextHash;
		NewCached->OrbitalCache = OrbitalCache;
		NewCached->LayerName = LayerName;
		Cluster->SetCachedData(PCGExValency::FOrbitalCacheFactory::CacheKey, NewCached);

		return true;
	}

	void IProcessor::InitializeValencyStates()
	{
		if (OrbitalCache && OrbitalCache->IsValid())
		{
			OrbitalCache->InitializeStates(ValencyStates);
		}
	}

	//////// IBatch

	IBatch::IBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx,
	               TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: PCGExClusterMT::IBatch(InContext, InVtx, InEdges)
	{
		DefaultVtxFilterValue = true;
	}

	void IBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		PCGExClusterMT::IBatch::RegisterBuffersDependencies(FacadePreloader);

		// Register orbital mask attribute for preloading
		FPCGExValencyProcessorContext* Context = GetContext<FPCGExValencyProcessorContext>();
		if (Context && Context->OrbitalSet)
		{
			FacadePreloader.Register<int64>(ExecutionContext, PCGExValency::Attributes::GetMaskAttributeName(Context->Suffix));
		}
	}

	void IBatch::OnProcessingPreparationComplete()
	{
		// Create readers BEFORE calling parent (parent may trigger PrepareSingle)
		FPCGExValencyProcessorContext* Context = GetContext<FPCGExValencyProcessorContext>();

		if (Context && Context->OrbitalSet)
		{
			MaxOrbitals = Context->MaxOrbitals > 0 ? Context->MaxOrbitals : Context->OrbitalSet->Num();

			// Create orbital mask reader from vertex facade
			const FName MaskAttributeName = PCGExValency::Attributes::GetMaskAttributeName(Context->Suffix);
			OrbitalMaskReader = VtxDataFacade->GetReadable<int64>(MaskAttributeName);

			if (!OrbitalMaskReader)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context,
				           FText::Format(FTEXT("Orbital mask attribute '{0}' not found on vertices. Run 'Write Valency Orbitals' first."),
					           FText::FromName(MaskAttributeName)));
			}
		}
		else if (Context && !Context->OrbitalSet)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("OrbitalSet is null. Ensure BondingRules or OrbitalSet is configured."));
		}

		PCGExClusterMT::IBatch::OnProcessingPreparationComplete();
	}

	bool IBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!PCGExClusterMT::IBatch::PrepareSingle(InProcessor)) { return false; }

		IProcessor* ValencyProcessor = static_cast<IProcessor*>(InProcessor.Get());
		if (!ValencyProcessor) { return false; }

		// Forward readers and config to processor - cache will be built after cluster is available
		ValencyProcessor->OrbitalMaskReader = OrbitalMaskReader;
		ValencyProcessor->MaxOrbitals = MaxOrbitals;

		// Forward property writer if initialized
		ValencyProcessor->PropertyWriter = PropertyWriter;

		return true;
	}

	bool IBatch::InitializePropertyWriter(
		const UPCGExValencyBondingRules* BondingRules,
		const FPCGExValencyBondingRulesCompiled* CompiledRules,
		const FPCGExValencyPropertyOutputSettings& OutputSettings)
	{
		if (!BondingRules || !CompiledRules)
		{
			return false;
		}

		// Only create if there's something to output
		if (!OutputSettings.HasOutputs())
		{
			return true; // Success but no writer needed
		}

		PropertyWriter = MakeShared<FPCGExValencyPropertyWriter>();
		return PropertyWriter->Initialize(BondingRules, CompiledRules, VtxDataFacade, OutputSettings);
	}
}
