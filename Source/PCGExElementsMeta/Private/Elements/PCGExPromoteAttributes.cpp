// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPromoteAttributes.h"

#include <type_traits>

#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
#include "PCGExVersion.h"
#include "Details/PCGExAttributesDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGContext.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExMTCommon.h"
#include "Core/PCGExPickerFactoryProvider.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExSubSelection.h"
#include "Factories/PCGExFactories.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Types/PCGExTypeOpsImpl.h"

#define LOCTEXT_NAMESPACE "PCGExAttributesToTagsElement"
#define PCGEX_NAMESPACE AttributesToTags

UPCGExAttributesToTagsSettings::UPCGExAttributesToTagsSettings()
{
	// A fresh node promotes nothing until names are listed.
	AttributeFilter.FilterMode = EPCGExAttributeFilter::Include;
}

#if WITH_EDITOR
void UPCGExAttributesToTagsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 8)
	{
		PCGExDeprecation::RenameShorthandOverridePin(
			this, InOutNode, FName(TEXT("CommaSeparatedAttributeSelectors")),
			GET_MEMBER_NAME_CHECKED(UPCGExAttributesToTagsSettings, AttributeFilter),
			GET_MEMBER_NAME_CHECKED(FPCGExNameFiltersDetails, CommaSeparatedNames));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExAttributesToTagsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 1)
	{
		// Behavior-preserving: each legacy selector becomes a non-remapped mapping (output keeps the source name).
		PCGExAttributeMigration::AppendMappingsFromSelectors(Attributes_DEPRECATED, AttributeMappings);
		Attributes_DEPRECATED.Empty();
	}

	PCGEX_IF_VERSION_LOWER(1, 78, 8)
	{
		PCGExPromoteAttributes::MigrateToNameFilter(CommaSeparatedAttributeSelectors_DEPRECATED, AttributeMappings, AttributeFilter);
		CommaSeparatedAttributeSelectors_DEPRECATED.Empty();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

TArray<FText> UPCGExAttributesToTagsSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Hoist Attributes")};
}
#endif

TArray<FPCGPinProperties> UPCGExAttributesToTagsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "The data to be processed.", Required)

	if (Resolution != EPCGExAttributeToTagsResolution::Self)
	{
		PCGEX_PIN_ANY(FName("Tags Source"), "Source collection(s) to read the tags from.", Required)
	}

	if (Selection == EPCGExCollectionEntrySelection::Picker || Selection == EPCGExCollectionEntrySelection::PickerFirst || Selection == EPCGExCollectionEntrySelection::PickerLast)
	{
		PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "Pickers config", Required, FPCGExDataTypeInfoPicker::AsId())
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExAttributesToTagsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (Action != EPCGExAttributeToTagsAction::Attribute)
	{
		PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "The processed input.", Normal)
	}
	else
	{
		PCGEX_PIN_PARAMS(FName("Tags"), "Tags value in the format `AttributeName = AttributeName:AttributeValue`", Required)
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(AttributesToTags)

namespace PCGExPromoteAttributes
{
	// Filter a pin's inputs to the non-null, non-empty ones, preserving pin order. Each entry copies the
	// FPCGTaggedData by value, so the result is self-contained -- safe to outlive the temporary TArray that
	// GetInputsByPin returns.
	void GatherValidInputs(const TArray<FPCGTaggedData>& Inputs, TArray<FPCGExAttributesToTagsInput>& OutValid)
	{
		OutValid.Reserve(Inputs.Num());
		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			if (!TaggedData.Data) { continue; }
			const int32 NumRows = PCGExMetaHelpers::GetElementsCount(TaggedData.Data);
			if (NumRows > 0) { OutValid.Add({TaggedData, NumRows}); }
		}
	}

	// Build the tag-detail reader for one source (shared by every resolution). Returns false if broadcasters can't init.
	bool InitDetails(const FPCGExContext* InContext, const bool bPrefixWithAttributeName, const TArray<FPCGExAttributeSourceToTargetDetails>& Mappings, const UPCGData* SourceData, FPCGExAttributeToTagDetails& OutDetails)
	{
		OutDetails.bAddIndexTag = false;
		OutDetails.bPrefixWithAttributeName = bPrefixWithAttributeName;
		OutDetails.AttributeMappings = Mappings;
		return OutDetails.Init(InContext, SourceData);
	}

	// Promote every picked row into the tag target (tag set or @Data metadata); the shared row->Tag loop. Tag() only reads the row index.
	template <typename TTarget>
	void PromoteAll(const FPCGExAttributeToTagDetails& Details, const TArray<int32>& Indices, TTarget&& Target)
	{
		PCGExData::FConstPoint TagSource;
		for (const int32 Idx : Indices)
		{
			TagSource.Index = Idx;
			Details.Tag(TagSource, Target);
		}
	}

	// The output name a mapping asks for, or None to keep the source's own. A remap with an empty target keeps it too,
	// as FPCGExAttributeToTagDetails does on the element path.
	FName GetRemappedName(const FPCGExAttributeSourceToTargetDetails& InMapping)
	{
		return InMapping.WantsRemappedOutput() ? InMapping.GetOutputName() : NAME_None;
	}

	// @Data-domain selectors carry a single value per data (not per-row), and the point-centric broadcaster
	// doesn't read them off raw data -- so handle them here via TryReadDataValue. Applied once per source,
	// mirroring FPCGExAttributeToTagDetails::Tag's formatting so @Data and element tags look identical.
	void PromoteDataDomainToTags(FPCGExContext* InContext, const UPCGData* SourceData, const TArray<FPCGExAttributeSourceToTargetDetails>& DataMappings, const bool bPrefixWithAttributeName, TSet<FString>& OutTags)
	{
		for (const FPCGExAttributeSourceToTargetDetails& Mapping : DataMappings)
		{
			const FPCGAttributePropertyInputSelector Selector = Mapping.GetSourceSelector();

			EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
			if (!PCGExData::TryGetType(Selector, SourceData, Type)) { continue; }

			// A remap wins; otherwise the source's resolved attribute name.
			const FName RemappedName = GetRemappedName(Mapping);
			const FName OutputName = RemappedName.IsNone() ? PCGExMetaHelpers::GetAttributeIdentifier(Selector, SourceData).Name : RemappedName;

			PCGExMetaHelpers::ExecuteWithRightType(Type, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				T Value{};
				if (!PCGExData::Helpers::TryReadDataValue<T>(InContext, SourceData, Selector, Value, /*bQuiet=*/true)) { return; }

				// Same formatting as the broadcaster (element) path, so @Data and element tags are identical.
				FPCGExAttributeToTagDetails::AppendValueTag<T>(OutputName, Value, bPrefixWithAttributeName, OutTags);
			});
		}
	}

	// @Data variant that writes each promoted value as a @Data attribute on the output metadata.
	void PromoteDataDomainToMetadata(FPCGExContext* InContext, const UPCGData* SourceData, const TArray<FPCGExAttributeSourceToTargetDetails>& DataMappings, UPCGMetadata* OutMetadata)
	{
		for (const FPCGExAttributeSourceToTargetDetails& Mapping : DataMappings)
		{
			const FPCGAttributePropertyInputSelector Selector = Mapping.GetSourceSelector();

			EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
			if (!PCGExData::TryGetType(Selector, SourceData, Type)) { continue; }

			// Keep the source's resolved identifier (preserves the @Data domain); only swap the name when remapping.
			FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(Selector, SourceData);
			if (const FName RemappedName = GetRemappedName(Mapping); !RemappedName.IsNone()) { Identifier.Name = RemappedName; }

			PCGExMetaHelpers::ExecuteWithRightType(Type, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				T Value{};
				if (!PCGExData::Helpers::TryReadDataValue<T>(InContext, SourceData, Selector, Value, /*bQuiet=*/true)) { return; }

				OutMetadata->DeleteAttribute(Identifier);
				OutMetadata->FindOrCreateAttribute<T>(Identifier, Value);
			});
		}
	}

	// Resolve the row indices to promote per selection mode (indices into the source's own row count). Picker emits all
	// valid picks ascending; PickerFirst/Last emit the lowest/highest of them.
	void ResolveIndices(
		const EPCGExCollectionEntrySelection Selection,
		const int32 SourceNum,
		const int32 Seed,
		const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& PickerFactories,
		TArray<int32>& OutIndices)
	{
		if (SourceNum <= 0) { return; }

		switch (Selection)
		{
		case EPCGExCollectionEntrySelection::FirstIndex:
			OutIndices.Add(0);
			break;
		case EPCGExCollectionEntrySelection::LastIndex:
			OutIndices.Add(SourceNum - 1);
			break;
		case EPCGExCollectionEntrySelection::RandomIndex:
			{
				const FRandomStream RandomSource(Seed);
				OutIndices.Add(RandomSource.RandRange(0, SourceNum - 1));
			}
			break;
		case EPCGExCollectionEntrySelection::Picker:
		case EPCGExCollectionEntrySelection::PickerFirst:
		case EPCGExCollectionEntrySelection::PickerLast:
			{
				TArray<int32> Picks;
				if (!PCGExPickers::GetSortedPicks(PickerFactories, SourceNum, Picks)) { break; }

				if (Selection == EPCGExCollectionEntrySelection::Picker) { OutIndices.Append(Picks); }
				else { OutIndices.Add(Selection == EPCGExCollectionEntrySelection::PickerFirst ? Picks[0] : Picks.Last()); }
			}
			break;
		default:
			checkNoEntry();
			break;
		}
	}

	void ResolveMappings(const UPCGData* InData, const FPCGExAttributesToTagsMappings& InExplicit, const FPCGExNameFiltersDetails& InFilter, FPCGExAttributesToTagsMappings& OutMappings)
	{
		OutMappings = InExplicit;

		const UPCGMetadata* Metadata = InData ? InData->ConstMetadata() : nullptr;
		if (!Metadata) { return; }

		// Default resolves to the domain it aliases on this data, so a plain row and the attribute it reads compare equal.
		auto ResolveDomain = [Metadata](const FPCGMetadataDomainID& InDomainID)
		{
			const FPCGMetadataDomain* Domain = Metadata->GetConstMetadataDomain(InDomainID);
			return Domain ? Domain->GetDomainID() : InDomainID;
		};

		// The attributes the explicit rows read: the filter never promotes them a second time.
		TSet<FPCGAttributeIdentifier> Claimed;
		auto Claim = [&](const FPCGExAttributeSourceToTargetDetails& InMapping)
		{
			const FPCGAttributePropertyInputSelector Selector = InMapping.GetSourceSelector().CopyAndFixLast(InData);
			if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute) { return; }
			Claimed.Add(FPCGAttributeIdentifier(Selector.GetAttributeName(), ResolveDomain(InData->GetMetadataDomainIDFromSelector(Selector))));
		};

		for (const FPCGExAttributeSourceToTargetDetails& Mapping : InExplicit.Element) { Claim(Mapping); }
		for (const FPCGExAttributeSourceToTargetDetails& Mapping : InExplicit.Data) { Claim(Mapping); }

		const FPCGMetadataDomainID DefaultDomainID = ResolveDomain(PCGMetadataDomainID::Default);

		TArray<PCGExData::FAttributeIdentity> Identities;
		PCGExData::FAttributeIdentity::Get(Metadata, Identities);

		for (const PCGExData::FAttributeIdentity& Identity : Identities)
		{
			if (!InFilter.Test(Identity.Name.ToString()) || Claimed.Contains(Identity.GetIdentifier())) { continue; }

			if (Identity.InDataDomain())
			{
				const FPCGAttributePropertyInputSelector Selector = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(Identity.Name, PCGDataConstants::DataDomainName);
				OutMappings.Data.Emplace(PCGExTypeOps::MakeNameChecked(Selector.ToString()));
			}
			else if (Identity.MetadataDomain == DefaultDomainID)
			{
				OutMappings.Element.Emplace(Identity.Name);
			}
		}
	}

#if WITH_EDITOR
	void MigrateToNameFilter(const FString& InCommaSeparatedSelectors, TArray<FPCGExAttributeSourceToTargetDetails>& InOutMappings, FPCGExNameFiltersDetails& InOutFilter)
	{
		// Set explicitly: a migrated node must never inherit the default mode, whatever it is.
		InOutFilter.FilterMode = EPCGExAttributeFilter::Include;

		bool bNamesPCGExData = false;

		InOutMappings.RemoveAll([&](const FPCGExAttributeSourceToTargetDetails& InMapping)
		{
			if (InMapping.Source.IsNone() || InMapping.WantsRemappedOutput()) { return false; }

			// A valid attribute name holds no domain, $property, sub-field or reserved '@' name, so the filter matches it as-is.
			const FString Name = InMapping.Source.ToString();
			if (!FPCGMetadataAttributeBase::IsValidName(Name)) { return false; }

			InOutFilter.Matches.Add(Name, EPCGExStringMatchMode::Equals);
			bNamesPCGExData |= Name.StartsWith(PCGExCommon::PCGExPrefix);
			return true;
		});

		TArray<FString> PlainTokens;
		for (const FString& Token : PCGExArrayHelpers::GetStringArrayFromCommaSeparatedList(InCommaSeparatedSelectors))
		{
			if (FPCGMetadataAttributeBase::IsValidName(Token))
			{
				PlainTokens.Add(Token);
				bNamesPCGExData |= Token.StartsWith(PCGExCommon::PCGExPrefix);
			}
			else
			{
				// The row the comma-separated list used to resolve to: never remapped.
				InOutMappings.Emplace(PCGExTypeOps::MakeNameChecked(Token));
			}
		}

		// Kept in the overridable string, so an override still replaces them.
		InOutFilter.CommaSeparatedNames = FString::Join(PlainTokens, TEXT(","));

		// The old list promoted the PCGEx attributes it named; with Ex on, the filter would skip them.
		if (bNamesPCGExData) { InOutFilter.bPreservePCGExData = false; }
	}
#endif
}

bool FPCGExAttributesToTagsElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(AttributesToTags)

	// Explicit sources, the same for every source data. @Data sources read a single value via TryReadDataValue, the
	// rest go through the per-row broadcaster. IsDataDomainAttribute resolves the selector, so $/@ syntax in Source is honored.
	FPCGExAttributesToTagsMappings Explicit;
	auto AddExplicit = [&](const FPCGExAttributeSourceToTargetDetails& Mapping)
	{
		if (Mapping.Source.IsNone()) { return; }
		if (PCGExMetaHelpers::IsDataDomainAttribute(Mapping.GetSourceSelector())) { Explicit.Data.Add(Mapping); }
		else { Explicit.Element.Add(Mapping); }
	};

	for (const FPCGExAttributeSourceToTargetDetails& Mapping : Settings->AttributeMappings) { AddExplicit(Mapping); }

	// Schema assets contribute their property names as sources, never remapped.
	TArray<FPCGExPropertyResolved> Resolved;
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& SchemaAsset : Settings->IncludedSchemas)
	{
		if (!SchemaAsset) { continue; }
		SchemaAsset->Collection.Resolve(Resolved);
		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (!Entry.Source || Entry.Source->Name.IsNone()) { continue; }
			AddExplicit(FPCGExAttributeSourceToTargetDetails(Entry.Source->Name));
		}
	}

	// Init merges the comma-separated names into Matches, so it runs on a copy.
	FPCGExNameFiltersDetails AttributeFilter = Settings->AttributeFilter;
	AttributeFilter.Init();

	// Pickers are valid for every resolution -- including Self -- so load them before the Self short-circuit below.
	if (Settings->Selection == EPCGExCollectionEntrySelection::Picker || Settings->Selection == EPCGExCollectionEntrySelection::PickerFirst || Settings->Selection == EPCGExCollectionEntrySelection::PickerLast)
	{
		if (!PCGExFactories::GetInputFactories(Context, PCGExPickers::Labels::SourcePickersLabel, Context->PickerFactories, {FPCGExDataTypeInfoPicker::AsId()}))
		{
			return false;
		}
	}

	// Gather valid main inputs once (reused by AdvanceWork for every resolution; GetInputsByPin returns a
	// temporary, but GatherValidInputs copies each entry by value).
	PCGExPromoteAttributes::GatherValidInputs(Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel), Context->ValidMains);

	if (Settings->Resolution == EPCGExAttributeToTagsResolution::Self)
	{
		// Self reads each input from itself, so each input resolves its own mappings.
		for (FPCGExAttributesToTagsInput& Main : Context->ValidMains) { PCGExPromoteAttributes::ResolveMappings(Main.Tagged.Data, Explicit, AttributeFilter, Main.Mappings); }
		return true;
	}

	// Cross-collection: resolve the "Tags Source" inputs (non-null, non-empty), preserving pin order.
	TArray<FPCGExAttributesToTagsInput> ValidSources;
	PCGExPromoteAttributes::GatherValidInputs(Context->InputData.GetInputsByPin(FName("Tags Source")), ValidSources);

	if (ValidSources.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Source collections are empty."));
		return false;
	}

	int32 NumIterations = 0;

	if (Settings->Resolution == EPCGExAttributeToTagsResolution::CollectionToCollection)
	{
		if (ValidSources.Num() != Context->ValidMains.Num())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Number of input collections don't match the number of sources."));
			return false;
		}

		NumIterations = ValidSources.Num();
	}
	else
	{
		if (ValidSources.Num() != 1 && !Settings->bQuietTooManyCollectionsWarning)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("More that one collections found in the sources, only the first one will be used."));
		}

		NumIterations = 1;
	}

	// Store sources (data + cached row count) paired with their readers, indexed the same as ValidMains for
	// CollectionToCollection.
	Context->Sources.Reserve(NumIterations);
	Context->Details.Reserve(NumIterations);
	for (int i = 0; i < NumIterations; i++)
	{
		FPCGExAttributesToTagsInput& Source = Context->Sources.Add_GetRef(ValidSources[i]);
		PCGExPromoteAttributes::ResolveMappings(Source.Tagged.Data, Explicit, AttributeFilter, Source.Mappings);

		FPCGExAttributeToTagDetails& Details = Context->Details.Emplace_GetRef();
		if (!PCGExPromoteAttributes::InitDetails(Context, Settings->bPrefixWithAttributeName, Source.Mappings.Element, Source.Tagged.Data, Details))
		{
			return false;
		}
	}

	return true;
}

bool FPCGExAttributesToTagsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributesToTagsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AttributesToTags)
	PCGEX_EXECUTION_CHECK

	// Valid main inputs were gathered once in Boot (with cached row counts).
	const TArray<FPCGExAttributesToTagsInput>& ValidMains = Context->ValidMains;
	if (ValidMains.IsEmpty())
	{
		return Context->CancelExecution(TEXT("Could not find any points to process."));
	}

	// Each input produces exactly one output. Build them in parallel (the per-input work is independent and
	// ManagedObjects / accessor creation / TryReadDataValue are all thread-safe), into a pre-sized array, then
	// stage in input order afterward so output order stays deterministic.
	struct FPendingOutput
	{
		UPCGData* Data = nullptr;
		FName Pin = NAME_None;
		TSet<FString> Tags;
	};
	TArray<FPendingOutput> Outputs;
	Outputs.SetNum(ValidMains.Num());

	PCGExMT::ParallelOrSequential(
		ValidMains.Num(), [&](const int32 i)
		{
			const FPCGTaggedData& MainTagged = ValidMains[i].Tagged;
			const UPCGData* MainData = MainTagged.Data;

			// Resolve the source (data, cached row count, mappings) and its reader for this input.
			const FPCGExAttributesToTagsInput* Source = nullptr;
			const FPCGExAttributeToTagDetails* Details = nullptr;
			FPCGExAttributeToTagDetails SelfDetails;

			switch (Settings->Resolution)
			{
			case EPCGExAttributeToTagsResolution::Self:
				// Self reads each input from itself, so the reader is per-input; source == main.
				Source = &ValidMains[i];
				if (!PCGExPromoteAttributes::InitDetails(Context, Settings->bPrefixWithAttributeName, Source->Mappings.Element, MainData, SelfDetails)) { return; }
				Details = &SelfDetails;
				break;
			case EPCGExAttributeToTagsResolution::CollectionToCollection:
				Source = &Context->Sources[i];
				Details = &Context->Details[i];
				break;
			case EPCGExAttributeToTagsResolution::EntryToCollection:
				Source = &Context->Sources[0];
				Details = &Context->Details[0];
				break;
			default:
				checkNoEntry();
				return;
			}

			const UPCGData* SourceData = Source->Tagged.Data;
			const TArray<FPCGExAttributeSourceToTargetDetails>& DataMappings = Source->Mappings.Data;

			TArray<int32> PickedIndices;
			PCGExPromoteAttributes::ResolveIndices(Settings->Selection, Source->NumRows, i, Context->PickerFactories, PickedIndices);

			FPendingOutput& Out = Outputs[i];

			switch (Settings->Action)
			{
			case EPCGExAttributeToTagsAction::AddTags:
				{
					// Forward the original data untouched; promoted tags merge with the input's own (round-tripped through FTags).
					PCGExData::FTags OutTags;
					OutTags.Append(MainTagged.Tags);

					TSet<FString> Promoted;
					PCGExPromoteAttributes::PromoteAll(*Details, PickedIndices, Promoted);
					PCGExPromoteAttributes::PromoteDataDomainToTags(Context, SourceData, DataMappings, Settings->bPrefixWithAttributeName, Promoted);
					OutTags.Append(Promoted);

					Out.Data = const_cast<UPCGData*>(MainData);
					Out.Pin = PCGPinConstants::DefaultOutputLabel;
					Out.Tags = OutTags.Flatten();
				}
				break;
			case EPCGExAttributeToTagsAction::Data:
				{
					// Duplicate the input and write the promoted values as @Data attributes on the copy.
					UPCGData* DupData = Context->ManagedObjects->DuplicateData<UPCGData>(MainData);
					if (!DupData) { return; }

					if (UPCGMetadata* Metadata = DupData->MutableMetadata())
					{
						PCGExPromoteAttributes::PromoteAll(*Details, PickedIndices, Metadata);
						PCGExPromoteAttributes::PromoteDataDomainToMetadata(Context, SourceData, DataMappings, Metadata);
					}

					PCGExData::FTags OutTags;
					OutTags.Append(MainTagged.Tags);

					Out.Data = DupData;
					Out.Pin = PCGPinConstants::DefaultOutputLabel;
					Out.Tags = OutTags.Flatten();
				}
				break;
			case EPCGExAttributeToTagsAction::Attribute:
				{
					// Emit one attribute set per input, carrying the promoted values as @Data attributes.
					UPCGParamData* OutputSet = Context->ManagedObjects->New<UPCGParamData>();
					OutputSet->Metadata->AddEntry();

					PCGExPromoteAttributes::PromoteAll(*Details, PickedIndices, OutputSet->Metadata);
					PCGExPromoteAttributes::PromoteDataDomainToMetadata(Context, SourceData, DataMappings, OutputSet->Metadata);

					Out.Data = OutputSet;
					Out.Pin = FName("Tags");
				}
				break;
			default:
				checkNoEntry();
				break;
			}
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	for (FPendingOutput& Out : Outputs)
	{
		if (Out.Data)
		{
			Context->StageOutput(Out.Data, Out.Pin, PCGExData::EStaging::None, Out.Tags);
		}
	}

	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
