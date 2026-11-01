// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildVariantCollection.h"

#include "PCGComponent.h"
#include "PCGParamData.h"
#include "PCGPin.h"

#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Elements/PCGExBuildAssetCollection.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Metadata/PCGMetadata.h"

#include "PCGExCollectionsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExBuildVariantCollectionElement"
#define PCGEX_NAMESPACE BuildVariantCollection

#pragma region UPCGExBuildVariantCollectionSettings

TArray<FPCGPinProperties> UPCGExBuildVariantCollectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceAssetCollection, "Attribute sets holding one material swap per row, read in pin order: for a material, the first row that applies wins.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBuildVariantCollectionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(PCGPinConstants::DefaultOutputLabel, "Attribute set carrying the built variant's soft path under Output Attribute Name. Wire into Staging : Swap's Additional Variant override pin.", Normal)
	return PinProperties;
}

FPCGDataTypeIdentifier UPCGExBuildVariantCollectionSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	FPCGDataTypeIdentifier Id;
	return PCGExBuildAssetCollection::TryGetPathPinTypesID(InPin, Id) ? Id : Super::GetCurrentPinTypesID(InPin);
}

PCGEX_INITIALIZE_ELEMENT(BuildVariantCollection)

#pragma endregion

#pragma region FPCGExBuildVariantCollectionElement

bool FPCGExBuildVariantCollectionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildVariantCollectionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildVariantCollection)
	PCGEX_EXECUTION_CHECK

	check(IsInGameThread()); // guaranteed by CanExecuteOnlyOnMainThread

	// Emit the resolved path (empty when the build can't happen).
	auto CompleteWith = [&](const FSoftObjectPath& InPath) -> bool
	{
		return PCGExBuildAssetCollection::CompleteWithPath(Context, Settings->OutputAttributeName, InPath);
	};

	TArray<const UPCGParamData*> Inputs;
	for (const FPCGTaggedData& Tagged : Context->InputData.GetInputsByPin(PCGExCollections::Labels::SourceAssetCollection))
	{
		const UPCGParamData* Param = Cast<UPCGParamData>(Tagged.Data);
		if (Param && Param->Metadata)
		{
			Inputs.Add(Param);
		}
	}

	if (Inputs.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Build Variant Collection needs at least one input attribute set."));
		return CompleteWith(FSoftObjectPath());
	}

	// No component -> nowhere to anchor lifetime; the soft path would dangle.
	UPCGComponent* Component = Context->GetMutableComponent();
	if (!Component)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Build Variant Collection needs an owning PCG component to manage the variant's lifetime; no variant was built."));
		return CompleteWith(FSoftObjectPath());
	}

	// The prefix keeps the key apart from Build Asset Collection's resources (same resource class). Names are
	// length-prefixed: tag constants are free text, so a delimiter inside one must not move a field boundary.
	TStringBuilder<512> ConfigBuilder;
	auto AppendName = [&ConfigBuilder](const FName InName)
	{
		const FString Text = InName.ToString();
		ConfigBuilder.Appendf(TEXT("%d:"), Text.Len());
		ConfigBuilder << Text;
	};

	ConfigBuilder << TEXT("BuildVariant|");
	AppendName(Settings->MatchAttribute);
	AppendName(Settings->ReplacementAttribute);
	if (Settings->bUseTagFilter)
	{
		const FPCGExBaseTagFilterDetails& Filter = Settings->TagFilter;
		for (const FPCGExInputShorthandNameName* Clause : {&Filter.RequireAll, &Filter.RequireAny, &Filter.Exclude})
		{
			ConfigBuilder.Appendf(TEXT("|%d;"), static_cast<int32>(Clause->Input));
			AppendName(Clause->Attribute);
			AppendName(Clause->Constant);
		}
		ConfigBuilder.Appendf(TEXT("|%d|%d"), Filter.bParseCommaSeparatedLists ? 1 : 0, static_cast<int32>(Filter.TagSources));
	}
	const FString ConfigId = ConfigBuilder.ToString();
	const FPCGCrc Crc = PCGExBuildAssetCollection::ComputeReuseCrc(ConfigId, Inputs);

	if (const UPCGExManagedAssetCollection* Existing = PCGExManagedHelpers::TryReuseManagedResource<UPCGExManagedAssetCollection>(
		Component, Crc,
		[&ConfigId](const UPCGExManagedAssetCollection* Candidate)
		{
			return Candidate->Collection && Candidate->Collection->IsA<UPCGExVariantCollection>() && Candidate->Config == ConfigId;
		}))
	{
		return CompleteWith(FSoftObjectPath(Existing->Collection));
	}

	// Outer resource->component (persists across gens) and variant->resource, so the variant's soft path is unique and
	// resolvable across the node boundary.
	UPCGExManagedAssetCollection* Managed = NewObject<UPCGExManagedAssetCollection>(Component);
	Managed->SetCrc(Crc);
	Managed->Config = ConfigId;

	UPCGExVariantCollection* Variant = NewObject<UPCGExVariantCollection>(Managed, NAME_None, RF_Transient);

	// Rows are written directly: Modify / PostEditChange would broadcast to every tracker of the object.
	int32 NumSkipped = 0;
	for (const UPCGParamData* Input : Inputs)
	{
		int32 Skipped = 0;
		if (PCGExCollectionHelpers::AppendMaterialSwapsFromAttributeSet(Variant, Context, Input, Settings->MatchAttribute, Settings->ReplacementAttribute, Settings->bUseTagFilter ? &Settings->TagFilter : nullptr, Skipped) < 0)
		{
			return CompleteWith(FSoftObjectPath());
		}
		NumSkipped += Skipped;
	}

	if (NumSkipped > 0 && !Settings->bQuietSkippedRowsWarning)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("{0} row(s) skipped: an empty path, a replacement that is not a material, a row an earlier one already covers, or an unreadable tag."), FText::AsNumber(NumSkipped)));
	}

	Managed->Collection = Variant;
	Component->AddToManagedResources(Managed);

	return CompleteWith(FSoftObjectPath(Variant));
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
