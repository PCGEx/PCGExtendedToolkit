// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCopyAttributes.h"

#include "PCGContext.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExDataHelpers.h"
#include "Elements/PCGCopyPoints.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGMetadataHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

#define LOCTEXT_NAMESPACE "PCGExCopyAttributesElement"
#define PCGEX_NAMESPACE CopyAttributes

#pragma region UPCGExCopyAttributesSettings

UPCGExCopyAttributesSettings::UPCGExCopyAttributesSettings()
{
	if (PCGHelpers::IsNewObjectAndNotDefault(this))
	{
		InputSource.SetAttributeName(PCGMetadataAttributeConstants::LastAttributeName);
		OutputTarget.SetAttributeName(PCGMetadataAttributeConstants::SourceAttributeName);
	}
}

FString UPCGExCopyAttributesSettings::GetAdditionalTitleInformation() const
{
#if WITH_EDITOR
	if (bCopyAllAttributes)
	{
		return LOCTEXT("NodeTitleAllAttributes", "All Attributes").ToString();
	}

	if (IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGExCopyAttributesSettings, InputSource)) || IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGExCopyAttributesSettings, OutputTarget)))
	{
		return FString();
	}
#endif

	return FString::Printf(TEXT("%s -> %s"), *InputSource.GetDisplayText().ToString(), *OutputTarget.GetDisplayText().ToString());
}

TArray<FPCGPinProperties> UPCGExCopyAttributesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGCopyPointsConstants::TargetPointsLabel, "The data to copy attributes onto.", Required)
	PCGEX_PIN_ANY(PCGCopyPointsConstants::SourcePointsLabel, "The data to copy attributes from.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCopyAttributesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "The target data, with attributes copied from the sources.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(CopyAttributes)

#pragma endregion

#pragma region PCGExCopyAttributes

namespace PCGExCopyAttributes
{
	// True when the resolved pair reads a single-value source attribute (@Data-like domain) into a
	// multi-entry target domain -- the case the engine helper collapses to a single element entry.
	bool IsSingleToMultiPromotion(
		const UPCGData* SourceData, const UPCGData* TargetData,
		const FPCGAttributePropertyInputSelector& InputSource, const FPCGAttributePropertyOutputSelector& OutputTarget)
	{
		if (InputSource.GetSelection() != EPCGAttributePropertySelection::Attribute || !InputSource.GetExtraNames().IsEmpty())
		{
			return false;
		}

		if (OutputTarget.GetSelection() != EPCGAttributePropertySelection::Attribute || !OutputTarget.GetExtraNames().IsEmpty())
		{
			return false;
		}

		const UPCGMetadata* SourceMetadata = SourceData->ConstMetadata();
		const UPCGMetadata* TargetMetadata = TargetData->ConstMetadata();
		if (!SourceMetadata || !TargetMetadata)
		{
			return false;
		}

		const FPCGMetadataDomainID SourceDomain = SourceData->GetMetadataDomainIDFromSelector(InputSource);
		const FPCGMetadataDomainID TargetDomain = TargetData->GetMetadataDomainIDFromSelector(OutputTarget);
		if (!SourceDomain.IsValid() || !TargetDomain.IsValid())
		{
			return false;
		}

		return !SourceMetadata->MetadataDomainSupportsMultiEntries(SourceDomain) && TargetMetadata->MetadataDomainSupportsMultiEntries(TargetDomain);
	}

	bool CopySingleValueToElements(
		FPCGExContext* InContext,
		const UPCGData* SourceData, UPCGData* TargetData,
		const FPCGAttributePropertyInputSelector& InputSource, const FPCGAttributePropertyOutputSelector& OutputTarget,
		const bool bMaterialize)
	{
		const FPCGAttributeIdentifier SourceId = PCGExMetaHelpers::GetAttributeIdentifier(InputSource, SourceData);
		const FPCGMetadataAttributeBase* SourceAttribute = PCGExMetaHelpers::TryGetConstAttribute(SourceData, SourceId);
		if (!SourceAttribute)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("SourceAttributeMissing", "Source attribute '{0}' does not exist."), InputSource.GetDisplayText()));
			return false;
		}

		const PCGMetadataEntryKey SourceKey = PCGExData::Helpers::GetDataValueKey(SourceAttribute);

		// Read-side validation happens before any target mutation: under data stealing the target is
		// the original input, and a destroy-then-fail sequence would be unrecoverable.
		if (!PCGExData::Helpers::HasPropertyCopyableValue(SourceAttribute, SourceKey))
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("SourceValueUnreadable", "Source attribute '{0}' has no readable value."), InputSource.GetDisplayText()));
			return false;
		}

		UPCGMetadata* TargetMetadata = TargetData->MutableMetadata();
		FPCGMetadataDomain* TargetDomain = TargetMetadata ? TargetMetadata->GetMetadataDomainFromSelector(OutputTarget) : nullptr;
		if (!TargetDomain)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("InvalidTargetDomain", "Invalid target domain for '{0}'."), OutputTarget.GetDisplayText()));
			return false;
		}

		// Materialization keys are gathered before the destructive step too, so a key failure
		// degrades to a default-only copy without having touched the existing attribute.
		TArray<PCGMetadataEntryKey*> EntryKeyPtrs;
		if (bMaterialize)
		{
			const TUniquePtr<IPCGAttributeAccessorKeys> TargetKeys = PCGAttributeAccessorHelpers::CreateKeys(TargetData, OutputTarget);
			const int32 NumTargetKeys = TargetKeys ? TargetKeys->GetNum() : 0;

			if (NumTargetKeys > 0)
			{
				EntryKeyPtrs.SetNumUninitialized(NumTargetKeys);
				if (!TargetKeys->GetKeys<PCGMetadataEntryKey>(0, EntryKeyPtrs))
				{
					EntryKeyPtrs.Reset();
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("MaterializeKeysFailed", "Could not gather element entries for '{0}' -- the value is written as default only, without materialized entries."), OutputTarget.GetDisplayText()));
				}
			}
		}

		// Same semantics as the engine helper: an existing target attribute is deleted and recreated,
		// so the non-materialized copy leaves nothing but the default value behind.
		const FName TargetName = OutputTarget.GetAttributeName();
		if (TargetDomain->HasAttribute(TargetName))
		{
			TargetDomain->DeleteAttribute(TargetName);
		}

		FPCGMetadataAttributeDesc Desc = SourceAttribute->GetAttributeDesc();
		Desc.Name = TargetName;

		FPCGMetadataAttributeBase* TargetAttribute = TargetDomain->CreateAttribute(Desc, SourceAttribute->AllowsInterpolation(), /*bOverrideParent=*/true);
		if (!TargetAttribute)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("FailedCreateAttribute", "Failed to create attribute '{0}'."), FText::FromName(TargetName)));
			return false;
		}

		if (EntryKeyPtrs.IsEmpty())
		{
			if (!PCGExData::Helpers::PropertyCopyAttribute(SourceAttribute, SourceKey, TargetAttribute, PCGDefaultValueKey))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("FailedCopyDefaultValue", "Failed to copy the value of '{0}' onto '{1}'."), InputSource.GetDisplayText(), FText::FromName(TargetName)));
				return false;
			}

			return true;
		}

		// Elements without a concrete entry get one so a value can be attached per element
		// (stripped-down InitializeOnSet, same shape as the engine helper).
		TArray<PCGMetadataEntryKey*> EntriesToAdd;
		EntriesToAdd.Reserve(EntryKeyPtrs.Num());
		for (PCGMetadataEntryKey* EntryKey : EntryKeyPtrs)
		{
			if (*EntryKey == PCGInvalidEntryKey || *EntryKey < TargetDomain->GetItemKeyCountForParent())
			{
				EntriesToAdd.Add(EntryKey);
			}
		}

		if (!EntriesToAdd.IsEmpty())
		{
			TargetDomain->AddEntriesInPlace(EntriesToAdd);
		}

		// One transient property writes both the default slot and the first entry.
		const PCGMetadataEntryKey MaterializeKeys[2] = {PCGDefaultValueKey, *EntryKeyPtrs[0]};
		if (!PCGExData::Helpers::PropertyCopyAttribute(SourceAttribute, SourceKey, TargetAttribute, MakeArrayView(MaterializeKeys, 2)))
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("FailedCopyMaterialized", "Failed to copy the value of '{0}' onto '{1}'."), InputSource.GetDisplayText(), FText::FromName(TargetName)));
			return false;
		}

		// The remaining entries share the first entry's value key -- one stored value regardless of
		// element count.
		const PCGMetadataEntryKey FirstEntryKey = *EntryKeyPtrs[0];
		TArray<PCGMetadataValueKey> ValueKeys;
		TargetAttribute->GetValueKeys(MakeArrayView(&FirstEntryKey, 1), ValueKeys);

		if (ValueKeys.Num() == 1)
		{
			if (EntryKeyPtrs.Num() > 1)
			{
				TArray<PCGMetadataValueKey> AllValueKeys;
				AllValueKeys.Init(ValueKeys[0], EntryKeyPtrs.Num());
				TargetAttribute->SetValuesFromValueKeys(EntryKeyPtrs, AllValueKeys, /*bResetValueOnDefaultValueKey=*/true);
			}
		}
		else
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("MaterializeValueKeyFailed", "Could not resolve the stored value key for '{0}' -- entries beyond the first are not materialized."), FText::FromName(TargetName)));
		}

		return true;
	}

	// Post-copy consumable/protection registration for cache-and-restore workflows: the source
	// attribute becomes a consumable (deleted from this node's outputs by the cleanup toggle) while
	// the written attribute is protected domain-precisely, so a same-named write from any pair can
	// never be deleted by another pair's registration. Only successful copies register -- a failed
	// copy must not delete a same-named pre-existing attribute.
	void RegisterConsumable(
		FPCGExContext* InContext,
		const UPCGData* TargetData,
		const FPCGAttributePropertyInputSelector& InputSource, const FPCGAttributePropertyOutputSelector& OutputTarget)
	{
		const FName QualifiedSourceName = PCGExMetaHelpers::GetDomainQualifiedName(InputSource);
		if (!QualifiedSourceName.IsNone())
		{
			InContext->AddConsumableAttributeName(QualifiedSourceName);
		}

		if (OutputTarget.GetSelection() == EPCGAttributePropertySelection::Attribute && OutputTarget.GetExtraNames().IsEmpty())
		{
			const FPCGMetadataDomainID WrittenDomain = PCGExMetaHelpers::GetNormalizedDomainID(TargetData, OutputTarget);
			if (WrittenDomain.IsValid())
			{
				InContext->AddProtectedAttribute(FPCGAttributeIdentifier(OutputTarget.GetAttributeName(), WrittenDomain));
			}
		}
	}

	bool CopyPair(
		FPCGExContext* InContext,
		const UPCGData* SourceData, UPCGData* TargetData,
		const FPCGAttributePropertyInputSelector& InInputSource, const FPCGAttributePropertyOutputSelector& InOutputTarget,
		const bool bSameOrigin, const bool bMaterialize, const bool bRegisterConsumable)
	{
		const FPCGAttributePropertyInputSelector InputSource = InInputSource.CopyAndFixLast(SourceData);
		const FPCGAttributePropertyOutputSelector OutputTarget = InOutputTarget.CopyAndFixSource(&InputSource, SourceData);

		bool bCopied;
		if (IsSingleToMultiPromotion(SourceData, TargetData, InputSource, OutputTarget))
		{
			bCopied = CopySingleValueToElements(InContext, SourceData, TargetData, InputSource, OutputTarget, bMaterialize);
		}
		else
		{
			PCGMetadataHelpers::FPCGCopyAttributeParams Params{};
			Params.SourceData = SourceData;
			Params.TargetData = TargetData;
			Params.InputSource = InputSource;
			Params.OutputTarget = OutputTarget;
			Params.OptionalContext = InContext;
			Params.bSameOrigin = bSameOrigin;

			bCopied = PCGMetadataHelpers::CopyAttribute(Params);
		}

		if (bCopied && bRegisterConsumable && InContext->bCleanupConsumableAttributes)
		{
			RegisterConsumable(InContext, TargetData, InputSource, OutputTarget);
		}

		return bCopied;
	}

	bool CopyAllAttributes(
		FPCGExContext* InContext, const UPCGExCopyAttributesSettings* Settings,
		const UPCGData* SourceData, UPCGData* TargetData)
	{
		const UPCGMetadata* SourceMetadata = SourceData->ConstMetadata();
		const UPCGMetadata* TargetMetadata = TargetData->ConstMetadata();
		if (!SourceMetadata || !TargetMetadata)
		{
			return false;
		}

		PCGMetadataHelpers::FPCGCopyAllAttributesParams Params
		{
			.SourceData = SourceData,
			.TargetData = TargetData,
			.OptionalContext = InContext,
		};

		if (Settings->bCopyAllDomains)
		{
			Params.InitializeMappingForAllDomains();
		}
		else
		{
			Params.InitializeMappingFromDomainNames(Settings->MetadataDomainsMapping);
		}

		if (Params.DomainMapping.IsEmpty())
		{
			return false;
		}

		// Promotion (single-value -> multi-entry) depends only on the (source domain, target domain)
		// pair, so it's resolved once per encountered domain instead of per attribute.
		TMap<FPCGMetadataDomainID, bool> SourceSingleValue;
		TMap<FPCGMetadataDomainID, bool> TargetMultiEntries;
		auto IsPromotion = [&](const FPCGMetadataDomainID& SourceDomain, const FPCGMetadataDomainID& TargetDomain) -> bool
		{
			bool* bSourceSingle = SourceSingleValue.Find(SourceDomain);
			if (!bSourceSingle)
			{
				bSourceSingle = &SourceSingleValue.Add(SourceDomain, !SourceMetadata->MetadataDomainSupportsMultiEntries(SourceDomain));
			}

			bool* bTargetMulti = TargetMultiEntries.Find(TargetDomain);
			if (!bTargetMulti)
			{
				bTargetMulti = &TargetMultiEntries.Add(TargetDomain, TargetMetadata->MetadataDomainSupportsMultiEntries(TargetDomain));
			}

			return *bSourceSingle && *bTargetMulti;
		};

		// Mirror of the engine's CopyAllAttributes selector expansion, except promoted pairs route
		// through CopySingleValueToElements so @Data values reach every element.
		TArray<FPCGAttributeIdentifier> AttributeIDs;
		TArray<EPCGMetadataTypes> AttributeTypes;
		SourceMetadata->GetAllAttributes(AttributeIDs, AttributeTypes);

		const FPCGMetadataDomainID DefaultSourceDomain = SourceMetadata->GetConstDefaultMetadataDomain()->GetDomainID();

		bool bSuccess = false;
		for (const FPCGAttributeIdentifier& AttributeID : AttributeIDs)
		{
			const FPCGMetadataDomainID* TargetDomainId = Params.DomainMapping.Find(AttributeID.MetadataDomain);
			if (!TargetDomainId && AttributeID.MetadataDomain == DefaultSourceDomain)
			{
				TargetDomainId = Params.DomainMapping.Find(PCGMetadataDomainID::Default);
			}

			if (!TargetDomainId && AttributeID.MetadataDomain.IsDefault())
			{
				TargetDomainId = Params.DomainMapping.Find(DefaultSourceDomain);
			}

			if (!TargetDomainId)
			{
				continue;
			}

			FPCGAttributePropertyInputSelector InputSource;
			InputSource.SetAttributeName(AttributeID.Name);
			FPCGAttributePropertyOutputSelector OutputTarget;
			OutputTarget.SetAttributeName(AttributeID.Name);

			SourceData->SetDomainFromDomainID(AttributeID.MetadataDomain, InputSource);
			TargetData->SetDomainFromDomainID(*TargetDomainId, OutputTarget);

			// No consumable registration in copy-all: source names match the just-copied attributes.
			if (IsPromotion(AttributeID.MetadataDomain, *TargetDomainId))
			{
				bSuccess |= CopySingleValueToElements(InContext, SourceData, TargetData, InputSource, OutputTarget, Settings->bMaterializeDataValues);
			}
			else
			{
				PCGMetadataHelpers::FPCGCopyAttributeParams CopyParams{};
				CopyParams.SourceData = SourceData;
				CopyParams.TargetData = TargetData;
				CopyParams.InputSource = InputSource;
				CopyParams.OutputTarget = OutputTarget;
				CopyParams.OptionalContext = InContext;
				CopyParams.bSameOrigin = false;

				bSuccess |= PCGMetadataHelpers::CopyAttribute(CopyParams);
			}
		}

		return bSuccess;
	}
}

#pragma endregion

#pragma region FPCGExCopyAttributesElement

bool FPCGExCopyAttributesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyAttributesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CopyAttributes)

	const TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGCopyPointsConstants::SourcePointsLabel);
	const TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGCopyPointsConstants::TargetPointsLabel);

	const int32 NumSources = Sources.Num();
	const int32 NumTargets = Targets.Num();

	if (NumSources == 0 || NumTargets == 0)
	{
		Context->Done();
		return Context->TryComplete();
	}

	int32 NumIterations = 0;
	switch (Settings->Operation)
	{
	case EPCGCopyAttributesOperation::CopyEachSourceOnEveryTarget:
		NumIterations = NumSources * NumTargets;
		break;
	case EPCGCopyAttributesOperation::MergeSourcesAndCopyToAllTargets:
		NumIterations = NumTargets;
		break;
	case EPCGCopyAttributesOperation::CopyEachSourceToEachTargetRespectively:
	default:
		if (NumSources != NumTargets && NumSources != 1 && NumTargets != 1)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("MismatchNum", "Num Sources ({0}) mismatches with Num Targets ({1}). Only supports N:N, 1:N and N:1 operation."), NumSources, NumTargets));
			for (const FPCGTaggedData& Target : Targets)
			{
				Context->StageOutput(const_cast<UPCGData*>(Target.Data.Get()), PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::None, Target.Tags);
			}
			Context->Done();
			return Context->TryComplete();
		}
		// The engine element iterates NumTargets here, silently dropping extra sources in the N:1
		// case; Max honors the documented "produces Max(N,M) data" contract of the operation enum.
		NumIterations = FMath::Max(NumSources, NumTargets);
		break;
	}

	// Iteration -> source index mapping (Merge visits every source per iteration).
	const EPCGCopyAttributesOperation Operation = Settings->Operation;
	auto SourceIndexForIteration = [Operation, NumSources, NumTargets](const int32 Iteration) -> int32
	{
		switch (Operation)
		{
		case EPCGCopyAttributesOperation::CopyEachSourceOnEveryTarget:
			return Iteration / NumTargets;
		case EPCGCopyAttributesOperation::CopyEachSourceToEachTargetRespectively:
		default:
			return FMath::Min(Iteration, NumSources - 1);
		}
	};

	// Pointer-level steal analysis: iterations run in parallel, so an input may only be mutated in
	// place when no other iteration can observe it -- targeted exactly once, and read as a source
	// only by that same iteration through a single-source copy (the engine handles same-data copies).
	const bool bStealData = Context->bWantsDataStealing;
	TSet<const UPCGData*> StealTargets;

	if (bStealData)
	{
		TMap<const UPCGData*, TPair<int32, int32>> TargetUse; // {UseCount, LastIteration}
		for (int32 i = 0; i < NumIterations; i++)
		{
			TPair<int32, int32>& Use = TargetUse.FindOrAdd(Targets[i % NumTargets].Data.Get(), TPair<int32, int32>(0, -1));
			Use.Key++;
			Use.Value = i;
		}

		TMap<const UPCGData*, TPair<int32, int32>> SourceUse; // {UseCount, LastIteration}
		auto RecordSourceUse = [&SourceUse](const UPCGData* InData, const int32 Iteration)
		{
			TPair<int32, int32>& Use = SourceUse.FindOrAdd(InData, TPair<int32, int32>(0, -1));
			Use.Key++;
			Use.Value = Iteration;
		};

		if (Operation == EPCGCopyAttributesOperation::MergeSourcesAndCopyToAllTargets)
		{
			for (int32 i = 0; i < NumIterations; i++)
			{
				for (int32 j = 0; j < NumSources; j++)
				{
					RecordSourceUse(Sources[j].Data.Get(), i);
				}
			}
		}
		else
		{
			for (int32 i = 0; i < NumIterations; i++)
			{
				RecordSourceUse(Sources[SourceIndexForIteration(i)].Data.Get(), i);
			}
		}

		for (const TPair<const UPCGData*, TPair<int32, int32>>& Target : TargetUse)
		{
			if (!Target.Key || Target.Value.Key != 1)
			{
				continue;
			}

			const TPair<int32, int32>* Source = SourceUse.Find(Target.Key);
			if (!Source || (Source->Key == 1 && Source->Value == Target.Value.Value))
			{
				StealTargets.Add(Target.Key);
			}
		}
	}

	// Iterations are independent by construction (distinct duplicates, or steal-safe pointers per
	// the analysis above); sources are only ever read.
	struct FIterationResult
	{
		const UPCGData* Data = nullptr;
		PCGExData::EStaging Staging = PCGExData::EStaging::None;
	};

	TArray<FIterationResult> Results;
	Results.SetNum(NumIterations);

	PCGExMT::ParallelOrSequential(NumIterations, [&](const int32 i)
	{
		const FPCGTaggedData& Target = Targets[i % NumTargets];
		const UPCGData* TargetData = Target.Data;

		FIterationResult& Result = Results[i];
		Result.Data = TargetData;

		const bool bStealThisTarget = TargetData && StealTargets.Contains(TargetData);

		bool bSuccess = false;
		UPCGData* OutputData = nullptr;

		auto DoCopy = [&](const int32 SourceIndex)
		{
			const FPCGTaggedData& Source = Sources[SourceIndex];
			const UPCGData* SourceData = Source.Data;

			const bool bIsSameData = TargetData == SourceData;

			if (!TargetData || !SourceData)
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("InvalidInputData", "Invalid input data"));
				return;
			}

			if (!SourceData->ConstMetadata())
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, LOCTEXT("MissingMetadata", "Source has no metadata"));
				return;
			}

			if (bIsSameData && Settings->bCopyAllAttributes)
			{
				PCGE_LOG_C(Verbose, LogOnly, Context, LOCTEXT("TrivialAllCopy", "Copying all attributes on itself is a trivial operation."));
				return;
			}

			if (bIsSameData && Settings->InputSource == Settings->OutputTarget)
			{
				PCGE_LOG_C(Verbose, LogOnly, Context, LOCTEXT("TrivialCopy", "Copying attribute to itself is a trivial operation."));
				return;
			}

			if (!OutputData)
			{
				OutputData = bStealThisTarget ? const_cast<UPCGData*>(TargetData) : Context->ManagedObjects->DuplicateData<UPCGData>(TargetData);
				if (!OutputData)
				{
					return;
				}
			}

			if (Settings->bCopyAllAttributes)
			{
				bSuccess |= PCGExCopyAttributes::CopyAllAttributes(Context, Settings, SourceData, OutputData);
			}
			else
			{
				bSuccess |= PCGExCopyAttributes::CopyPair(Context, SourceData, OutputData, Settings->InputSource, Settings->OutputTarget, bIsSameData, Settings->bMaterializeDataValues, /*bRegisterConsumable=*/true);
			}
		};

		if (Operation == EPCGCopyAttributesOperation::MergeSourcesAndCopyToAllTargets)
		{
			for (int32 j = 0; j < NumSources; ++j)
			{
				DoCopy(j);
			}
		}
		else
		{
			DoCopy(SourceIndexForIteration(i));
		}

		if (bSuccess && OutputData)
		{
			Result.Data = OutputData;
			Result.Staging = bStealThisTarget
				                 ? PCGExData::EStaging::Mutable
				                 : PCGExData::EStaging::Managed | PCGExData::EStaging::Mutable;
		}
	}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	for (int32 i = 0; i < NumIterations; i++)
	{
		const FIterationResult& Result = Results[i];
		if (!Result.Data)
		{
			continue;
		}

		Context->StageOutput(const_cast<UPCGData*>(Result.Data), PCGPinConstants::DefaultOutputLabel, Result.Staging, Targets[i % NumTargets].Tags);
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
