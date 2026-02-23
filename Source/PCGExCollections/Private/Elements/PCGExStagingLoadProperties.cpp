// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadProperties.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Core/PCGExAssetCollection.h"
#include "PCGExPropertyTypes.h"
#include "PCGParamData.h"

#define LOCTEXT_NAMESPACE "PCGExStagingLoadPropertiesElement"
#define PCGEX_NAMESPACE StagingLoadProperties

PCGEX_INITIALIZE_ELEMENT(StagingLoadProperties)

void UPCGExStagingLoadPropertiesSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

PCGExData::EIOInit UPCGExStagingLoadPropertiesSettings::GetMainDataInitializationPolicy() const
{
	return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingLoadProperties)

bool FPCGExStagingLoadPropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadProperties)

	Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionPickUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	PCGEX_FWD(PropertyOutputSettings)

	if (!Context->PropertyOutputSettings.HasOutputs())
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("No property outputs configured."));
	}

	return true;
}

bool FPCGExStagingLoadPropertiesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingLoadPropertiesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadProperties)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	return Context->TryComplete();
}

namespace PCGExStagingLoadProperties
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter) { return false; }

		// Step 1: Collect unique entry hashes (O(N) scan, but enables O(1) lookups later)
		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedUniqueEntryHashes = MakeShared<PCGExMT::TScopedSet<uint64>>(Loops, 8);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingLoadProperties::ProcessPoints);

		// Scoped fetch & filtering
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		// Collect unique hashes
		TSet<uint64>& LocalSet = ScopedUniqueEntryHashes->Get_Ref(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }
			
			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash != 0)
			{
				LocalSet.Add(Hash);
			}
		}
	}
	
	void FProcessor::BuildPropertyCaches()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::BuildPropertyCaches);

		// For each configured property output
		for (const FPCGExPropertyOutputConfig& Config : Context->PropertyOutputSettings.Configs)
		{
			if (!Config.IsValid()) { continue; }

			const FName OutputName = Config.GetEffectiveOutputName();
			const FName PropName = Config.PropertyName;

			// Find a prototype property from any collection
			const FInstancedStruct* Prototype = nullptr;
			for (const auto& CollectionPair : Context->CollectionPickUnpacker->GetCollections())
			{
				if (const UPCGExAssetCollection* Collection = CollectionPair.Value)
				{
					if (const FInstancedStruct* Found = Collection->CollectionProperties.GetPropertyByName(PropName))
					{
						Prototype = Found;
						break;
					}
				}
			}

			if (!Prototype)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Property '{0}' not found in any staged collection, skipping."),
					           FText::FromName(PropName)));
				continue;
			}

			// Create cache entry
			FPropertyCache& Cache = PropertyCaches.Add(PropName);
			Cache.Writer = *Prototype;
			Cache.SourceByHash.Reserve(UniqueEntryHashes.Num());

			// Initialize the output buffer
			if (FPCGExProperty* Prop = Cache.Writer.GetMutablePtr<FPCGExProperty>())
			{
				if (!Prop->InitializeOutput(PointDataFacade, OutputName))
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
						           FTEXT("Failed to initialize output buffer for property '{0}', skipping."),
						           FText::FromName(PropName)));
					PropertyCaches.Remove(PropName);
					continue;
				}
				Cache.WriterPtr = Prop;
			}
			else
			{
				PropertyCaches.Remove(PropName);
				continue;
			}

			// Pre-resolve source for each unique hash
			int16 MaterialPick = 0;
			for (const uint64 Hash : UniqueEntryHashes)
			{
				if (FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
					Result.IsValid())
				{
					const FPCGExProperty* Source = nullptr;

					// Check entry overrides first
					if (const FInstancedStruct* SourceProp = Result.Entry->PropertyOverrides.GetOverride(PropName))
					{
						Source = SourceProp->GetPtr<FPCGExProperty>();
					}
					else if (Result.Host)
					{
						// Fallback to collection defaults
						if (const FInstancedStruct* CollectionProp = Result.Host->CollectionProperties.GetPropertyByName(PropName))
						{
							Source = CollectionProp->GetPtr<FPCGExProperty>();
						}
					}

					if (Source)
					{
						Cache.SourceByHash.Add(Hash, Source);
					}
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		ScopedUniqueEntryHashes->Collapse(UniqueEntryHashes);

		if (UniqueEntryHashes.IsEmpty())
		{
			// No valid entries found
			bIsProcessorValid = false;
			return;
		}

		// Step 2: Initialize writers and pre-resolve properties for all unique hashes
		BuildPropertyCaches();

		if (PropertyCaches.IsEmpty())
		{
			// No valid property outputs
			bIsProcessorValid = false;
			return;
		}

		PCGEX_PARALLEL_FOR(
			PointDataFacade->GetNum(),
			if (!PointFilterCache[i]) { return; }

			const uint64 Hash = EntryHashGetter->Read(i);

			// For each property, lookup cached source and write
			for (const auto& CachePair : PropertyCaches)
			{
				const FPropertyCache& Cache = CachePair.Value;

				if (const FPCGExProperty* const* SourcePtr = Cache.SourceByHash.Find(Hash))
				{
					Cache.WriterPtr->WriteOutputFrom(i, *SourcePtr);
				}
			}
		)

		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
