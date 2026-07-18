// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadProperties.h"

#include "PCGExPropertyTypes.h"
#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/Grammar/PCGSubdivisionBase.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"

#define LOCTEXT_NAMESPACE "PCGExStagingLoadPropertiesElement"
#define PCGEX_NAMESPACE StagingLoadProperties

PCGEX_INITIALIZE_ELEMENT(StagingLoadProperties)

void UPCGExStagingLoadPropertiesSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

PCGExData::EIOInit UPCGExStagingLoadPropertiesSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingLoadProperties)

bool FPCGExStagingLoadPropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadProperties)

	Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionPickUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	PCGEX_FWD(PropertyOutputSettings)
	PCGEX_FWD(SampledPropertyOutputs)

	// Roll up "any output configured?" across all output families so the warning only fires
	// when the node would genuinely produce nothing.
	bool bAnyEntryOrGrammarOutput = false;
#define PCGEX_LOAD_PROP_FIELD_BOOT(_NAME) \
	if (Settings->bWrite##_NAME) \
	{ \
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->_NAME##AttributeName) \
		bAnyEntryOrGrammarOutput = true; \
	}

	PCGEX_LOAD_PROP_FIELD_BOOT(AssetPath)
	PCGEX_LOAD_PROP_FIELD_BOOT(Weight)
	PCGEX_LOAD_PROP_FIELD_BOOT(Category)
	PCGEX_LOAD_PROP_FIELD_BOOT(Extents)
	PCGEX_LOAD_PROP_FIELD_BOOT(BoundsMin)
	PCGEX_LOAD_PROP_FIELD_BOOT(BoundsMax)
	PCGEX_LOAD_PROP_FIELD_BOOT(CollectionType)
	PCGEX_LOAD_PROP_FIELD_BOOT(Symbol)
	PCGEX_LOAD_PROP_FIELD_BOOT(Size)
	PCGEX_LOAD_PROP_FIELD_BOOT(Scalable)
	PCGEX_LOAD_PROP_FIELD_BOOT(DebugColor)

#undef PCGEX_LOAD_PROP_FIELD_BOOT

	bool bAnySampledOutput = false;
	for (const FPCGExPropertySampledOutputConfig& Config : Context->SampledPropertyOutputs)
	{
		if (Config.IsValid())
		{
			bAnySampledOutput = true;
			break;
		}
	}

	if (!Context->PropertyOutputSettings.HasOutputs() && !bAnySampledOutput && !Settings->bWriteEntryTags && !bAnyEntryOrGrammarOutput)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("No property outputs configured."));
	}

	if (Settings->bWriteEntryTags)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->EntryTagsAttributeName)
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
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			return false;
		}

		// Sampled-output writers and time getters must exist before the parallel pass below:
		// the getters' readable buffers only get fetched by the per-scope Fetch calls if they
		// are registered on the facade beforehand.
		PrepareSampledPropertyCaches();

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
			if (!PointFilterCache[Index])
			{
				continue;
			}

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

		// Flatten collections from the unpacker into a search order, once.
		TArray<const UPCGExAssetCollection*> SearchOrder;
		SearchOrder.Reserve(Context->CollectionPickUnpacker->GetCollections().Num());
		for (const auto& CollectionPair : Context->CollectionPickUnpacker->GetCollections())
		{
			if (CollectionPair.Value)
			{
				SearchOrder.Add(CollectionPair.Value);
			}
		}

		TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
		Context->PropertyOutputSettings.GetEffectiveConfigs(EffectiveConfigs);

		for (const FPCGExPropertyOutputConfig& Config : EffectiveConfigs)
		{
			if (!Config.IsValid())
			{
				continue;
			}

			const FName OutputName = Config.GetEffectiveOutputName();
			const FName PropName = Config.PropertyName;

			const FInstancedStruct* Prototype = PCGExCollections::FindPrototypeProperty(PropName, SearchOrder);
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
				FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
				if (!Result.IsValid())
				{
					continue;
				}

				if (const FInstancedStruct* Source = PCGExCollections::ResolveEntrySourceProperty(Result.Entry, Result.Host, PropName))
				{
					if (const FPCGExProperty* SourceProp = Source->GetPtr<FPCGExProperty>())
					{
						Cache.SourceByHash.Add(Hash, SourceProp);
					}
				}
			}
		}
	}

	void FProcessor::PrepareSampledPropertyCaches()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::PrepareSampledPropertyCaches);

		if (Context->SampledPropertyOutputs.IsEmpty())
		{
			return;
		}

		// Flatten collections from the unpacker into a search order, once.
		TArray<const UPCGExAssetCollection*> SearchOrder;
		SearchOrder.Reserve(Context->CollectionPickUnpacker->GetCollections().Num());
		for (const auto& CollectionPair : Context->CollectionPickUnpacker->GetCollections())
		{
			if (CollectionPair.Value)
			{
				SearchOrder.Add(CollectionPair.Value);
			}
		}

		SampledPropertyCaches.Reserve(Context->SampledPropertyOutputs.Num());

		// Sampled configs may legitimately repeat a property (different times), so effective
		// output names can collide -- and colliding caches would share one facade buffer
		// (GetWritable is identifier-keyed) and silently clobber each other. First wins.
		TSet<FName> SeenOutputNames;
		SeenOutputNames.Reserve(Context->SampledPropertyOutputs.Num());

		for (const FPCGExPropertySampledOutputConfig& Config : Context->SampledPropertyOutputs)
		{
			if (!Config.IsValid())
			{
				continue;
			}

			const FName OutputName = Config.GetEffectiveOutputName();
			const FName PropName = Config.PropertyName;

			bool bNameAlreadyUsed = false;
			SeenOutputNames.Add(OutputName, &bNameAlreadyUsed);
			if (bNameAlreadyUsed)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Sampled output '{0}' (property '{1}') collides with a previous sampled output attribute name, skipping. Set distinct Output Attribute Names when sampling the same property several times."),
					           FText::FromName(OutputName), FText::FromName(PropName)));
				continue;
			}

			const FInstancedStruct* Prototype = PCGExCollections::FindPrototypeProperty(PropName, SearchOrder);
			const FPCGExProperty* PrototypeProp = Prototype ? Prototype->GetPtr<FPCGExProperty>() : nullptr;
			if (!PrototypeProp)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Sampled property '{0}' not found in any staged collection, skipping."),
					           FText::FromName(PropName)));
				continue;
			}

			if (!PrototypeProp->SupportsSampling())
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Property '{0}' ({1}) does not support sampling, skipping."),
					           FText::FromName(PropName), FText::FromName(PrototypeProp->GetTypeName())));
				continue;
			}

			TSharedPtr<PCGExData::TBuffer<double>> Writer = PointDataFacade->GetWritable<double>(
				OutputName, 0.0, true, PCGExData::EBufferInit::New);
			if (!Writer)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Failed to initialize sampled output buffer for property '{0}', skipping."),
					           FText::FromName(PropName)));
				continue;
			}

			TSharedPtr<PCGExDetails::TSettingValue<double>> TimeGetter = Config.Time.GetValueSetting();
			if (!TimeGetter->Init(PointDataFacade))
			{
				// Init logs the missing-attribute error itself.
				continue;
			}

			FSampledPropertyCache& Cache = SampledPropertyCaches.AddDefaulted_GetRef();
			Cache.PropertyName = PropName;
			Cache.TimeGetter = TimeGetter;
			Cache.Writer = Writer;
		}
	}

	void FProcessor::BuildSampledPropertySources()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::BuildSampledPropertySources);

		int16 MaterialPick = 0;
		for (FSampledPropertyCache& Cache : SampledPropertyCaches)
		{
			Cache.SourceByHash.Reserve(UniqueEntryHashes.Num());

			for (const uint64 Hash : UniqueEntryHashes)
			{
				FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
				if (!Result.IsValid())
				{
					continue;
				}

				if (const FInstancedStruct* Source = PCGExCollections::ResolveEntrySourceProperty(Result.Entry, Result.Host, Cache.PropertyName))
				{
					const FPCGExProperty* SourceProp = Source->GetPtr<FPCGExProperty>();
					if (SourceProp && SourceProp->SupportsSampling())
					{
						Cache.SourceByHash.Add(Hash, SourceProp);
					}
				}
			}
		}
	}

	void FProcessor::BuildEntryTagsCache()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::BuildEntryTagsCache);

		EntryTagsWriter = PointDataFacade->GetWritable<FString>(
			Settings->EntryTagsAttributeName, FString(), false, PCGExData::EBufferInit::New);
		if (!EntryTagsWriter)
		{
			return;
		}

		const FString& Separator = Settings->EntryTagsSeparator;
		EntryTagsByHash.Reserve(UniqueEntryHashes.Num());

		int16 MaterialPick = 0;
		for (const uint64 Hash : UniqueEntryHashes)
		{
			FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid() || !Result.Entry || Result.Entry->Tags.IsEmpty())
			{
				continue;
			}

			FString Joined;
			for (const FName& Tag : Result.Entry->Tags)
			{
				if (!Joined.IsEmpty())
				{
					Joined += Separator;
				}
				Joined += Tag.ToString();
			}
			EntryTagsByHash.Add(Hash, MoveTemp(Joined));
		}
	}

	void FProcessor::BuildEntryFieldsCache()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadProperties::BuildEntryFieldsCache);

#define PCGEX_LOAD_PROP_FIELD_INIT(_NAME, _TYPE, _DEFAULT, _GETTER) \
		if (Settings->bWrite##_NAME) \
		{ \
			_NAME##Writer = PointDataFacade->GetWritable<_TYPE>(Settings->_NAME##AttributeName, _DEFAULT, false, PCGExData::EBufferInit::New); \
			if (_NAME##Writer) { _NAME##ByHash.Reserve(UniqueEntryHashes.Num()); } \
		}
		PCGEX_FOREACH_ENTRY_DATA_FIELD(PCGEX_LOAD_PROP_FIELD_INIT)
#undef PCGEX_LOAD_PROP_FIELD_INIT

		const bool bWantGrammar =
			(Settings->OutputAxes != 0) &&
			(Settings->bWriteSymbol || Settings->bWriteSize || Settings->bWriteScalable || Settings->bWriteDebugColor);

		// Pre-resolve each hash once and union the effective axes; per-axis writer allocation
		// depends on which axes actually contribute output.
		struct FResolved
		{
			uint64 Hash = 0;
			const FPCGExAssetCollectionEntry* Entry = nullptr;
			const UPCGExAssetCollection* Host = nullptr;
			const FPCGExAssetGrammarDetails* Grammar = nullptr;
		};

		TArray<FResolved> Resolved;
		Resolved.Reserve(UniqueEntryHashes.Num());
		uint8 LocalUsedAxes = 0;
		{
			int16 MaterialPick = 0;
			for (const uint64 Hash : UniqueEntryHashes)
			{
				FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
				if (!Result.IsValid() || !Result.Entry)
				{
					continue;
				}

				FResolved& R = Resolved.AddDefaulted_GetRef();
				R.Hash = Hash;
				R.Entry = Result.Entry;
				R.Host = Result.Host;
				if (bWantGrammar)
				{
					R.Grammar = Result.Entry->GetEffectiveGrammar(Result.Host);
					if (R.Grammar)
					{
						LocalUsedAxes |= (R.Grammar->Axes & Settings->OutputAxes);
					}
				}
			}
		}

#define PCGEX_LOAD_PROP_FIELD_INIT_SHARED(_NAME, _TYPE, _DEFAULT, _GETTER) \
		if (Settings->bWrite##_NAME) \
		{ \
			_NAME##Writer = PointDataFacade->GetWritable<_TYPE>(Settings->_NAME##AttributeName, _DEFAULT, false, PCGExData::EBufferInit::New); \
			if (_NAME##Writer) { _NAME##ByHash.Reserve(Resolved.Num()); } \
		}
		PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(PCGEX_LOAD_PROP_FIELD_INIT_SHARED)
#undef PCGEX_LOAD_PROP_FIELD_INIT_SHARED

		// Single-axis output drops the _X/_Y/_Z suffix to preserve the legacy attribute shape
		// (override with bAlwaysSuffixAxes).
		const bool bSuppressSuffix = (PCGExGrammarAxes::CountAxes(LocalUsedAxes) <= 1) && !Settings->bAlwaysSuffixAxes;
#define PCGEX_LOAD_PROP_FIELD_INIT_PERAXIS(_NAME, _TYPE, _DEFAULT, _GETTER) \
		if (Settings->bWrite##_NAME) \
		{ \
			for (int32 _a = 0; _a < 3; _a++) \
			{ \
				if (!(LocalUsedAxes & static_cast<uint8>(PCGExGrammarAxes::Bits[_a]))) { continue; } \
				const FName _AttrName = PCGExGrammarAxes::MakeAxisAttributeName(Settings->_NAME##AttributeName, _a, bSuppressSuffix); \
				_NAME##Writer[_a] = PointDataFacade->GetWritable<_TYPE>(_AttrName, _DEFAULT, false, PCGExData::EBufferInit::New); \
				if (_NAME##Writer[_a]) \
				{ \
					_NAME##ByHash[_a].Reserve(Resolved.Num()); \
					_NAME##ActiveAxes.Add(_a); \
				} \
			} \
		}
		PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(PCGEX_LOAD_PROP_FIELD_INIT_PERAXIS)
#undef PCGEX_LOAD_PROP_FIELD_INIT_PERAXIS

		if (!HasAnyEntryFieldWriter())
		{
			return;
		}

		FPCGExGrammarSizeCache SizeCache;
		if (Settings->bWriteSize)
		{
			SizeCache.Reserve(Resolved.Num() * PCGExGrammarAxes::CountAxes(LocalUsedAxes));
		}

		for (const FResolved& R : Resolved)
		{
			const uint64 Hash = R.Hash;
			const FPCGExAssetCollectionEntry* Entry = R.Entry;
			const UPCGExAssetCollection* Host = R.Host;

#define PCGEX_LOAD_PROP_FIELD_FILL_ENTRY(_NAME, _TYPE, _DEFAULT, _GETTER) \
			if (_NAME##Writer) { _NAME##ByHash.Add(Hash, _GETTER); }
			PCGEX_FOREACH_ENTRY_DATA_FIELD(PCGEX_LOAD_PROP_FIELD_FILL_ENTRY)
#undef PCGEX_LOAD_PROP_FIELD_FILL_ENTRY

			const FPCGExAssetGrammarDetails* Grammar = R.Grammar;
			if (!Grammar || Grammar->Axes == 0)
			{
				continue;
			}

#define PCGEX_LOAD_PROP_FIELD_FILL_SHARED(_NAME, _TYPE, _DEFAULT, _GETTER) \
			if (_NAME##Writer) { _NAME##ByHash.Add(Hash, _GETTER); }
			PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(PCGEX_LOAD_PROP_FIELD_FILL_SHARED)
#undef PCGEX_LOAD_PROP_FIELD_FILL_SHARED

			// Dispatch directly through Grammar-> rather than Entry::FixModuleInfos to skip a redundant resolve.
			const uint8 EntryAxes = Grammar->Axes & Settings->OutputAxes;
			const bool bIsSub = Entry->bIsSubCollection;
			for (int32 a = 0; a < 3; a++)
			{
				if (!(EntryAxes & static_cast<uint8>(PCGExGrammarAxes::Bits[a])))
				{
					continue;
				}
				FPCGSubdivisionSubmodule Module;
				const bool bFixed = bIsSub
					? Grammar->FixSubCollection(Entry->SubCollection, PCGExGrammarAxes::Bits[a], Module, Settings->bWriteSize ? &SizeCache : nullptr)
					: Grammar->FixLeaf(Entry->Staging.Bounds, PCGExGrammarAxes::Bits[a], Module);
				if (!bFixed)
				{
					continue;
				}

#define PCGEX_LOAD_PROP_FIELD_FILL_PERAXIS(_NAME, _TYPE, _DEFAULT, _GETTER) \
				if (_NAME##Writer[a]) { _NAME##ByHash[a].Add(Hash, _GETTER); }
				PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(PCGEX_LOAD_PROP_FIELD_FILL_PERAXIS)
#undef PCGEX_LOAD_PROP_FIELD_FILL_PERAXIS
			}
		}
	}

	bool FProcessor::HasAnyEntryFieldWriter() const
	{
#define PCGEX_LOAD_PROP_FIELD_HAS_WRITER(_NAME, _TYPE, _DEFAULT, _GETTER) if (_NAME##Writer) { return true; }
		PCGEX_FOREACH_ENTRY_DATA_FIELD(PCGEX_LOAD_PROP_FIELD_HAS_WRITER)
		PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(PCGEX_LOAD_PROP_FIELD_HAS_WRITER)
#undef PCGEX_LOAD_PROP_FIELD_HAS_WRITER

#define PCGEX_LOAD_PROP_FIELD_HAS_WRITER_PERAXIS(_NAME, _TYPE, _DEFAULT, _GETTER) if (!_NAME##ActiveAxes.IsEmpty()) { return true; }
		PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(PCGEX_LOAD_PROP_FIELD_HAS_WRITER_PERAXIS)
#undef PCGEX_LOAD_PROP_FIELD_HAS_WRITER_PERAXIS
		return false;
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
		BuildSampledPropertySources();

		if (Settings->bWriteEntryTags)
		{
			BuildEntryTagsCache();
		}

		BuildEntryFieldsCache();

		if (PropertyCaches.IsEmpty() && SampledPropertyCaches.IsEmpty() && !EntryTagsWriter && !HasAnyEntryFieldWriter())
		{
			// No valid outputs of any kind
			bIsProcessorValid = false;
			return;
		}

#define PCGEX_LOAD_PROP_FIELD_WRITE(_NAME, _TYPE, _DEFAULT, _GETTER) if (_NAME##Writer) { if (const _TYPE* Cached = _NAME##ByHash.Find(Hash)) { _NAME##Writer->SetValue(i, *Cached); } }
#define PCGEX_LOAD_PROP_FIELD_WRITE_PERAXIS(_NAME, _TYPE, _DEFAULT, _GETTER) \
		for (const int32 _a : _NAME##ActiveAxes) { \
			if (const _TYPE* Cached = _NAME##ByHash[_a].Find(Hash)) { _NAME##Writer[_a]->SetValue(i, *Cached); } \
		}

		PCGExMT::ParallelOrSequential(
			PointDataFacade->GetNum(),
			[&](const int32 i)
			{
				if (!PointFilterCache[i])
				{
					return;
				}

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

				// Sampled outputs: read the per-point time, sample the entry's source property.
				// Unmatched hashes leave the buffer default (0.0), same as the raw-output path.
				for (const FSampledPropertyCache& Cache : SampledPropertyCaches)
				{
					if (const FPCGExProperty* const* SourcePtr = Cache.SourceByHash.Find(Hash))
					{
						Cache.Writer->SetValue(i, (*SourcePtr)->SampleAt(Cache.TimeGetter->Read(i)));
					}
				}

				if (EntryTagsWriter)
				{
					if (const FString* Joined = EntryTagsByHash.Find(Hash))
					{
						EntryTagsWriter->SetValue(i, *Joined);
					}
				}

				PCGEX_FOREACH_ENTRY_DATA_FIELD(PCGEX_LOAD_PROP_FIELD_WRITE)
				PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(PCGEX_LOAD_PROP_FIELD_WRITE)
				PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(PCGEX_LOAD_PROP_FIELD_WRITE_PERAXIS)
			});

#undef PCGEX_LOAD_PROP_FIELD_WRITE
#undef PCGEX_LOAD_PROP_FIELD_WRITE_PERAXIS

		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
