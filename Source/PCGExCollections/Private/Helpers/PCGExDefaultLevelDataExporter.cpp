// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDefaultLevelDataExporter.h"

#include "PCGDataAsset.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorMeshClassificator.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "PCGExAssemblyRoot.h"
#include "PCGExCollectionsSettingsCache.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Core/PCGExExportSlots.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExLevelExportBuiltinHandlers.h"
#include "Helpers/PCGExLevelExportHandler.h"
#include "Helpers/PCGExLevelExportShared.h"
#include "Metadata/PCGMetadata.h"
#endif

UPCGExDefaultLevelDataExporter::UPCGExDefaultLevelDataExporter(const FObjectInitializer& ObjectInitializer)
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;

	UClass* FilterClass = Settings.DefaultContentFilterClass
		? Settings.DefaultContentFilterClass.Get()
		: UPCGExDefaultActorContentFilter::StaticClass();

	UClass* EvalClass = Settings.DefaultBoundsEvaluatorClass
		? Settings.DefaultBoundsEvaluatorClass.Get()
		: UPCGExDefaultBoundsEvaluator::StaticClass();

	UClass* ClassificatorClass = Settings.DefaultMeshClassificatorClass
		? Settings.DefaultMeshClassificatorClass.Get()
		: UPCGExDefaultActorMeshClassificator::StaticClass();

	ContentFilter = Cast<UPCGExActorContentFilter>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("ContentFilter"),
		                                         UPCGExActorContentFilter::StaticClass(), FilterClass, false, false));

	MeshClassificator = Cast<UPCGExActorMeshClassificator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("MeshClassificator"),
		                                         UPCGExActorMeshClassificator::StaticClass(), ClassificatorClass, false, false));

	BoundsEvaluator = Cast<UPCGExBoundsEvaluator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("BoundsEvaluator"),
		                                         UPCGExBoundsEvaluator::StaticClass(), EvalClass, false, false));
}

EPCGExActorExportType UPCGExDefaultLevelDataExporter::ClassifyActor(AActor* Actor) const
{
	// Level instances precede the mesh check: an ALevelInstance can incidentally have
	// a UStaticMeshComponent (gizmo/visualizer) but the meaningful payload is the
	// referenced UWorld asset.
	if (const ALevelInstance* LI = Cast<ALevelInstance>(Actor))
	{
		if (!LI->GetWorldAsset().IsNull())
		{
			return EPCGExActorExportType::Level;
		}
		// Empty world ref -- nothing to embed; fall through to actor-style export so
		// transform/tags survive the round-trip.
	}

	if (MeshClassificator && MeshClassificator->ShouldClassifyAsMesh(Actor))
	{
		// Any UStaticMeshComponent (or subclass -- ISMC, HISM, splines, future kinds)
		// with a valid mesh AND geometry to contribute qualifies the actor as a Mesh
		// container. ISMCs with zero instances contribute nothing and don't count.
		TInlineComponentArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents<UStaticMeshComponent>(SMCs);
		for (UStaticMeshComponent* SMC : SMCs)
		{
			if (!SMC || !SMC->GetStaticMesh())
			{
				continue;
			}
			if (const UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
			{
				if (ISMC->GetInstanceCount() == 0)
				{
					continue;
				}
			}
			return EPCGExActorExportType::Mesh;
		}
	}

	return EPCGExActorExportType::Actor;
}

void UPCGExDefaultLevelDataExporter::OnExportComplete(UPCGDataAsset* OutAsset)
{
	// Default: no-op. Override for custom post-export logic.
}

bool UPCGExDefaultLevelDataExporter::ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset)
{
	FPCGExLevelExportContext EmptyContext;
	return ExportLevelData(FPCGExLevelExportSource::FromWorld(World), OutAsset, EmptyContext);
}

#if WITH_EDITOR

namespace PCGExDefaultLevelDataExporter
{
	FName SlotForClassification(const EPCGExActorExportType Type)
	{
		switch (Type)
		{
		case EPCGExActorExportType::Mesh: return PCGExLevelExport::Slots::Meshes;
		case EPCGExActorExportType::Actor: return PCGExLevelExport::Slots::Actors;
		case EPCGExActorExportType::Level: return PCGExLevelExport::Slots::Levels;
		case EPCGExActorExportType::Skip: return NAME_None;
		default:
			ensureMsgf(false, TEXT("Unhandled EPCGExActorExportType"));
			return NAME_None;
		}
	}
}

bool UPCGExDefaultLevelDataExporter::ExportLevelData(const FPCGExLevelExportSource& Source, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext)
{
	if (!Source.IsValid() || !OutAsset)
	{
		return false;
	}

	// Never writes Tag_EntryIdx for shared slots and never emplaces the CollectionMap pin: shared
	// entries are handed back as captures and the caller's compaction resolves final indices. Per-entry
	// slots are built and hashed here (they have no cross-entry mutualization story).

	// Move any previous inner subobjects to the transient package so they get GC'd instead of being
	// saved as orphan exports in the collection's .uasset -- accumulated dead inners have crashed
	// save-time pointer traversal.
	{
		TArray<UObject*> OldInners;
		GetObjectsWithOuter(OutAsset, OldInners, false);
		for (UObject* Inner : OldInners)
		{
			Inner->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	using namespace PCGExDefaultLevelDataExporter;

	TArray<PCGExLevelExport::FHandlerRegistration> Registrations;
	PCGExLevelExport::FHandlerRegistry::Get().GetRegistrations(Registrations);
	if (!bUseRegisteredHandlers)
	{
		Registrations.RemoveAll([](const PCGExLevelExport::FHandlerRegistration& R) { return !PCGExLevelExport::IsBuiltinHandlerClass(R.HandlerClass); });
	}

	TArray<const UPCGExLevelExportHandler*> Handlers;
	Handlers.Reserve(Registrations.Num());
	for (const PCGExLevelExport::FHandlerRegistration& Registration : Registrations)
	{
		Handlers.Add(Registration.HandlerClass->GetDefaultObject<UPCGExLevelExportHandler>());
	}

	// Claim pass. The root is a candidate too (handlers may harvest its components) but never an entry.
	TArray<FPCGExExportCandidate> Candidates;
	if (Source.Root)
	{
		FPCGExExportCandidate& Root = Candidates.AddDefaulted_GetRef();
		Root.Actor = Source.Root;
		Root.bIsRoot = true;
	}

	for (AActor* Actor : Source.Actors)
	{
		if (!Actor)
		{
			continue;
		}

		// An assembly root met INSIDE the export (a module cage in a level, a root actor nested in a
		// subtree) authors nothing itself and is never an entry; like the export root, only its components
		// are offered to handlers. Decided ahead of the content filter, whose tags mean "not an entry".
		if (Actor->Implements<UPCGExAssemblyRoot>())
		{
			if (!UPCGExActorContentFilter::IsInfrastructureActor(Actor))
			{
				FPCGExExportCandidate& RootLike = Candidates.AddDefaulted_GetRef();
				RootLike.Actor = Actor;
				RootLike.bIsRoot = true;
			}
			continue;
		}

		if (!UPCGExActorContentFilter::StaticPassesFilter(ContentFilter, Actor))
		{
			continue;
		}

		FName ClaimedSlot = NAME_None;
		for (int32 i = 0; i < Handlers.Num(); i++)
		{
			if (Handlers[i]->Claim(Actor, Source, this))
			{
				ClaimedSlot = Registrations[i].Desc.SlotId;
				break;
			}
		}
		if (ClaimedSlot.IsNone())
		{
			ClaimedSlot = SlotForClassification(ClassifyActor(Actor));
			if (ClaimedSlot.IsNone())
			{
				continue;
			}
		}

		FPCGExExportCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Actor = Actor;
		Candidate.ClaimedSlot = ClaimedSlot;
	}

	if (Candidates.IsEmpty())
	{
		return false;
	}

	// Harvest pass: every handler sees every candidate.
	TArray<TUniquePtr<FPCGExExportSlotWriter>> Writers;
	Writers.Reserve(Registrations.Num());
	for (int32 i = 0; i < Registrations.Num(); i++)
	{
		const PCGExLevelExport::FHandlerRegistration& Registration = Registrations[i];
		FPCGExExportSlotWriter& Writer = *Writers.Add_GetRef(MakeUnique<FPCGExExportSlotWriter>(Registration.Desc));

		for (const FPCGExExportCandidate& Candidate : Candidates)
		{
			Handlers[i]->Collect(Candidate, Source, this, Writer);
		}

		// Shared captures live under the caller's outer (the host collection); per-entry collections under
		// the exported asset. Instanced subobjects a handler minted must follow, or they cross packages
		// once the exported asset is externalized. The writer is their sole owner, so they are moved.
		UObject* EntryOuter = (Registration.Desc.Scope == EPCGExExportSlotScope::Shared && OutContext.CaptureOuter) ? OutContext.CaptureOuter : OutAsset;
		Handlers[i]->FinalizeSlot(Writer, EntryOuter, this);
		for (FInstancedStruct& Entry : Writer.Entries)
		{
			PCGExCollectionHelpers::ReparentInstancedSubobjects(Registration.Desc.EntryStruct, Entry.GetMutableMemory(), EntryOuter);
		}
	}

	// Emit one point data per non-empty slot, in handler order.
	TArray<UPCGBasePointData*> SlotPointData;
	SlotPointData.Init(nullptr, Registrations.Num());
	for (int32 i = 0; i < Registrations.Num(); i++)
	{
		if (!Writers[i]->IsEmpty())
		{
			SlotPointData[i] = EmitSlotPoints(*Writers[i], Handlers[i], OutAsset);
		}
	}

	OnExportComplete(OutAsset);

	if (bGenerateCollections)
	{
		for (int32 i = 0; i < Registrations.Num(); i++)
		{
			const PCGExLevelExport::FHandlerRegistration& Registration = Registrations[i];
			FPCGExExportSlotWriter& Writer = *Writers[i];
			if (Writer.IsEmpty() || !SlotPointData[i])
			{
				continue;
			}

			if (Registration.Desc.Scope == EPCGExExportSlotScope::PerEntry)
			{
				BuildEmbeddedSlot(Registration, Writer, OutAsset, SlotPointData[i], OutContext);
				continue;
			}

			if (!OutContext.Captures)
			{
				continue;
			}

			// Local picks (low 16 = local entry index, high 16 = sec+1). Tag_EntryIdx stays unwritten; the
			// caller resolves shared indices during compaction.
			FPCGExExportSlotCapture& Capture = PCGExExportSlots::FindOrAdd(*OutContext.Captures, Registration.Desc.SlotId);
			Capture.LocalPicks.SetNumUninitialized(Writer.Items.Num());
			for (int32 p = 0; p < Writer.Items.Num(); p++)
			{
				const FPCGExExportItem& Item = Writer.Items[p];
				if (Item.LocalEntryIndex < 0)
				{
					Capture.LocalPicks[p] = -1;
					continue;
				}
				const int16 Sec = Registration.Desc.bSupportsSecondary ? Item.SecondaryIndex : static_cast<int16>(-1);
				Capture.LocalPicks[p] = FPCGExLevelExportContext::PackLocalPick(Item.LocalEntryIndex, Sec);
			}
			Capture.Entries = MoveTemp(Writer.Entries);
			Capture.InheritedDefaults = MoveTemp(Writer.InheritedDefaults);
		}
	}

	return OutAsset->Data.TaggedData.Num() > 0;
}

UPCGBasePointData* UPCGExDefaultLevelDataExporter::EmitSlotPoints(const FPCGExExportSlotWriter& Writer, const UPCGExLevelExportHandler* Handler, UPCGDataAsset* OutAsset) const
{
	using namespace PCGExLevelExportShared;

	const int32 NumPoints = Writer.Items.Num();

	TPCGValueRange<FTransform> Transforms;
	TPCGValueRange<FVector> BMin, BMax;
	UPCGBasePointData* PointData = CreatePointData(OutAsset, NumPoints, Transforms, BMin, BMax);

	for (int32 i = 0; i < NumPoints; i++)
	{
		const FPCGExExportItem& Item = Writer.Items[i];
		Transforms[i] = Item.Transform;
		BMin[i] = Item.BoundsMin;
		BMax[i] = Item.BoundsMax;
	}

	InitMetadata(PointData, NumPoints);

	UPCGMetadata* Meta = PointData->MutableMetadata();
	TPCGValueRange<int64> MetaEntryRange = PointData->GetMetadataEntryValueRange();
	TArray<int64> MetaEntries;
	MetaEntries.SetNumUninitialized(NumPoints);
	for (int32 i = 0; i < NumPoints; i++)
	{
		MetaEntries[i] = MetaEntryRange[i];
	}

	if (FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true))
	{
		for (int32 i = 0; i < NumPoints; i++)
		{
			if (const AActor* SourceActor = Writer.Items[i].SourceActor)
			{
				ActorNameAttr->SetValue(MetaEntries[i], SourceActor->GetActorNameOrLabel());
			}
		}
	}

	// Value tags, parsed once per unique source actor (ISM actors contribute many points).
	if (ValueTagMode != EPCGExValueTagMode::NoParsing)
	{
		FValueTagRegistry Registry;
		TMap<const AActor*, int32> ParsedIndex;
		TArray<FParsedActorTags> ParsedList;

		for (const FPCGExExportItem& Item : Writer.Items)
		{
			if (Item.SourceActor && !ParsedIndex.Contains(Item.SourceActor))
			{
				ParsedIndex.Add(Item.SourceActor, ParsedList.Num());
				ParsedList.Add(ParseActorTags(Item.SourceActor, &Registry));
			}
		}

		if (!ParsedList.IsEmpty() && !Registry.TypeMap.IsEmpty())
		{
			const TMap<FName, FPCGMetadataAttributeBase*> AttrMap = CreateValueTagAttributes(Meta, Registry);
			if (!AttrMap.IsEmpty())
			{
				for (int32 i = 0; i < NumPoints; i++)
				{
					if (const int32* Index = ParsedIndex.Find(Writer.Items[i].SourceActor))
					{
						SetValueTagAttributes(AttrMap, MetaEntries[i], ParsedList[*Index]);
					}
				}
			}
		}
	}

	Handler->WriteItemAttributes(Meta, MetaEntries, Writer, this);
	if (!bGenerateCollections)
	{
		Handler->WriteRawAttributes(Meta, MetaEntries, Writer, this);
	}

	FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
	TaggedData.Data = PointData;
	TaggedData.Pin = Writer.Desc.PinName;

	return PointData;
}

void UPCGExDefaultLevelDataExporter::BuildEmbeddedSlot(const PCGExLevelExport::FHandlerRegistration& Registration, FPCGExExportSlotWriter& Writer, UPCGDataAsset* OutAsset, UPCGBasePointData* PointData, FPCGExLevelExportContext& OutContext) const
{
	const FPCGExExportSlotDesc& Desc = Registration.Desc;
	const IPCGExExportSlotPolicy& Policy = *Registration.Policy;

	// A caller without slot storage still gets hashes: the collection is an inner of the exported asset
	// either way, only the hand-back is skipped.
	TArray<FPCGExExportCollectionSlot> LocalSlots;
	FPCGExExportCollectionSlot& Slot = PCGExExportSlots::FindOrAdd(OutContext.EmbeddedSlots ? *OutContext.EmbeddedSlots : LocalSlots, Desc.SlotId);

	// EntryIds re-claimed by identity: exact = the policy's content hash, loose = its primary path (survives
	// a content change). Unclaimed entries get fresh ids from the RebuildStagingData -> SyncEntryIds pass.
	PCGExAssetCollection::FEntryIdBank PreservedIds;
	UPCGExAssetCollection* Collection = Slot.Collection;

	if (Collection && Collection->GetClass() != Desc.CollectionClass)
	{
		// A slot whose registered class changed cannot keep the object; ids are lost with it.
		Collection->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		Collection = nullptr;
	}

	if (Collection)
	{
		// Reuse the previous object -- a fresh one would mint a new CollectionGUID on every export,
		// unbinding external references (variants).
		Collection->ForEachEntry([&PreservedIds, &Policy](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry)
			{
				PreservedIds.Deposit(Policy.Hash(*Entry), GetTypeHash(Policy.PrimaryPath(*Entry)), Entry->EntryId);
			}
		});

		Collection->Rename(nullptr, OutAsset, REN_DontCreateRedirectors | REN_NonTransactional);

		// Every index is rewritten below; only the previous entries' instanced subobjects need retiring.
		Collection->ForEachEntry([&Desc](FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry)
			{
				PCGExCollectionHelpers::RetireInstancedSubobjects(Desc.EntryStruct, Entry);
			}
		});
	}
	else
	{
		Collection = NewObject<UPCGExAssetCollection>(OutAsset, Desc.CollectionClass);
	}

	const int32 NumEntries = Writer.Entries.Num();
	Collection->InitNumEntries(NumEntries);
	for (int32 i = 0; i < NumEntries; i++)
	{
		FPCGExAssetCollectionEntry* Dst = Collection->GetMutableEntryRaw(i);
		Desc.EntryStruct->CopyScriptStruct(Dst, Writer.Entries[i].GetMemory());
		// The writer's entries are the sole owners of what they minted: moved, not duplicated.
		PCGExCollectionHelpers::ReparentInstancedSubobjects(Desc.EntryStruct, Dst, Collection);
		Dst->EntryId = PreservedIds.ClaimExact(Policy.Hash(*Dst));
	}

	// Loose pass -- strictly after every exact claim.
	for (int32 i = 0; i < NumEntries; i++)
	{
		FPCGExAssetCollectionEntry* Dst = Collection->GetMutableEntryRaw(i);
		if (Dst->EntryId == 0)
		{
			Dst->EntryId = PreservedIds.ClaimLoose(GetTypeHash(Policy.PrimaryPath(*Dst)));
		}
	}

	Registration.HandlerClass->GetDefaultObject<UPCGExLevelExportHandler>()->FinalizeEmbeddedCollection(Collection, Writer, this);
	Collection->RebuildStagingData(true);

	Slot.Collection = Collection;

	// Hashes resolve against this collection's own GUID; the caller's map rebuild re-registers it as is.
	PCGExCollections::FPickPacker Packer;
	Packer.RegisterCollection(Collection);

	UPCGMetadata* Meta = PointData->MutableMetadata();
	TPCGValueRange<int64> MetaEntries = PointData->GetMetadataEntryValueRange();
	FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(PCGExCollections::Labels::Tag_EntryIdx, 0, false, true);
	if (!EntryHashAttr)
	{
		return;
	}

	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		const FPCGExExportItem& Item = Writer.Items[i];
		if (Item.LocalEntryIndex < 0)
		{
			continue;
		}
		const int16 Sec = Desc.bSupportsSecondary ? Item.SecondaryIndex : static_cast<int16>(-1);
		const uint64 Hash = Packer.GetPickIdx(Collection, static_cast<int16>(Item.LocalEntryIndex), Sec);
		EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
	}
}

#else

bool UPCGExDefaultLevelDataExporter::ExportLevelData(const FPCGExLevelExportSource& Source, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext)
{
	// Level harvesting is an authoring operation; cooked data carries the baked result.
	return false;
}

#endif
