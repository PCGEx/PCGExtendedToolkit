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
#include "Helpers/PCGExLevelExportSplineHandler.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "PCGExAssemblyRoot.h"
#include "PCGExCollectionsSettingsCache.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Core/PCGExExportSlots.h"
#include "PCGExLog.h"
#include "Data/PCGExDataHelpers.h"
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

	UClass* SplineClassificatorClass = Settings.DefaultSplineClassificatorClass
		? Settings.DefaultSplineClassificatorClass.Get()
		: UPCGExDefaultActorSplineClassificator::StaticClass();

	ContentFilter = Cast<UPCGExActorContentFilter>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("ContentFilter"),
		                                         UPCGExActorContentFilter::StaticClass(), FilterClass, false, false));

	MeshClassificator = Cast<UPCGExActorMeshClassificator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("MeshClassificator"),
		                                         UPCGExActorMeshClassificator::StaticClass(), ClassificatorClass, false, false));

	SplineClassificator = Cast<UPCGExActorSplineClassificator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("SplineClassificator"),
		                                         UPCGExActorSplineClassificator::StaticClass(), SplineClassificatorClass, false, false));

	BoundsEvaluator = Cast<UPCGExBoundsEvaluator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("BoundsEvaluator"),
		                                         UPCGExBoundsEvaluator::StaticClass(), EvalClass, false, false));
}

namespace PCGExDefaultLevelDataExporterInternal
{
	template <typename T>
	void ApplyProjectClass(UObject* Outer, TObjectPtr<T>& Slot, UClass* ProjectClass)
	{
		if (!ProjectClass || !ProjectClass->IsChildOf(T::StaticClass()) || ProjectClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return;
		}

		if (Slot && Slot->GetClass() == ProjectClass)
		{
			return;
		}

		Slot = NewObject<T>(Outer, ProjectClass);
	}
}

void UPCGExDefaultLevelDataExporter::ApplyProjectDefaults()
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;
	PCGExDefaultLevelDataExporterInternal::ApplyProjectClass(this, ContentFilter, Settings.DefaultContentFilterClass.Get());
	PCGExDefaultLevelDataExporterInternal::ApplyProjectClass(this, MeshClassificator, Settings.DefaultMeshClassificatorClass.Get());
	PCGExDefaultLevelDataExporterInternal::ApplyProjectClass(this, SplineClassificator, Settings.DefaultSplineClassificatorClass.Get());
	PCGExDefaultLevelDataExporterInternal::ApplyProjectClass(this, BoundsEvaluator, Settings.DefaultBoundsEvaluatorClass.Get());
}

namespace PCGExDefaultLevelDataExporterInternal
{
	// Any UStaticMeshComponent (or subclass -- ISMC, HISM, splines, future kinds) with a valid mesh AND geometry to
	// contribute. ISMCs with zero instances contribute nothing and don't count.
	bool HasMeshContent(const AActor* Actor)
	{
		TInlineComponentArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents<UStaticMeshComponent>(SMCs);
		for (const UStaticMeshComponent* SMC : SMCs)
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
			return true;
		}
		return false;
	}
}

void UPCGExDefaultLevelDataExporter::ClassifyActor(AActor* Actor, PCGExLevelExport::FSlotSet& OutSlots) const
{
	// Level instances precede the content checks: an ALevelInstance can incidentally have a UStaticMeshComponent
	// (gizmo/visualizer) but the meaningful payload is the referenced UWorld asset. An empty world ref has nothing to
	// embed and falls through, so transform/tags survive the round-trip.
	if (const ALevelInstance* LI = Cast<ALevelInstance>(Actor); LI && !LI->GetWorldAsset().IsNull())
	{
		OutSlots.Add(PCGExLevelExport::Slots::Levels);
		return;
	}

	if (MeshClassificator && MeshClassificator->ShouldClassifyAsMesh(Actor) && PCGExDefaultLevelDataExporterInternal::HasMeshContent(Actor))
	{
		OutSlots.Add(PCGExLevelExport::Slots::Meshes);
	}

	if (SplineClassificator && SplineClassificator->ShouldClassifyAsSpline(Actor) && PCGExLevelExport::HasSplineContent(Actor, SplineClassificator->bConvertSplineMeshes))
	{
		OutSlots.Add(PCGExLevelExport::Slots::Splines);
	}

	if (OutSlots.IsEmpty())
	{
		OutSlots.Add(PCGExLevelExport::Slots::Actors);
	}
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
	/** The classification an actor's system tags force. False when it carries none. */
	bool ReadClassificationTags(const AActor* Actor, PCGExLevelExport::FSlotSet& OutSlots)
	{
		for (const PCGExLevelExportShared::FClassificationTag& Classification : PCGExLevelExportShared::ClassificationTags)
		{
			if (Actor->ActorHasTag(Classification.Tag))
			{
				OutSlots.Add(Classification.SlotId);
			}
		}
		return !OutSlots.IsEmpty();
	}

	/**
	 * Drops (once per export, loudly) the slots no handler registers, then sets bRecreated. False when a slot that
	 * spawns the actor again would share it: the actor is skipped.
	 */
	bool ResolveClaims(FPCGExExportCandidate& Candidate, const TMap<FName, const FPCGExExportSlotDesc*>& DescById, TSet<FName>& ReportedUnknownSlots)
	{
		PCGExLevelExport::FSlotSet Resolved;
		bool bRecreated = false;

		for (const FName SlotId : Candidate.ClaimedSlots)
		{
			const FPCGExExportSlotDesc* const* Desc = DescById.Find(SlotId);
			if (!Desc)
			{
				bool bAlreadyReported = false;
				ReportedUnknownSlots.Add(SlotId, &bAlreadyReported);
				if (!bAlreadyReported)
				{
					UE_LOG(LogPCGEx, Error, TEXT("Level export: '%s' is classified into slot '%s', which no handler registers; that slot is ignored."),
					       *Candidate.Actor->GetActorNameOrLabel(), *SlotId.ToString());
				}
				continue;
			}

			if (!Resolved.Contains(SlotId))
			{
				Resolved.Add(SlotId);
				bRecreated |= (*Desc)->bRecreatesSource;
			}
		}

		if (bRecreated && Resolved.Num() > 1)
		{
			TArray<FString> SlotNames;
			for (const FName SlotId : Resolved)
			{
				SlotNames.Add(SlotId.ToString());
			}
			UE_LOG(LogPCGEx, Error, TEXT("Level export: '%s' is classified into %s, but a slot that spawns the actor again (Actors, Levels) cannot share it. The actor is skipped."),
			       *Candidate.Actor->GetActorNameOrLabel(), *FString::Join(SlotNames, TEXT(" + ")));
			return false;
		}

		Candidate.ClaimedSlots = MoveTemp(Resolved);
		Candidate.bRecreated = bRecreated;
		return true;
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
		GetObjectsWithOuter(OutAsset, OldInners, EGetObjectsFlags::None);
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
	TMap<FName, const FPCGExExportSlotDesc*> DescById;
	DescById.Reserve(Registrations.Num());
	for (const PCGExLevelExport::FHandlerRegistration& Registration : Registrations)
	{
		Handlers.Add(Registration.HandlerClass->GetDefaultObject<UPCGExLevelExportHandler>());
		DescById.Add(Registration.Desc.SlotId, &Registration.Desc);
	}

	TSet<FName> ReportedUnknownSlots;

	// Claim pass. A root is a candidate too (handlers may harvest its components) but never an entry, so the content
	// filter, which picks entries, skips it; its classification tags pick its slots unless it is tagged StaticIgnore.
	TArray<FPCGExExportCandidate> Candidates;
	auto AddRoot = [&](AActor* RootActor)
	{
		FPCGExExportCandidate Root;
		Root.Actor = RootActor;
		Root.bIsRoot = true;
		Root.bForcedByTag = !RootActor->ActorHasTag(PCGExCollections::Labels::StaticIgnore) && ReadClassificationTags(RootActor, Root.ClaimedSlots);
		if (ResolveClaims(Root, DescById, ReportedUnknownSlots))
		{
			Candidates.Add(MoveTemp(Root));
		}
	};

	if (Source.Root)
	{
		AddRoot(Source.Root);
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
				AddRoot(Actor);
			}
			continue;
		}

		if (!UPCGExActorContentFilter::StaticPassesFilter(ContentFilter, Actor))
		{
			continue;
		}

		FPCGExExportCandidate Candidate;
		Candidate.Actor = Actor;

		// Tags replace the classification: no handler claim, no classificator, no content check.
		Candidate.bForcedByTag = ReadClassificationTags(Actor, Candidate.ClaimedSlots);
		if (!Candidate.bForcedByTag)
		{
			for (int32 i = 0; i < Handlers.Num(); i++)
			{
				if (Handlers[i]->Claim(Actor, Source, this))
				{
					Candidate.ClaimedSlots.Add(Registrations[i].Desc.SlotId);
					break;
				}
			}
			if (Candidate.ClaimedSlots.IsEmpty())
			{
				ClassifyActor(Actor, Candidate.ClaimedSlots);
			}
		}

		if (!ResolveClaims(Candidate, DescById, ReportedUnknownSlots) || Candidate.ClaimedSlots.IsEmpty())
		{
			continue;
		}

		Candidates.Add(MoveTemp(Candidate));
	}

	if (Candidates.IsEmpty())
	{
		return false;
	}

	// Harvest pass: every handler sees every candidate. A tag skips the content check, so a tagged slot its candidate
	// adds nothing to is warned about, once per actor.
	TMap<const AActor*, TArray<FString>> EmptyTaggedSlots;
	TArray<TUniquePtr<FPCGExExportSlotWriter>> Writers;
	Writers.Reserve(Registrations.Num());
	for (int32 i = 0; i < Registrations.Num(); i++)
	{
		const PCGExLevelExport::FHandlerRegistration& Registration = Registrations[i];
		FPCGExExportSlotWriter& Writer = *Writers.Add_GetRef(MakeUnique<FPCGExExportSlotWriter>(Registration.Desc));
		const PCGExLevelExportShared::FClassificationTag* Classification = Algo::FindBy(PCGExLevelExportShared::ClassificationTags, Registration.Desc.SlotId, &PCGExLevelExportShared::FClassificationTag::SlotId);

		for (const FPCGExExportCandidate& Candidate : Candidates)
		{
			const int32 NumItems = Writer.Items.Num();
			Handlers[i]->Collect(Candidate, Source, this, Writer);
			if (Classification && Candidate.bForcedByTag && Writer.Items.Num() == NumItems && Candidate.IsClaimedBy(Registration.Desc.SlotId))
			{
				EmptyTaggedSlots.FindOrAdd(Candidate.Actor).Add(Classification->Tag.ToString());
			}
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

	for (const TPair<const AActor*, TArray<FString>>& Empty : EmptyTaggedSlots)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("Level export: '%s' is tagged %s but has nothing to export there."),
		       *Empty.Key->GetActorNameOrLabel(), *FString::Join(Empty.Value, TEXT(" and ")));
	}

	// Emit one point data per non-empty slot, in handler order; a slot without a collection emits each item's data.
	TArray<UPCGBasePointData*> SlotPointData;
	SlotPointData.Init(nullptr, Registrations.Num());
	for (int32 i = 0; i < Registrations.Num(); i++)
	{
		if (Writers[i]->IsEmpty())
		{
			continue;
		}

		if (Registrations[i].Desc.HasCollection())
		{
			SlotPointData[i] = EmitSlotPoints(*Writers[i], Handlers[i], OutAsset);
		}
		else
		{
			EmitRawSlotData(*Writers[i], Handlers[i], OutAsset);
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

	if (FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(ActorNameAttribute, FString(), false, true))
	{
		for (int32 i = 0; i < NumPoints; i++)
		{
			if (const AActor* SourceActor = Writer.Items[i].SourceActor)
			{
				ActorNameAttr->SetValue(MetaEntries[i], SourceActor->GetActorNameOrLabel());
			}
		}
	}

	const bool bHasMeshPoints = Writer.Items.ContainsByPredicate([](const FPCGExExportItem& Item) { return Item.SourceComponent && Item.SourceComponent->IsA<UStaticMeshComponent>(); });
	TArray<FName> DeclaredAttributes;
	if (bHasMeshPoints)
	{
		TArray<FName> PriorAttributes;
		TArray<EPCGMetadataTypes> AttributeTypes;
		Meta->GetAttributes(PriorAttributes, AttributeTypes);
		DeclareMeshPointAttributes(Meta);
		Meta->GetAttributes(DeclaredAttributes, AttributeTypes);
		DeclaredAttributes.RemoveAll([&PriorAttributes](const FName& Name) { return PriorAttributes.Contains(Name); });
	}

	// Value tags, parsed once per unique source (ISM actors contribute many points). The writer's tag sources
	// register before any actor and are written after it: they win on a shared name, type conflict included.
	if (ValueTagMode != EPCGExValueTagMode::NoParsing)
	{
		FValueTagRegistry Registry;

		// What the export writes itself on this pin. PCG fails to create an attribute over one of another type,
		// so a tag that took one of these names first would cost every point that attribute.
		Registry.Reserved.Add(ActorNameAttribute);
		if (bGenerateCollections)
		{
			Registry.Reserved.Add(PCGExCollections::Labels::Tag_EntryIdx);
		}
		TArray<FName> HandlerAttributes;
		Handler->GetWrittenAttributeNames(this, !bGenerateCollections, HandlerAttributes);
		Registry.Reserved.Append(HandlerAttributes);
		Registry.Reserved.Append(DeclaredAttributes);

		TArray<FParsedTags> ParsedSources;
		ParsedSources.Reserve(Writer.TagSources.Num());
		for (const FPCGExExportSlotWriter::FTagSource& TagSource : Writer.TagSources)
		{
			ParsedSources.Add(ParseTags(TagSource.Tags, &Registry, TagSource.Name));
		}

		TMap<const AActor*, int32> ParsedIndex;
		TArray<FParsedTags> ParsedList;

		for (const FPCGExExportItem& Item : Writer.Items)
		{
			if (Item.SourceActor && !ParsedIndex.Contains(Item.SourceActor))
			{
				ParsedIndex.Add(Item.SourceActor, ParsedList.Num());
				ParsedList.Add(ParseActorTags(Item.SourceActor, &Registry));
			}
		}

		if (!Registry.TypeMap.IsEmpty())
		{
			const TMap<FName, FPCGMetadataAttributeBase*> AttrMap = CreateValueTagAttributes(Meta, Registry);
			if (!AttrMap.IsEmpty())
			{
				for (int32 i = 0; i < NumPoints; i++)
				{
					const FPCGExExportItem& Item = Writer.Items[i];
					if (const int32* Index = ParsedIndex.Find(Item.SourceActor))
					{
						SetValueTagAttributes(AttrMap, MetaEntries[i], ParsedList[*Index]);
					}
					if (Item.TagSource != INDEX_NONE)
					{
						SetValueTagAttributes(AttrMap, MetaEntries[i], ParsedSources[Item.TagSource]);
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

	if (bHasMeshPoints)
	{
		for (int32 i = 0; i < NumPoints; i++)
		{
			const FPCGExExportItem& Item = Writer.Items[i];
			if (const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Item.SourceComponent))
			{
				OnMeshPointExported(Item.SourceActor, MeshComponent, Item.SourceInstanceIndex, Meta, MetaEntries[i]);
			}
		}
	}

	FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
	TaggedData.Data = PointData;
	TaggedData.Pin = Writer.Desc.PinName;

	return PointData;
}

void UPCGExDefaultLevelDataExporter::EmitRawSlotData(const FPCGExExportSlotWriter& Writer, const UPCGExLevelExportHandler* Handler, UPCGDataAsset* OutAsset) const
{
	using namespace PCGExLevelExportShared;

	// What the export writes itself on every datum of this pin, whether or not a given datum carries it.
	TArray<FName> HandlerAttributes;
	Handler->GetWrittenAttributeNames(this, false, HandlerAttributes);

	// One registry for the pin, so each skipped name warns once; types are per datum, one value per @Data name.
	FValueTagRegistry Registry;

	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		const FPCGExExportItem& Item = Writer.Items[i];
		if (!Item.Data)
		{
			continue;
		}

		// PCG's own recipe for keeping transient data in an asset (UPCGAssetExporter): copy, re-outer, flatten.
		UPCGData* Data = Item.Data->DuplicateData(/*Context=*/nullptr);
		if (!Data)
		{
			continue;
		}
		Data->Rename(nullptr, OutAsset, REN_DontCreateRedirectors | REN_NonTransactional);
		Data->Flatten();

		if (Item.SourceActor)
		{
			PCGExData::Helpers::SetDataValue<FString>(Data, ActorNameAttribute, Item.SourceActor->GetActorNameOrLabel());
		}

		Handler->FinalizeItemData(Data, i, Writer, this);

		const FPCGExExportSlotWriter::FTagSource* TagSource = Writer.TagSources.IsValidIndex(Item.TagSource) ? &Writer.TagSources[Item.TagSource] : nullptr;
		const TArray<FName> ActorTags = Item.SourceActor ? FAuthoredTags::OfActor(Item.SourceActor) : TArray<FName>();

		if (ValueTagMode != EPCGExValueTagMode::NoParsing)
		{
			Registry.TypeMap.Reset();
			Registry.Reserved.Reset();
			Registry.Reserved.Add(ActorNameAttribute);
			Registry.Reserved.Append(HandlerAttributes);
			Registry.Reserved.Append(GetDataDomainAttributeNames(Data));

			// The source registers first and is written last: it wins a shared name, as on points.
			const FParsedTags SourceParsed = TagSource ? ParseTags(TagSource->Tags, &Registry, TagSource->Name) : FParsedTags();
			const FParsedTags ActorParsed = Item.SourceActor ? ParseTags(ActorTags, &Registry, Item.SourceActor->GetActorNameOrLabel()) : FParsedTags();
			SetValueTagDataValues(Data, ActorParsed);
			SetValueTagDataValues(Data, SourceParsed);
		}

		// Authored tags as data tags, as written: a Name:Value tag reads as a value tag downstream.
		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = Data;
		TaggedData.Pin = Writer.Desc.PinName;
		Algo::Transform(ActorTags, TaggedData.Tags, [](const FName& Tag) { return Tag.ToString(); });
		if (TagSource)
		{
			Algo::Transform(TagSource->Tags, TaggedData.Tags, [](const FName& Tag) { return Tag.ToString(); });
		}
	}
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
