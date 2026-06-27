// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPreloadResources.h"

#include "PCGComponent.h"
#include "PCGExLog.h"
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "PCGModule.h"
#include "PCGPin.h"

#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGExPreloadResourcesElement"
#define PCGEX_NAMESPACE PreloadResources

namespace PCGExPreloadResources
{
	const FName SourcesLabel = FName(TEXT("Sources"));
}

#pragma region UPCGExManagedPreloadedResources

bool UPCGExManagedPreloadedResources::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	// Honor the soft/hard contract so the resource survives a regeneration and can be reused. PCG's
	// start-of-generation cleanup calls Release(bHardRelease=false); a reusable resource must KEEP its
	// holds and return false there (the base marks it unused so the Preload element can MarkAsReused
	// it). Only a hard release -- the end-of-gen sweep of an un-reused resource via ReleaseIfUnused, or
	// teardown -- actually drops residency. We hold no actors, so OutActorsToDelete is untouched.
	if (bHardRelease)
	{
		DropResidency();
	}
	return Super::Release(bHardRelease, OutActorsToDelete);
}

void UPCGExManagedPreloadedResources::DropResidency()
{
	// Releasing the handles drops the streamable keep-alive (and the last subsystem-cache ref if
	// this was the only holder); clearing the hard refs unroots the assets for GC.
	Handles.Empty();
	RootedAssets.Empty();
	Paths.Empty();
}

#pragma endregion

#pragma region UPCGExPreloadResourcesSettings

#if WITH_EDITOR
FName UPCGExPreloadResourcesSettings::GetEnumName() const
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExPreloadResourcesMode>())
	{
		return FName(EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(Mode)).ToString());
	}
	return NAME_None;
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGExPreloadResourcesSettings::GetPreconfiguredInfo() const
{
	// One palette entry per mode (Preload / Release).
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExPreloadResourcesMode>();
}
#endif

void UPCGExPreloadResourcesSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExPreloadResourcesMode>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Mode = static_cast<EPCGExPreloadResourcesMode>(PreconfigureInfo.PreconfiguredIndex);
			bExecutionDependencyRequired = Mode == EPCGExPreloadResourcesMode::Release;
		}
	}
}

TArray<FPCGPinProperties> UPCGExPreloadResourcesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Opt-in input: PCG forces the first input pin to be Required, so we only add it while the user has
	// asked to load from inputs (and only in Preload mode) -- otherwise the node has no input pins.
	if (Mode == EPCGExPreloadResourcesMode::Preload && bLoadFromInputs)
	{
		PCGEX_PIN_ANY(PCGExPreloadResources::SourcesLabel, "Points or attribute sets carrying FSoftObjectPath (or string-path) attributes to preload.", Required)
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPreloadResourcesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Dependency-only output: lets downstream nodes order themselves *after* this preload without any
	// data flowing through. Resources come entirely from settings, so there are no data pins.
	PCGEX_PIN_DEPENDENCY(PCGPinConstants::DefaultOutputLabel)

	return PinProperties;
}

#pragma endregion

#pragma region FPCGExPreloadResourcesElement

PCGEX_INITIALIZE_ELEMENT(PreloadResources)

namespace PCGExPreloadResources
{
	// Map the user-facing load flags onto the collection's recursion enum. Returns false when no asset
	// bit is set (CustomProperties is an orthogonal axis, handled separately by the caller).
	bool ResolveCollectionAssetFlags(const EPCGExCollectionLoadFlags InFlags, PCGExAssetCollection::ELoadingFlags& OutFlags)
	{
		const bool bDirect = EnumHasAnyFlags(InFlags, EPCGExCollectionLoadFlags::DirectAssets);
		const bool bSub = EnumHasAnyFlags(InFlags, EPCGExCollectionLoadFlags::SubCollections);

		if (bDirect && bSub)
		{
			OutFlags = PCGExAssetCollection::ELoadingFlags::Recursive;
			return true;
		}
		if (bSub)
		{
			OutFlags = PCGExAssetCollection::ELoadingFlags::RecursiveCollectionsOnly;
			return true;
		}
		if (bDirect)
		{
			OutFlags = PCGExAssetCollection::ELoadingFlags::Default;
			return true;
		}
		return false;
	}

	// Order-independent CRC of a sorted path set, used only as an in-process reuse key (the resource
	// also stores the sorted paths for an exact, collision-proof match). The sorted input makes the
	// HashCombine fold order-independent, and GetTypeHash avoids a heap FString per path.
	FPCGCrc ComputeSortedPathsCrc(const TArray<FSoftObjectPath>& InSortedPaths)
	{
		uint32 Hash = 0;
		for (const FSoftObjectPath& Path : InSortedPaths)
		{
			Hash = HashCombine(Hash, GetTypeHash(Path));
		}
		return FPCGCrc(Hash);
	}

	// First FSoftObjectPath attribute on the metadata. NAME_None if there's none / no metadata.
	FName FindFirstSoftObjectPathAttribute(const UPCGMetadata* Metadata)
	{
		if (!Metadata)
		{
			return NAME_None;
		}

		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		Metadata->GetAttributes(Names, Types);
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			if (Types[i] == EPCGMetadataTypes::SoftObjectPath)
			{
				return Names[i];
			}
		}
		return NAME_None;
	}

	// Which attribute to read soft paths from on a given input: the configured selector (resolved to a
	// concrete attribute name via the shared helper, which fixes @Last); when that doesn't name a
	// present attribute, the first FSoftObjectPath attribute on the data.
	FName ResolveAssetAttribute(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData)
	{
		const UPCGMetadata* Metadata = InData ? InData->ConstMetadata() : nullptr;

		FName Name = NAME_None;
		if (PCGExMetaHelpers::TryGetAttributeName(InSelector, InData, Name) && !Name.IsNone() && Metadata && Metadata->HasAttribute(Name))
		{
			return Name;
		}
		return FindFirstSoftObjectPathAttribute(Metadata);
	}
}

bool FPCGExPreloadResourcesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPreloadResourcesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PreloadResources)
	PCGEX_EXECUTION_CHECK

	// CanExecuteOnlyOnMainThread => we're on the game thread, so the blocking loads below can marshal
	// cache misses inline without risking the cancel deadlock. Null component means the execution
	// source isn't a UPCGComponent -- nowhere to anchor managed resources, so there's nothing to do.
	UPCGComponent* Component = Context->GetMutableComponent();
	if (!Component)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Preload/Release Resources needs an owning PCG component to manage resource lifetime; nothing was done."));
		Context->Done();
		return Context->TryComplete();
	}

	if (Settings->Mode == EPCGExPreloadResourcesMode::Release)
	{
		// Drop every resource this node-type preloaded on the component. They are not re-marked, so
		// PCG's end-of-generation sweep then removes the now-empty resources from the managed set.
		int32 ReleasedCount = 0;
		Component->ForEachManagedResource(
			[&ReleasedCount](UPCGManagedResource* InResource)
			{
				if (UPCGExManagedPreloadedResources* Typed = Cast<UPCGExManagedPreloadedResources>(InResource))
				{
					Typed->DropResidency();
					++ReleasedCount;
				}
			});

		if (ReleasedCount == 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Release Resources found no preloaded resources to release on this component."));
		}

		Context->Done();
		return Context->TryComplete();
	}

	// --- Preload ---

	const EPCGExCollectionLoadFlags LoadFlags = static_cast<EPCGExCollectionLoadFlags>(Settings->CollectionLoadFlags);
	PCGExAssetCollection::ELoadingFlags AssetFlags = PCGExAssetCollection::ELoadingFlags::Default;
	const bool bGatherCollectionAssets = PCGExPreloadResources::ResolveCollectionAssetFlags(LoadFlags, AssetFlags);
	const bool bGatherProperties = EnumHasAnyFlags(LoadFlags, EPCGExCollectionLoadFlags::CustomProperties);

	TSet<FSoftObjectPath> PathSet;

	// 1) Asset collections. Each must be resident before we can introspect it, so batch-load the
	//    collection assets themselves first (this also warms them in the subsystem cache), then gather
	//    each one's referenced assets / custom-property soft paths.
	if (!Settings->Collections.IsEmpty())
	{
		const TSharedRef<TSet<FSoftObjectPath>> CollectionPaths = MakeShared<TSet<FSoftObjectPath>>();
		for (const TSoftObjectPtr<UPCGExAssetCollection>& Collection : Settings->Collections)
		{
			const FSoftObjectPath Path = Collection.ToSoftObjectPath();
			if (Path.IsValid())
			{
				CollectionPaths->Add(Path);
			}
		}

		if (!CollectionPaths->IsEmpty())
		{
			// Game-thread blocking load (NOT LoadSynchronous) to resolve the collection objects so we
			// can introspect them. Hold the handles for the duration of the introspection loop below so
			// the collections stay resident even when there is no subsystem warm cache; the collection
			// paths also re-enter the final managed resource via PathSet, which owns residency long-term.
			const TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> CollectionHandles = PCGExHelpers::LoadAndCacheBlocking_AnyThread(CollectionPaths, nullptr, true);

			for (const TSoftObjectPtr<UPCGExAssetCollection>& Collection : Settings->Collections)
			{
				const UPCGExAssetCollection* Resolved = Collection.Get();
				if (!Resolved)
				{
					continue;
				}

				// Keep the collection asset itself resident alongside its contents.
				PathSet.Add(Collection.ToSoftObjectPath());

				if (bGatherCollectionAssets)
				{
					Resolved->GetAssetPaths(PathSet, AssetFlags);
				}
				if (bGatherProperties)
				{
					Resolved->GatherPropertySoftObjectPaths(PathSet);
				}
			}
		}
	}

	// 2) Standalone assets.
	for (const TSoftObjectPtr<UObject>& Asset : Settings->Assets)
	{
		const FSoftObjectPath Path = Asset.ToSoftObjectPath();
		if (Path.IsValid())
		{
			PathSet.Add(Path);
		}
	}

	// 3) Soft object paths read from connected input data (opt-in). The input pin only exists when
	//    bLoadFromInputs is set (see InputPinProperties), so there is nothing to read otherwise.
	if (Settings->bLoadFromInputs)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGExPreloadResources::SourcesLabel);
		if (!Inputs.IsEmpty())
		{
			// Reads are independent and read-only, so fan out across inputs; merge into the (non-thread-safe)
			// PathSet sequentially afterwards.
			TArray<TArray<FSoftObjectPath>> PerInput;
			PerInput.SetNum(Inputs.Num());
			PCGExMT::ParallelOrSequential(
				Inputs.Num(),
				[&](const int32 i)
				{
					const UPCGData* Data = Inputs[i].Data;
					if (!Data)
					{
						return;
					}

					// Resolve which attribute holds the asset reference (selector / @Last, else the first
					// FSoftObjectPath attribute), then bulk-read it (with an FString-path fallback).
					const FName AttributeName = PCGExPreloadResources::ResolveAssetAttribute(Settings->AssetAttribute, Data);
					if (AttributeName.IsNone())
					{
						return;
					}

					PCGExData::Helpers::BulkReadSoftPaths(Data, AttributeName, PerInput[i]);
				},
				/*Threshold=*/2, EParallelForFlags::Unbalanced);

			for (const TArray<FSoftObjectPath>& InputPaths : PerInput)
			{
				for (const FSoftObjectPath& Path : InputPaths)
				{
					if (Path.IsValid())
					{
						PathSet.Add(Path);
					}
				}
			}
		}
	}

	if (PathSet.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Preload Resources resolved no assets to load; check the configured collections / assets and load flags."));
		Context->Done();
		return Context->TryComplete();
	}

	// Stable, order-independent identity: sort the resolved paths, then CRC them. The resource also
	// stores the sorted set so reuse is validated exactly (ruling out a 32-bit CRC collision).
	TArray<FSoftObjectPath> Paths = PathSet.Array();
	Paths.Sort(FSoftObjectPathLexicalLess()); // alloc-free comparison; deterministic order keeps the CRC stable
	const FPCGCrc Crc = PCGExPreloadResources::ComputeSortedPathsCrc(Paths);

	// Change-tracking is per-execution and must run on EVERY pass (create AND reuse), so register it
	// here -- before the reuse early-out -- keyed on the resolved paths. Editor-only; no-op at runtime.
#if WITH_EDITOR
	if (Settings->bTrackResources)
	{
		for (const FSoftObjectPath& Path : Paths)
		{
			Context->EDITOR_TrackPath(Path);
		}
	}

	if (Settings->bLogLoadedResources)
	{
		UE_LOG(LogPCGEx, Log, TEXT("================== Preloading from %s | %s"), *GetNameSafe(Context->GetComponent()->GetOwner()), *Context->Node->GetGraph()->GetFName().ToString())
		for (const FSoftObjectPath& Path : Paths)
		{
			UE_LOG(LogPCGEx, Log, TEXT("+ %s"), *Path.ToString())
		}
	}
#endif

	// Reuse an identical preloaded set already on this component: keep it (MarkAsReused) and skip the
	// reload entirely -- the existing resource still holds the handles and GC roots.
	if (PCGExManagedHelpers::TryReuseManagedResource<UPCGExManagedPreloadedResources>(
		Component, Crc,
		[&Paths](const UPCGExManagedPreloadedResources* Existing)
		{
			return Existing->Paths == Paths;
		}))
	{
		Context->Done();
		return Context->TryComplete();
	}

	// Fresh set: one batched, cache-warming load. InContext is null on purpose -- residency is owned
	// durably by the managed resource (across runs), not by the current execution.
	const TSharedRef<TSet<FSoftObjectPath>> LoadSet = MakeShared<TSet<FSoftObjectPath>>(MoveTemp(PathSet));
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> Handles = PCGExHelpers::LoadAndCacheBlocking_AnyThread(LoadSet, nullptr, true);

	// Resolve the loaded objects to hard-root them (belt-and-suspenders alongside the streamable
	// handles). If NOTHING resolved -- e.g. every path is a deleted/unbuildable asset -- don't register
	// an empty resource: it would hold no residency yet satisfy future reuse forever, silently
	// reporting success.
	TArray<TObjectPtr<UObject>> RootedAssets;
	RootedAssets.Reserve(Paths.Num());
	for (const FSoftObjectPath& Path : Paths)
	{
		if (UObject* Obj = Path.ResolveObject())
		{
			RootedAssets.Add(Obj);
		}
	}

	if (RootedAssets.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Preload Resources: none of the resolved paths loaded to a live asset; nothing was kept resident."));
		Context->Done();
		return Context->TryComplete();
	}

	UPCGExManagedPreloadedResources* Managed = NewObject<UPCGExManagedPreloadedResources>(Component);
	Managed->SetCrc(Crc);
	Managed->Handles = MoveTemp(Handles);
	Managed->RootedAssets = MoveTemp(RootedAssets);
	Managed->Paths = MoveTemp(Paths);

	Component->AddToManagedResources(Managed);

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
