// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFormatAttributes.h"

#include "PCGContext.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

#define LOCTEXT_NAMESPACE "PCGExFormatAttributesElement"
#define PCGEX_NAMESPACE FormatAttributes

// Shared log prefix so all user-facing warnings carry the same node name.
#define PCGEX_FORMAT_LOG_PREFIX TEXT("Format Attributes: ")

#pragma region UPCGSettings interface

void UPCGExFormatAttributesSettings::GatherExternalPinNames(TSet<FName>& OutPins) const
{
	for (const FPCGExFormatTokenRule& Rule : Rules)
	{
		if (Rule.SourceMode != EPCGExTokenSourceMode::External) { continue; }
		if (Rule.ExternalPin.IsNone()) { continue; }
		OutPins.Add(Rule.ExternalPin);
	}
}

TArray<FPCGPinProperties> UPCGExFormatAttributesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Point or attribute-set data whose FName/FString attributes will be formatted.", Required)

	TSet<FName> ExternalPins;
	GatherExternalPinNames(ExternalPins);
	for (const FName& PinName : ExternalPins)
	{
		PCGEX_PIN_ANY(PinName, "External source data providing replacement values for tokens. May be a single attribute set (broadcast), N attribute sets matching input count (per-input pairing), or a single set with matching row count (row-by-row).", Normal)
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFormatAttributesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "The processed input data, with token replacements applied.", Required)
	return PinProperties;
}

FPCGElementPtr UPCGExFormatAttributesSettings::CreateElement() const
{
	return MakeShared<FPCGExFormatAttributesElement>();
}

#pragma endregion

namespace PCGExFormatAttributes
{
	// One rule's replacement value for one input. Broadcast holds a single value used across every
	// row (1-row sources or fallback); per-row holds an aligned array. Broadcast avoids an N-copy
	// of the same string in the common case (single-source self-attribute, single-row external set,
	// or fallback).
	struct FRuleReplacement
	{
		FString BroadcastValue;
		TArray<FString> PerRowValues;
		bool bIsBroadcast = true;

		FORCEINLINE const FString& Get(const int32 Row) const
		{
			return bIsBroadcast ? BroadcastValue : PerRowValues[Row];
		}
	};

	// Hot-path-invariant lookups built once before the parallel dispatch.
	//   ApplicableRulesByTarget[t]   -> indices of rules whose TargetAttributes filter accepts target t
	//   ExternalListByRule[i]        -> resolved external-pin TaggedData list for rule i (nullptr for Self)
	struct FFormatPrecomputed
	{
		TArray<TArray<int32>> ApplicableRulesByTarget;
		TArray<const TArray<FPCGTaggedData>*> ExternalListByRule;
	};

	// Per-task scratch carried into FormatTargetAttribute. DupData and WriteKeys are non-owning
	// pointers into the task's local objects.
	struct FFormatInputState
	{
		UPCGData* DupData = nullptr;
		IPCGAttributeAccessorKeys* WriteKeys = nullptr;
		int32 NumRows = 0;
		TArray<FRuleReplacement> RuleReplacements;
		TArray<bool> RuleOK;
	};

	// Pin-level pairing: 1 source data -> broadcast; N sources matching input count -> input k pairs
	// with source k; out-of-range -> clamp to last available source.
	FORCEINLINE const UPCGData* ResolveExternalSource(const TArray<FPCGTaggedData>* List, const int32 InputIdx)
	{
		if (!List || List->IsEmpty()) { return nullptr; }
		const int32 SrcIdx = List->Num() > 1 ? FMath::Clamp(InputIdx, 0, List->Num() - 1) : 0;
		return (*List)[SrcIdx].Data;
	}

	// Source row count rules:
	//   NumSourceRows == NumTargetRows (> 1) -> per-row pairing
	//   anything else                        -> broadcast row 0 (covers 1-row sources and mismatches)
	bool ReadSourceIntoReplacement(
		const UPCGData* SourceData,
		const FPCGAttributePropertyInputSelector& InSelector,
		const int32 NumTargetRows,
		FRuleReplacement& Out)
	{
		if (!SourceData || NumTargetRows <= 0) { return false; }

		FPCGAttributePropertyInputSelector Resolved = InSelector.CopyAndFixLast(SourceData);
		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(SourceData, Resolved);
		if (!Accessor) { return false; }

		TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(SourceData, Resolved);
		if (!Keys || Keys->GetNum() == 0) { return false; }

		const int32 NumSourceRows = Keys->GetNum();
		if (NumSourceRows == NumTargetRows && NumSourceRows > 1)
		{
			Out.bIsBroadcast = false;
			Out.PerRowValues.SetNum(NumTargetRows);
			Out.BroadcastValue.Reset();
			return Accessor->GetRange<FString>(Out.PerRowValues, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
		}

		Out.bIsBroadcast = true;
		Out.PerRowValues.Reset();
		return Accessor->Get<FString>(Out.BroadcastValue, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}

	// Precompute the per-target rule-applicability lists and per-rule external-source pointers.
	// Both depend only on Settings + Boot-resolved external inputs, so building them once and
	// passing const refs into the parallel loop kills NumInputs * NumTargets worth of redundant
	// allocations and TMap lookups.
	void BuildPrecomputed(
		const UPCGExFormatAttributesSettings* Settings,
		const FPCGExFormatAttributesContext* Context,
		FFormatPrecomputed& Out)
	{
		const TArray<FPCGAttributePropertyInputSelector>& Targets = Context->TargetSelectors;
		const TArray<FPCGExFormatTokenRule>& Rules = Settings->Rules;
		const int32 NumTargets = Targets.Num();
		const int32 NumRules = Rules.Num();

		Out.ApplicableRulesByTarget.SetNum(NumTargets);
		for (int32 t = 0; t < NumTargets; t++)
		{
			const FName TargetName = Targets[t].GetName();
			TArray<int32>& Applicable = Out.ApplicableRulesByTarget[t];
			Applicable.Reserve(NumRules);
			for (int32 i = 0; i < NumRules; i++)
			{
				const FPCGExFormatTokenRule& Rule = Rules[i];
				if (Rule.TargetAttributes.IsEmpty() || Rule.TargetAttributes.Contains(TargetName))
				{
					Applicable.Add(i);
				}
			}
		}

		Out.ExternalListByRule.SetNumZeroed(NumRules);
		for (int32 i = 0; i < NumRules; i++)
		{
			const FPCGExFormatTokenRule& Rule = Rules[i];
			if (Rule.SourceMode == EPCGExTokenSourceMode::External)
			{
				Out.ExternalListByRule[i] = Context->ExternalSources.Find(Rule.ExternalPin);
			}
		}
	}

	void ResolveRuleReplacements(
		FPCGExContext* InContext,
		const UPCGExFormatAttributesSettings* Settings,
		const FFormatPrecomputed& Precomputed,
		const UPCGData* InputData,
		const int32 InputIdx,
		const int32 NumRows,
		FFormatInputState& State)
	{
		const TArray<FPCGExFormatTokenRule>& Rules = Settings->Rules;
		const int32 NumRules = Rules.Num();
		State.RuleReplacements.SetNum(NumRules);
		State.RuleOK.Init(false, NumRules);

		for (int32 i = 0; i < NumRules; i++)
		{
			const FPCGExFormatTokenRule& Rule = Rules[i];

			const UPCGData* SourceData = (Rule.SourceMode == EPCGExTokenSourceMode::Self)
				                             ? InputData
				                             : ResolveExternalSource(Precomputed.ExternalListByRule[i], InputIdx);

			if (SourceData && ReadSourceIntoReplacement(SourceData, Rule.Source, NumRows, State.RuleReplacements[i]))
			{
				State.RuleOK[i] = true;
				continue;
			}

			if (Rule.bHasFallback)
			{
				FRuleReplacement& R = State.RuleReplacements[i];
				R.bIsBroadcast = true;
				R.BroadcastValue = Rule.FallbackValue;
				R.PerRowValues.Reset();
				State.RuleOK[i] = true;
				continue;
			}

			if (!Settings->bQuietMissingSourceWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					        FTEXT(PCGEX_FORMAT_LOG_PREFIX "rule #{0} could not read its source attribute on input #{1} (no fallback configured). Rule will be skipped for this input."),
					        FText::AsNumber(i), FText::AsNumber(InputIdx)));
			}
		}
	}

	void ApplyRulesSequential(
		TArray<FString>& Values,
		TConstArrayView<int32> ApplicableRuleIndices,
		const TArray<FPCGExFormatTokenRule>& Rules,
		const TArray<FRuleReplacement>& RuleReplacements,
		const TArray<bool>& RuleOK)
	{
		const int32 NumRows = Values.Num();
		for (const int32 RuleIdx : ApplicableRuleIndices)
		{
			if (!RuleOK[RuleIdx]) { continue; }
			const FString& Token = Rules[RuleIdx].Token;
			if (Token.IsEmpty()) { continue; }

			const FRuleReplacement& Repl = RuleReplacements[RuleIdx];
			if (Repl.bIsBroadcast)
			{
				const FString& Replacement = Repl.BroadcastValue;
				for (int32 r = 0; r < NumRows; r++) { Values[r].ReplaceInline(*Token, *Replacement, ESearchCase::CaseSensitive); }
			}
			else
			{
				const TArray<FString>& PerRow = Repl.PerRowValues;
				for (int32 r = 0; r < NumRows; r++) { Values[r].ReplaceInline(*Token, *PerRow[r], ESearchCase::CaseSensitive); }
			}
		}
	}

	// Walk each row's original string once. Literal runs are bulk-copied via FString::Append(TCHAR*,
	// Count) instead of per-char Push; replacements are injected at match positions. Replacement
	// text is never re-scanned. Tie-break: first applicable rule whose token matches wins.
	//
	// Scratch / Output buffers are hoisted outside the row loop so their FString capacity carries
	// across rows; we use Swap to move strings in/out without copying.
	void ApplyRulesSinglePass(
		TArray<FString>& Values,
		TConstArrayView<int32> ApplicableRuleIndices,
		const TArray<FPCGExFormatTokenRule>& Rules,
		const TArray<FRuleReplacement>& RuleReplacements,
		const TArray<bool>& RuleOK)
	{
		const int32 NumRows = Values.Num();
		FString Scratch;
		FString Output;

		for (int32 r = 0; r < NumRows; r++)
		{
			if (Values[r].IsEmpty()) { continue; }

			Scratch.Reset();
			Swap(Scratch, Values[r]);

			const TCHAR* OrigData = *Scratch;
			const int32 InputLen = Scratch.Len();

			Output.Reset();
			Output.Reserve(InputLen);

			int32 Pos = 0;
			int32 RunStart = 0;
			while (Pos < InputLen)
			{
				bool bMatched = false;
				for (const int32 RuleIdx : ApplicableRuleIndices)
				{
					if (!RuleOK[RuleIdx]) { continue; }
					const FString& Token = Rules[RuleIdx].Token;
					const int32 TokenLen = Token.Len();
					if (TokenLen == 0 || Pos + TokenLen > InputLen) { continue; }
					if (FCString::Strncmp(OrigData + Pos, *Token, TokenLen) == 0)
					{
						if (Pos > RunStart) { Output.AppendChars(OrigData + RunStart, Pos - RunStart); }
						Output += RuleReplacements[RuleIdx].Get(r);
						Pos += TokenLen;
						RunStart = Pos;
						bMatched = true;
						break;
					}
				}
				if (!bMatched) { ++Pos; }
			}
			if (Pos > RunStart) { Output.AppendChars(OrigData + RunStart, Pos - RunStart); }

			Swap(Values[r], Output);
		}
	}

	// Per-target write pipeline. Reads the target as FString, applies rules, writes back -- either
	// in place (InPlace) or to a new attribute with the configured suffix (NewAttribute). The new
	// attribute is declared with the same type as the source (FName stays FName, FString stays
	// FString). Targets with any other type are warned-and-skipped.
	void FormatTargetAttribute(
		FPCGExContext* InContext,
		const UPCGExFormatAttributesSettings* Settings,
		const FFormatPrecomputed& Precomputed,
		FFormatInputState& State,
		const int32 TargetIdx,
		const FPCGAttributePropertyInputSelector& TargetSelector)
	{
		UPCGData* DupData = State.DupData;
		const int32 NumRows = State.NumRows;

		FPCGAttributePropertyInputSelector Resolved = TargetSelector.CopyAndFixLast(DupData);
		const FName TargetName = Resolved.GetName();

		UPCGMetadata* Metadata = DupData->MutableMetadata();
		if (!Metadata) { return; }

		const FPCGAttributeIdentifier SrcId = PCGExMetaHelpers::GetAttributeIdentifier(Resolved, DupData);
		const FPCGMetadataAttributeBase* SrcAttrBase = Metadata->GetConstAttribute(SrcId);
		if (!SrcAttrBase)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				        FTEXT(PCGEX_FORMAT_LOG_PREFIX "target attribute \"{0}\" not found on this input -- skipping."),
				        FText::FromName(TargetName)));
			return;
		}

		const int16 SrcTypeId = SrcAttrBase->GetTypeId();
		const bool bIsFName = SrcTypeId == PCG::Private::MetadataTypes<FName>::Id;
		const bool bIsFString = SrcTypeId == PCG::Private::MetadataTypes<FString>::Id;
		if (!bIsFName && !bIsFString)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				        FTEXT(PCGEX_FORMAT_LOG_PREFIX "target attribute \"{0}\" is not FName or FString -- skipping."),
				        FText::FromName(TargetName)));
			return;
		}

		// Reading as FString relies on the AllowBroadcastAndConstructible flag to convert from
		// FName when applicable -- no-op for FString sources.
		TUniquePtr<const IPCGAttributeAccessor> ReadAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(DupData, Resolved);
		if (!ReadAccessor) { return; }

		TArray<FString> Values;
		Values.SetNum(NumRows);
		if (!ReadAccessor->GetRange<FString>(Values, 0, *State.WriteKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			return;
		}

		const TConstArrayView<int32> ApplicableRules = Precomputed.ApplicableRulesByTarget[TargetIdx];

		if (Settings->ReplacementMode == EPCGExFormatReplacementMode::Sequential)
		{
			ApplyRulesSequential(Values, ApplicableRules, Settings->Rules, State.RuleReplacements, State.RuleOK);
		}
		else
		{
			ApplyRulesSinglePass(Values, ApplicableRules, Settings->Rules, State.RuleReplacements, State.RuleOK);
		}

		FName OutputName = TargetName;
		if (Settings->WriteMode == EPCGExFormatWriteMode::NewAttribute)
		{
			OutputName = FName(*(TargetName.ToString() + Settings->NewAttributeSuffix));
			if (!PCGExMetaHelpers::IsWritableAttributeName(OutputName))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					        FTEXT(PCGEX_FORMAT_LOG_PREFIX "derived output name \"{0}\" is not a valid attribute name -- skipping target \"{1}\"."),
					        FText::FromName(OutputName), FText::FromName(TargetName)));
				return;
			}

			const FPCGAttributeIdentifier OutId = PCGExMetaHelpers::GetAttributeIdentifier(OutputName, DupData);
			if (bIsFName) { Metadata->FindOrCreateAttribute<FName>(OutId, NAME_None, false, true); }
			else { Metadata->FindOrCreateAttribute<FString>(OutId, FString(), false, true); }
		}

		FPCGAttributePropertyInputSelector WriteSelector;
		WriteSelector.Update(OutputName.ToString());
		WriteSelector = WriteSelector.CopyAndFixLast(DupData);
		TUniquePtr<IPCGAttributeAccessor> WriteAccessor = PCGAttributeAccessorHelpers::CreateAccessor(DupData, WriteSelector);
		if (!WriteAccessor) { return; }
		WriteAccessor->SetRange<FString>(Values, 0, *State.WriteKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}
}

bool FPCGExFormatAttributesElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFormatAttributesElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, FormatAttributes)
	FPCGExFormatAttributesContext* Context = static_cast<FPCGExFormatAttributesContext*>(InContext);

	// Resolve target list: explicit array + selectors parsed from the comma-separated override.
	// Done once in Boot so AdvanceWork doesn't re-parse the string per-input.
	Context->TargetSelectors = Settings->TargetAttributes;
	PCGExMetaHelpers::AppendUniqueSelectorsFromCommaSeparatedList(Settings->CommaSeparatedAttributeSelectors, Context->TargetSelectors);

	TSet<FName> ExternalPins;
	Settings->GatherExternalPinNames(ExternalPins);
	for (const FName& PinName : ExternalPins)
	{
		Context->ExternalSources.Add(PinName, InContext->InputData.GetInputsByPin(PinName));
	}

	return true;
}

bool FPCGExFormatAttributesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFormatAttributesElement::AdvanceWork);

	PCGEX_SETTINGS_C(InContext, FormatAttributes)
	FPCGExFormatAttributesContext* Context = static_cast<FPCGExFormatAttributesContext*>(InContext);

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<int32> ValidInputIndices;
	ValidInputIndices.Reserve(Inputs.Num());
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		if (Inputs[i].Data) { ValidInputIndices.Add(i); }
	}

	if (ValidInputIndices.IsEmpty())
	{
		InContext->Done();
		return InContext->TryComplete();
	}

	PCGExFormatAttributes::FFormatPrecomputed Precomputed;
	PCGExFormatAttributes::BuildPrecomputed(Settings, Context, Precomputed);

	TArray<FPCGTaggedData> ParallelResults;
	ParallelResults.SetNum(ValidInputIndices.Num());

	PCGExMT::ParallelOrSequential(ValidInputIndices.Num(), [&](const int32 ValidIdx)
	{
		const int32 InputIdx = ValidInputIndices[ValidIdx];
		const FPCGTaggedData& InputTagged = Inputs[InputIdx];

		UPCGData* DupData = InContext->ManagedObjects->DuplicateData<UPCGData>(InputTagged.Data);
		if (!DupData) { return; }

		const int32 NumRows = PCGExMetaHelpers::GetElementsCount(DupData);
		if (NumRows > 0 && !Settings->Rules.IsEmpty() && !Context->TargetSelectors.IsEmpty())
		{
			if (TSharedPtr<IPCGAttributeAccessorKeys> WriteKeys = PCGExMetaHelpers::MakeMutableKeys(DupData))
			{
				PCGExFormatAttributes::FFormatInputState State;
				State.DupData = DupData;
				State.WriteKeys = WriteKeys.Get();
				State.NumRows = NumRows;
				PCGExFormatAttributes::ResolveRuleReplacements(InContext, Settings, Precomputed, DupData, InputIdx, NumRows, State);

				const int32 NumTargets = Context->TargetSelectors.Num();
				for (int32 t = 0; t < NumTargets; t++)
				{
					PCGExFormatAttributes::FormatTargetAttribute(InContext, Settings, Precomputed, State, t, Context->TargetSelectors[t]);
				}
			}
		}

		FPCGTaggedData& OutTagged = ParallelResults[ValidIdx];
		OutTagged.Pin = PCGPinConstants::DefaultOutputLabel;
		OutTagged.Data = DupData;
		OutTagged.Tags = InputTagged.Tags;
	}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	// Staged (not written directly to OutputData) so ownership flags feed the flush-time
	// consumable/flatten pass.
	InContext->IncreaseStagedOutputReserve(ParallelResults.Num());
	for (const FPCGTaggedData& R : ParallelResults)
	{
		if (R.Data)
		{
			InContext->StageOutput(const_cast<UPCGData*>(R.Data.Get()), PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::Managed | PCGExData::EStaging::Mutable, R.Tags);
		}
	}

	InContext->Done();
	return InContext->TryComplete();
}

#undef PCGEX_FORMAT_LOG_PREFIX
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
