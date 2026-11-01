// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Build Asset Collection - Builds a transient asset collection from an input attribute set and outputs its
// soft path so a Staging : Distribute node can consume it via its SourceCollection (Constant) override pin.

#include "Elements/PCGExBuildAssetCollection.h"

#include "PCGComponent.h"
#include "PCGExLog.h"
#include "PCGParamData.h"
#include "PCGPin.h"

#include "Collections/PCGExOmniCollection.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Helpers/PCGExCollectionAssetSave.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "PCGExCollectionsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExBuildAssetCollectionElement"
#define PCGEX_NAMESPACE BuildAssetCollection

#pragma region UPCGExManagedAssetCollection

bool UPCGExManagedAssetCollection::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	// Honor the soft/hard contract: keep state on soft cleanup (so the node can MarkAsReused it), drop only on
	// hard release. Returning bHardRelease unconditionally would destroy the collection every generation.
	if (bHardRelease)
	{
		Collection = nullptr;
		Config.Reset();
	}
	return Super::Release(bHardRelease, OutActorsToDelete);
}

#pragma endregion

#pragma region PCGExBuildAssetCollection

namespace PCGExBuildAssetCollection
{
	bool TryGetPathPinTypesID(const UPCGPin* InPin, FPCGDataTypeIdentifier& OutId)
	{
		// UPCGSettings::GetCurrentPinTypesID dereferences InPin unconditionally (PCGSettings.cpp, its tail).
		if (!InPin)
		{
			OutId = FPCGDataTypeInfoParam::AsId();
			return true;
		}

		if (!InPin->IsOutputPin())
		{
			return false;
		}

		OutId = FPCGDataTypeInfoParam::AsId();
		OutId.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::SoftObjectPath);
		return true;
	}

	bool CompleteWithPath(FPCGExContext* InContext, const FName InAttributeName, const FSoftObjectPath& InPath)
	{
		UPCGParamData* OutData = InContext->ManagedObjects->New<UPCGParamData>();
		check(OutData && OutData->Metadata);

		FPCGMetadataAttribute<FSoftObjectPath>* PathAttr = OutData->Metadata->CreateAttribute<FSoftObjectPath>(
			InAttributeName, FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

		const int64 EntryKey = OutData->Metadata->AddEntry();
		if (PathAttr) { PathAttr->SetValue(EntryKey, InPath); }

		InContext->StageOutput(OutData, PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::Managed);
		InContext->Done();
		return InContext->TryComplete();
	}

	FPCGCrc ComputeReuseCrc(const FString& InConfigId, const TConstArrayView<const UPCGParamData*> InInputs)
	{
		// A 32-bit collision only risks reusing a stale collection until the next input change, never a crash.
		uint32 Hash = GetTypeHash(InConfigId);
		for (const UPCGParamData* Input : InInputs)
		{
			if (const FPCGCrc DataCrc = Input->GetOrComputeCrc(/*bFullDataCrc=*/true); DataCrc.IsValid())
			{
				Hash = HashCombine(Hash, DataCrc.GetValue());
			}
		}
		return FPCGCrc(Hash);
	}
}

#pragma endregion

#pragma region UPCGExBuildAssetCollectionSettings

TArray<FPCGPinProperties> UPCGExBuildAssetCollectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceAssetCollection, "Attribute set describing the collection entries (asset path, optional weight/category).", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBuildAssetCollectionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(PCGPinConstants::DefaultOutputLabel, "Attribute set carrying the built collection's soft path under OutputAttributeName. Wire into Staging : Distribute's SourceCollection (Constant) override pin.", Normal)
	return PinProperties;
}

FPCGDataTypeIdentifier UPCGExBuildAssetCollectionSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	FPCGDataTypeIdentifier Id;
	return PCGExBuildAssetCollection::TryGetPathPinTypesID(InPin, Id) ? Id : Super::GetCurrentPinTypesID(InPin);
}

PCGEX_INITIALIZE_ELEMENT(BuildAssetCollection)

#pragma endregion

#pragma region FPCGExBuildAssetCollectionElement

bool FPCGExBuildAssetCollectionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildAssetCollectionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildAssetCollection)
	PCGEX_EXECUTION_CHECK

	check(IsInGameThread()); // guaranteed by CanExecuteOnlyOnMainThread

	// Emit the resolved path (empty when the build can't happen).
	auto CompleteWith = [&](const FSoftObjectPath& InPath) -> bool
	{
		return PCGExBuildAssetCollection::CompleteWithPath(Context, Settings->OutputAttributeName, InPath);
	};

	// No asset path attribute -> nothing to build.
	if (!Settings->AttributeSetDetails.Validate(Context))
	{
		return CompleteWith(FSoftObjectPath());
	}

	const UPCGParamData* InParam = nullptr;
	for (const FPCGTaggedData& Tagged : Context->InputData.GetInputsByPin(PCGExCollections::Labels::SourceAssetCollection))
	{
		InParam = Cast<UPCGParamData>(Tagged.Data);
		if (InParam) { break; }
	}

	if (!InParam || !InParam->ConstMetadata())
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Build Asset Collection needs one input attribute set."));
		return CompleteWith(FSoftObjectPath());
	}

	// No component -> nowhere to anchor lifetime; the soft path would dangle.
	UPCGComponent* Component = Context->GetMutableComponent();
	if (!Component)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Build Asset Collection needs an owning PCG component to manage collection lifetime; no collection was built."));
		return CompleteWith(FSoftObjectPath());
	}

	const FString ConfigId = Settings->AttributeSetDetails.GetConfigId();
	const FPCGCrc Crc = PCGExBuildAssetCollection::ComputeReuseCrc(ConfigId, MakeConstArrayView(&InParam, 1));

	// A rebuild always writes; a reuse hit writes only when the target is missing, so identical input never
	// re-writes and deleting the saved asset re-creates it. No editor guard: the helpers no-op there.
	auto TrySaveToAsset = [&](const bool bOnlyWhenMissing)
	{
		if (!Settings->bSaveToAsset || !PCGExAssetSave::CanWriteSourceContent(Context)) { return; }

		FString PackagePath;
		FString AssetName;
		FText ResolveError;
		if (!PCGExAssetSave::ResolveTarget(Settings->SaveTarget, PackagePath, AssetName, ResolveError))
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, ResolveError);
			return;
		}

		if (bOnlyWhenMissing && PCGExAssetSave::TargetAssetExists(Settings->SaveTarget)) { return; }

		if (PCGExCollectionSave::SaveOmniFromAttributeSet(Settings->SaveTarget, Context, InParam, Settings->AttributeSetDetails))
		{
			// Settings-scoped target: with several components on this graph, the last to execute wins.
			UE_LOG(LogPCGEx, Log, TEXT("Build Asset Collection saved '%s' (source: %s)."), *PackagePath, *Context->GetExecutionSourceName());
		}
	};

	// Reuse an identical collection already on this component.
	if (const UPCGExManagedAssetCollection* Existing = PCGExManagedHelpers::TryReuseManagedResource<UPCGExManagedAssetCollection>(
		Component, Crc,
		[&ConfigId](const UPCGExManagedAssetCollection* Candidate) { return Candidate->Collection && Candidate->Config == ConfigId; }))
	{
		TrySaveToAsset(/*bOnlyWhenMissing=*/true);
		return CompleteWith(FSoftObjectPath(Existing->Collection));
	}

	// Outer resource->component (persists across gens) and collection->resource, so the collection's soft path
	// is unique + resolvable across the node boundary.
	UPCGExManagedAssetCollection* Managed = NewObject<UPCGExManagedAssetCollection>(Component);
	Managed->SetCrc(Crc);
	Managed->Config = ConfigId;

	UPCGExOmniCollection* Collection = NewObject<UPCGExOmniCollection>(Managed, NAME_None, RF_Transient);

	// Bake staging now (RebuildStagingData self-loads each asset on the GT) so downstream consumes the
	// collection exactly like a saved asset.
	if (!Collection || !PCGExCollectionHelpers::BuildFromAttributeSet(Collection, Context, InParam, Settings->AttributeSetDetails, /*bBuildStaging=*/true))
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Failed to build collection from the input attribute set."));
		return CompleteWith(FSoftObjectPath());
	}

	Collection->LoadCache(); // warm the cache for consumers

	Managed->Collection = Collection;
	Component->AddToManagedResources(Managed);

	TrySaveToAsset(/*bOnlyWhenMissing=*/false);

	return CompleteWith(FSoftObjectPath(Collection));
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
