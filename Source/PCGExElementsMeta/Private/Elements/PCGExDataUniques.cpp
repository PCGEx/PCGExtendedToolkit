// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDataUniques.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExHashHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGExDataUniquesElement"
#define PCGEX_NAMESPACE DataUniques

#pragma region UPCGExDataUniquesSettings

FPCGDataTypeIdentifier UPCGExDataUniquesSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (InPin->IsOutputPin() && InPin->Properties.Label == PCGExDataUniques::OutputUniquesLabel)
	{
		return FPCGDataTypeInfoParam::AsId();
	}
	return Super::GetCurrentPinTypesID(InPin);
}

TArray<FPCGPinProperties> UPCGExDataUniquesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Data carrying the key attributes on the @Data domain.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExDataUniquesSettings::OutputPinProperties() const
{
	// Pin order is load-bearing: AdvanceWork deactivates Uniques (bit 0) and Discarded (bit 2) by index.
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAMS(PCGExDataUniques::OutputUniquesLabel, "One row per unique combination of key values, carrying the representative input's @Data attributes (plus the identifier column).", Normal)
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "Inputs forwarded untouched, tagged with the identifier of the row they match when Write Identifier is on.", Normal)
	PCGEX_PIN_ANY(PCGExDataUniques::OutputDiscardedLabel, "Inputs missing a key attribute (or whose key has a container/extended type). Forwarded untouched, untagged.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(DataUniques)

#pragma endregion

namespace PCGExDataUniques
{
	struct FInputKey
	{
		bool bValid = false;
		bool bUnsupported = false; // A key exists but has a container/extended type.
		uint64 Hash = 0;
	};

	struct FBucket
	{
		uint64 Hash = 0;
		int32 RepresentativeInput = INDEX_NONE;
	};

	// Stable hash of the key count, then each key's type and value, in key order: the same values give the same
	// identifier on every session and platform. The type keeps int32 5 and int64 5 apart. False when a key is
	// missing or has a container/extended type. Read-only, safe to run in parallel.
	bool HashKeys(const FPCGExDataUniquesContext* Context, const UPCGData* InData, uint64& OutHash, bool& bOutUnsupported)
	{
		const UPCGMetadata* Metadata = InData->ConstMetadata();
		if (!Metadata)
		{
			return false;
		}

		uint64 Hash = PCGExHashHelpers::MixWord(PCGExHashHelpers::StableSeed, static_cast<uint64>(Context->KeyIdentifiers.Num()));

		for (const FPCGAttributeIdentifier& Identifier : Context->KeyIdentifiers)
		{
			// Quiet on a never-instantiated @Data domain, where UPCGMetadata::GetConstAttribute would error-log.
			const FPCGMetadataAttributeBase* Attribute = PCGExMetaHelpers::TryGetConstAttribute(Metadata, Identifier);
			if (!Attribute)
			{
				return false;
			}

			bool bHashed = false;

			PCGExMetaHelpers::ExecuteWithRightType(
				Attribute,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					Hash = PCGExHashHelpers::MixWord(Hash, static_cast<uint64>(Attribute->GetTypeId()));
					Hash = PCGExHashHelpers::MixValue<T>(Hash, PCGExData::Helpers::ReadDataValue<T>(Attribute));
					bHashed = true;
				},
				[&]()
				{
					bOutUnsupported = true;
				});

			if (!bHashed)
			{
				return false;
			}
		}

		// Top bit cleared: the identifier is stored and tagged as a non-negative int64.
		OutHash = PCGExRandomHelpers::Avalanche(Hash) & static_cast<uint64>(MAX_int64);
		return true;
	}

	// Copies every filtered @Data attribute of InData onto one row (TargetKey) of OutMetadata, elements domain.
	// Any-UPCGData counterpart of FDataForwardHandler::Forward(SourceIndex, UPCGMetadata*, TargetKey), which is
	// point-facade only. First-seen type wins: a later input carrying the same name with another type is
	// skipped and counted in OutNumConflicts.
	void CopyDataDomainRow(const FPCGExDataUniquesContext* Context, const UPCGData* InData, UPCGMetadata* OutMetadata, const PCGMetadataEntryKey TargetKey, int32& OutNumConflicts)
	{
		const UPCGMetadata* InMetadata = InData->ConstMetadata();
		if (!InMetadata)
		{
			return;
		}

		TArray<FPCGAttributeIdentifier> Identifiers;
		PCGExDataFilter::Helpers::GetAttributes(InMetadata, EPCGExAttributeDomainScope::Data, Identifiers);

		for (const FPCGAttributeIdentifier& Identifier : Identifiers)
		{
			// The identifier column is written afterward and takes precedence over a same-named source attribute.
			if (Context->bWriteIdentifier && Identifier.Name == Context->IdentifierName)
			{
				continue;
			}

			if (!Context->RowAttributes.Test(Identifier.Name.ToString()))
			{
				continue;
			}

			const FPCGMetadataAttributeBase* SourceAtt = InMetadata->GetConstAttribute(Identifier);
			if (!SourceAtt)
			{
				continue;
			}

			const FPCGAttributeIdentifier TargetIdentifier(Identifier.Name);

			PCGExMetaHelpers::ExecuteWithRightType(
				SourceAtt,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					// Find-or-create, not delete+create: successive buckets land on distinct rows of the same metadata.
					// No overwrite on mismatch -- the default would delete the column and every row already written to it;
					// null here IS the first-seen-type-wins conflict.
					FPCGMetadataAttribute<T>* TargetAtt = OutMetadata->FindOrCreateAttribute<T>(
						TargetIdentifier, T{}, SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/false);

					if (!TargetAtt)
					{
						OutNumConflicts++;
						return;
					}

					TargetAtt->SetValue(TargetKey, PCGExData::Helpers::ReadDataValue<T>(SourceAtt));
				},
				[&]()
				{
					// Property-backed (Struct/Enum/Object/container): create from desc, deep-copy the single @Data value.
					FPCGMetadataAttributeBase* TargetAtt = PCGExMetaHelpers::HasAttribute(OutMetadata, TargetIdentifier) ? OutMetadata->GetMutableAttribute(TargetIdentifier) : nullptr;
					if (!TargetAtt)
					{
						TargetAtt = OutMetadata->CreateAttribute(TargetIdentifier, SourceAtt->GetAttributeDesc(), SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/true);
					}

					// Explicit desc check: PropertyCopyAttribute does not compare descs, and the engine's SetValueFromProperty
					// silently no-ops on a mismatch -- the conflict would go uncounted.
					if (!TargetAtt
						|| !TargetAtt->GetAttributeDesc().IsSameType(SourceAtt->GetAttributeDesc())
						|| !PCGExData::Helpers::PropertyCopyAttribute(SourceAtt, PCGExData::Helpers::GetDataValueKey(SourceAtt), TargetAtt, TargetKey))
					{
						OutNumConflicts++;
					}
				});
		}
	}

	void WriteIdentifier(const FPCGExDataUniquesContext* Context, UPCGMetadata* OutMetadata, const PCGMetadataEntryKey TargetKey, const uint64 Hash)
	{
		FPCGMetadataAttribute<int64>* Attribute = OutMetadata->FindOrCreateAttribute<int64>(FPCGAttributeIdentifier(Context->IdentifierName), 0, /*bAllowsInterpolation=*/false);
		if (Attribute)
		{
			Attribute->SetValue(TargetKey, static_cast<int64>(Hash));
		}
	}

	// Input tags verbatim plus the identifier tag. Prefixed, a value tag already named like the identifier gives
	// way (StaleTagPrefix is "Name:", matched case-insensitively like FTags keys); nothing else is touched.
	TSet<FString> MakeTags(const FPCGExDataUniquesContext* Context, const TSet<FString>& InTags, const uint64 Hash, const bool bPrefixed, const FString& StaleTagPrefix)
	{
		TSet<FString> OutTags;
		OutTags.Reserve(InTags.Num() + 1);

		for (const FString& Tag : InTags)
		{
			if (bPrefixed && Tag.Len() > StaleTagPrefix.Len() && Tag.StartsWith(StaleTagPrefix, ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutTags.Add(Tag);
		}

		FPCGExAttributeToTagDetails::AppendValueTag<int64>(Context->IdentifierName, static_cast<int64>(Hash), bPrefixed, OutTags);
		return OutTags;
	}
}

#pragma region FPCGExDataUniquesElement

bool FPCGExDataUniquesElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDataUniquesElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(DataUniques)

	// Keys: list + comma-separated overrides, user order preserved (it feeds the hash), duplicates dropped.
	TArray<FName> KeyNames = Settings->KeyAttributes;

	TArray<FString> Tokens;
	Settings->CommaSeparatedKeyAttributes.ParseIntoArray(Tokens, TEXT(","), true);
	for (FString& Token : Tokens)
	{
		Token.TrimStartAndEndInline();
		if (!Token.IsEmpty())
		{
			KeyNames.Add(FName(*Token));
		}
	}

	TSet<FName> SeenKeys;
	Context->KeyIdentifiers.Reserve(KeyNames.Num());
	for (const FName& KeyName : KeyNames)
	{
		if (KeyName.IsNone())
		{
			continue;
		}

		// Accepts "@Data.Foo" or "Foo"; the domain is always @Data for this node. Sub-selections are refused rather
		// than dropped -- "Foo.X" would otherwise silently hash the whole of Foo.
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(KeyName.ToString());

		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute
			|| !Selector.GetExtraNames().IsEmpty()
			|| !PCGExMetaHelpers::IsWritableAttributeName(Selector.GetAttributeName()))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" is not a valid key attribute name (plain @Data attribute names only, no sub-selection)."), FText::FromName(KeyName)));
			return false;
		}

		const FPCGAttributeIdentifier Identifier(Selector.GetAttributeName(), PCGMetadataDomainID::Data);

		bool bAlreadySeen = false;
		SeenKeys.Add(Identifier.Name, &bAlreadySeen);
		if (bAlreadySeen)
		{
			continue;
		}

		Context->KeyIdentifiers.Add(Identifier);
	}

	if (Context->KeyIdentifiers.IsEmpty())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No key attribute specified."));
		return false;
	}

	Context->bWriteIdentifier = Settings->bWriteIdentifier;
	Context->IdentifierName = Settings->IdentifierAttributeName;
	PCGEX_VALIDATE_NAME_CONDITIONAL(Context->bWriteIdentifier, Context->IdentifierName)

	// Let the name filter govern every attribute uniformly, PCGEx-prefixed ones included.
	Context->RowAttributes = Settings->RowAttributes;
	Context->RowAttributes.bPreservePCGExData = false;
	Context->RowAttributes.Init();

	return true;
}

bool FPCGExDataUniquesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDataUniquesElement::AdvanceWork);

	PCGEX_CONTEXT_AND_SETTINGS(DataUniques)
	PCGEX_EXECUTION_CHECK

	uint64& InactiveMask = Context->OutputData.InactiveOutputPinBitmask;

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<int32> ValidInputs;
	ValidInputs.Reserve(Inputs.Num());
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		if (Inputs[i].Data)
		{
			ValidInputs.Add(i);
		}
	}

	if (ValidInputs.IsEmpty())
	{
		InactiveMask |= 1ULL << 0;
		InactiveMask |= 1ULL << 2;
		Context->Done();
		return Context->TryComplete();
	}

	// Phase 1 -- hash every input's key combination. Each task writes only its own slot.
	TArray<PCGExDataUniques::FInputKey> Keys;
	Keys.SetNum(ValidInputs.Num());

	PCGExMT::ParallelOrSequential(
		ValidInputs.Num(), [&](const int32 i)
		{
			PCGExDataUniques::FInputKey& Key = Keys[i];
			Key.bValid = PCGExDataUniques::HashKeys(Context, Inputs[ValidInputs[i]].Data, Key.Hash, Key.bUnsupported);
		},
		/*Threshold=*/8, EParallelForFlags::Unbalanced);

	// Phase 2 -- bucket sequentially in input order so bucket order (and thus row order) is deterministic.
	TMap<uint64, int32> HashToBucket;
	TArray<PCGExDataUniques::FBucket> Buckets;
	TArray<int32> BucketOfInput;
	BucketOfInput.Init(INDEX_NONE, ValidInputs.Num());

	int32 NumUnsupported = 0;

	for (int32 i = 0; i < ValidInputs.Num(); i++)
	{
		const PCGExDataUniques::FInputKey& Key = Keys[i];
		if (!Key.bValid)
		{
			NumUnsupported += Key.bUnsupported ? 1 : 0;
			continue;
		}

		int32& BucketIndex = HashToBucket.FindOrAdd(Key.Hash, INDEX_NONE);
		if (BucketIndex == INDEX_NONE)
		{
			BucketIndex = Buckets.Num();
			Buckets.Add({Key.Hash, i});
		}

		BucketOfInput[i] = BucketIndex;
	}

	// Phase 3 -- one row per bucket, copied from the first input seen with that combination.
	int32 NumConflicts = 0;

	if (Buckets.IsEmpty())
	{
		InactiveMask |= 1ULL << 0;
	}
	else if (Settings->OutputMode == EPCGExDataUniquesOutputMode::Merged)
	{
		UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();
		UPCGMetadata* OutMetadata = ParamData->Metadata;

		for (const PCGExDataUniques::FBucket& Bucket : Buckets)
		{
			const PCGMetadataEntryKey RowKey = OutMetadata->AddEntry();
			PCGExDataUniques::CopyDataDomainRow(Context, Inputs[ValidInputs[Bucket.RepresentativeInput]].Data, OutMetadata, RowKey, NumConflicts);
			if (Context->bWriteIdentifier)
			{
				PCGExDataUniques::WriteIdentifier(Context, OutMetadata, RowKey, Bucket.Hash);
			}
		}

		Context->StageOutput(ParamData, PCGExDataUniques::OutputUniquesLabel, PCGExData::EStaging::MutableAndManaged);
	}
	else
	{
		for (const PCGExDataUniques::FBucket& Bucket : Buckets)
		{
			UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();
			UPCGMetadata* OutMetadata = ParamData->Metadata;

			const PCGMetadataEntryKey RowKey = OutMetadata->AddEntry();
			PCGExDataUniques::CopyDataDomainRow(Context, Inputs[ValidInputs[Bucket.RepresentativeInput]].Data, OutMetadata, RowKey, NumConflicts);
			if (Context->bWriteIdentifier)
			{
				PCGExDataUniques::WriteIdentifier(Context, OutMetadata, RowKey, Bucket.Hash);
			}

			Context->StageOutput(ParamData, PCGExDataUniques::OutputUniquesLabel, PCGExData::EStaging::MutableAndManaged);
		}
	}

	// Phase 4 -- forward every input as-is (shared pointer, no duplication), in input order. Matched inputs keep
	// their tags and gain the identifier tag.
	int32 NumDiscarded = 0;
	const FString StaleTagPrefix = Context->IdentifierName.ToString() + PCGExData::TagSeparator;

	for (int32 i = 0; i < ValidInputs.Num(); i++)
	{
		const FPCGTaggedData& InTagged = Inputs[ValidInputs[i]];
		UPCGData* Data = const_cast<UPCGData*>(InTagged.Data.Get());

		if (BucketOfInput[i] == INDEX_NONE)
		{
			Context->StageOutput(Data, PCGExDataUniques::OutputDiscardedLabel, PCGExData::EStaging::None, InTagged.Tags);
			NumDiscarded++;
			continue;
		}

		if (!Context->bWriteIdentifier)
		{
			Context->StageOutput(Data, PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::None, InTagged.Tags);
			continue;
		}

		Context->StageOutput(
			Data, PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::None,
			PCGExDataUniques::MakeTags(Context, InTagged.Tags, Keys[i].Hash, Settings->bPrefixTagWithAttributeName, StaleTagPrefix));
	}

	if (!NumDiscarded)
	{
		InactiveMask |= 1ULL << 2;
	}

	// Aggregated diagnostics -- one line each, never per input.
	if (NumDiscarded > 0)
	{
		PCGEX_LOG_INVALID_INPUT(Context, FText::Format(FTEXT("{0} input(s) routed to Discarded -- a @Data key attribute is missing, or has a container/extended type ({1} of them)."), FText::AsNumber(NumDiscarded), FText::AsNumber(NumUnsupported)))
	}

	if (NumConflicts > 0)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("{0} attribute value(s) skipped on the Uniques rows: same attribute name with a different type across inputs (first-seen type wins)."), FText::AsNumber(NumConflicts)));
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
