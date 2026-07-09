// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Build Asset Collection - Builds a transient asset collection from an input attribute set and outputs its
// soft path so a Staging : Distribute node can consume it via its SourceCollection (Constant) override pin.

#include "Elements/PCGExBuildAssetCollection.h"

#include "PCGComponent.h"
#include "PCGExLog.h"
#include "PCGParamData.h"
#include "PCGPin.h"

#include "Collections/PCGExMeshCollection.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
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

#pragma region UPCGExBuildAssetCollectionSettings

UPCGExBuildAssetCollectionSettings::UPCGExBuildAssetCollectionSettings()
{
	// Mesh is the sensible default: the common case, and the only type that can rebuild staging outside the
	// editor. bSupportCustomType stays true, so it's user-changeable.
	AttributeSetDetails.AssetCollectionType = UPCGExMeshCollection::StaticClass();
}

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
	if (InPin && InPin->IsOutputPin())
	{
		// Tag the subtype so the output type-matches soft-path override pins (Distribute's SourceCollection/Constant).
		FPCGDataTypeIdentifier Id = FPCGDataTypeInfoParam::AsId();
		Id.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::SoftObjectPath);
		return Id;
	}

	// Arbitrary attribute set -- no single subtype, leave it generic.
	return FPCGDataTypeInfoParam::AsId();
}

PCGEX_INITIALIZE_ELEMENT(BuildAssetCollection)

#pragma endregion

#pragma region FPCGExBuildAssetCollectionElement

namespace PCGExBuildAssetCollection
{
	// Config-axis half of the reuse key (folded with the input's data CRC in AdvanceWork).
	FString BuildConfigId(const FPCGExRoamingAssetCollectionDetails& Details)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|%s"),
			*GetPathNameSafe(Details.AssetCollectionType.Get()),
			*Details.AssetPathSourceAttribute.ToString(),
			*Details.WeightSourceAttribute.ToString(),
			*Details.CategorySourceAttribute.ToString());
	}
}

bool FPCGExBuildAssetCollectionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildAssetCollectionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildAssetCollection)
	PCGEX_EXECUTION_CHECK

	check(IsInGameThread()); // guaranteed by CanExecuteOnlyOnMainThread

	// Emit the resolved path (empty when the build can't happen).
	auto OutputPath = [&](const FSoftObjectPath& InPath)
	{
		UPCGParamData* OutData = Context->ManagedObjects->New<UPCGParamData>();
		check(OutData && OutData->Metadata);

		FPCGMetadataAttribute<FSoftObjectPath>* PathAttr = OutData->Metadata->CreateAttribute<FSoftObjectPath>(
			Settings->OutputAttributeName, FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

		const int64 EntryKey = OutData->Metadata->AddEntry();
		if (PathAttr) { PathAttr->SetValue(EntryKey, InPath); }

		FPCGTaggedData& Tagged = Context->OutputData.TaggedData.Emplace_GetRef();
		Tagged.Pin = PCGPinConstants::DefaultOutputLabel;
		Tagged.Data = OutData;
	};

	auto CompleteWith = [&](const FSoftObjectPath& InPath) -> bool
	{
		OutputPath(InPath);
		Context->Done();
		return Context->TryComplete();
	};

	// No collection type -> nothing to build.
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

	// Reuse key = config string + the input's full data CRC. A 32-bit collision only risks reusing a stale
	// collection until the next input change, never a crash.
	const FString ConfigId = PCGExBuildAssetCollection::BuildConfigId(Settings->AttributeSetDetails);
	uint32 Hash = GetTypeHash(ConfigId);
	if (const FPCGCrc DataCrc = InParam->GetOrComputeCrc(/*bFullDataCrc=*/true); DataCrc.IsValid())
	{
		Hash = HashCombine(Hash, DataCrc.GetValue());
	}
	const FPCGCrc Crc(Hash);

	// Reuse an identical collection already on this component.
	if (const UPCGExManagedAssetCollection* Existing = PCGExManagedHelpers::TryReuseManagedResource<UPCGExManagedAssetCollection>(
		Component, Crc,
		[&ConfigId](const UPCGExManagedAssetCollection* Candidate) { return Candidate->Collection && Candidate->Config == ConfigId; }))
	{
		return CompleteWith(FSoftObjectPath(Existing->Collection));
	}

	// Outer resource->component (persists across gens) and collection->resource, so the collection's soft path
	// is unique + resolvable across the node boundary.
	UPCGExManagedAssetCollection* Managed = NewObject<UPCGExManagedAssetCollection>(Component);
	Managed->SetCrc(Crc);
	Managed->Config = ConfigId;

	UPCGExAssetCollection* Collection = NewObject<UPCGExAssetCollection>(
		Managed, Settings->AttributeSetDetails.AssetCollectionType.Get(), NAME_None, RF_Transient);

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

	return CompleteWith(FSoftObjectPath(Collection));
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
