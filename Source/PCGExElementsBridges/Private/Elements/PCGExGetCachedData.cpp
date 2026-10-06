// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetCachedData.h"

#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGParamData.h"
#include "Data/PCGBasePointData.h" // PCGPointDataConstants
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h" // FPropertyChangedEvent

#include "PCGExCoreSettingsCache.h"
#include "Components/PCGExDataCacheComponent.h"
#include "Core/PCGExMainThreadPoll.h"
#include "Helpers/PCGExActorHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGetCachedData"
#define PCGEX_NAMESPACE GetCachedData

namespace PCGExGetCachedData
{
	// Game thread. True when every requested entry is readable on every host, so there is nothing to wait for.
	bool IsEverythingAvailable(FPCGExGetCachedDataContext* Context, const UPCGExGetCachedDataSettings* Settings)
	{
		// No key to look up: the read reports the miss, waiting would not change it.
		if (!Settings->bReadAllEntries && Context->Keys.IsEmpty()) { return true; }

		TArray<AActor*> Actors;
		TArray<PCGExDataCache::FTargetReference> Unresolved;
		Settings->ResolveTargets(Context, Context->TargetReferences, /*bCreateWorldActor=*/false, Actors, &Unresolved, /*bQuiet=*/true);

		if (!Unresolved.IsEmpty() && Context->bReferencesMayLoad) { return false; }

		// An empty pin requests nothing; any other target names one host, which may not exist yet (PCG World Actor).
		if (Actors.IsEmpty()) { return Settings->UsesTargetPin(); }

		for (const AActor* Actor : Actors)
		{
			const UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::FindCurrent(Actor);
			if (!Cache) { return false; }

			if (Settings->bReadAllEntries)
			{
				if (Cache->GetCachedIDs().IsEmpty()) { return false; }
				continue;
			}

			for (const FName Key : Context->Keys)
			{
				if (!Cache->HasCachedData(Key)) { return false; }
			}
		}

		return true;
	}

	// Game thread. Fills Reads and StatusRows from scratch: target first, then key, then stored order.
	void ReadTargets(FPCGExGetCachedDataContext* Context, const UPCGExGetCachedDataSettings* Settings)
	{
		Context->Reads.TaggedData.Reset();
		Context->StatusRows.Reset();

		const TArray<FName>& Keys = Context->Keys;

		// One not-found row per key, or a single keyless one: Status always has something to branch on.
		auto AddMissRows = [Context, &Keys](const FSoftObjectPath& InActor)
		{
			if (Keys.IsEmpty())
			{
				Context->StatusRows.Emplace_GetRef().Actor = InActor;
				return;
			}

			for (const FName Key : Keys)
			{
				FPCGExGetCachedDataContext::FStatusRow& Row = Context->StatusRows.Emplace_GetRef();
				Row.Actor = InActor;
				Row.CacheID = Key;
			}
		};

		// A read never spawns the PCG World Actor. Resolved here, in the step that reads: a pooled host can move in between.
		TArray<AActor*> Actors;
		TArray<PCGExDataCache::FTargetReference> Unresolved;
		Settings->ResolveTargets(Context, Context->TargetReferences, /*bCreateWorldActor=*/false, Actors, &Unresolved);

		if (Actors.IsEmpty() && Unresolved.IsEmpty())
		{
			AddMissRows(FSoftObjectPath());
			return;
		}

#if WITH_EDITOR
		// Engine mirror (FPCGDataFromActorElement::ProcessActor): data owned by an actor outside the source's persistent
		// level is duplicated to the transient package, so the graph cache never pins that level's objects.
		const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
		const AActor* SourceOwner = PCGExHelpers::GetSourceActor(Source ? Source->GetExecutionState().GetOriginalSource() : nullptr);
		const ULevel* PersistentLevel = (SourceOwner && SourceOwner->GetWorld()) ? SourceOwner->GetWorld()->PersistentLevel : nullptr;
#endif

		for (AActor* Actor : Actors)
		{
			// A pooled partition actor keeps its components when it is recycled: never read what another cell left behind.
			const UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::FindCurrent(Actor);
			if (!Cache)
			{
				// Normal on the first generation.
				PCGE_LOG_C(Verbose, LogOnly, Context, FText::Format(LOCTEXT("NoCacheComponent", "Actor '{0}' has no PCGEx Data Cache component."), FText::FromString(Actor->GetName())));
				AddMissRows(FSoftObjectPath(Actor));
				continue;
			}

			bool bMustDuplicate = false;
#if WITH_EDITOR
			bMustDuplicate = !PersistentLevel || Actor->GetLevel() != PersistentLevel;
#endif

			// Moves one entry's data into Reads; returns how many data objects it held.
			auto Ingest = [Context, Settings, bMustDuplicate](const FName InKey, TArray<FPCGTaggedData>& InData)
			{
				const FString CacheTag = Settings->bTagWithCacheID ? PCGExDataCache::MakeCacheIDTag(InKey) : FString();

				int32 DataCount = 0;
				for (FPCGTaggedData& Stored : InData)
				{
					if (!Stored.Data) { continue; }

					FPCGTaggedData& Read = Context->Reads.TaggedData.Emplace_GetRef(MoveTemp(Stored));
					if (bMustDuplicate) { Read.Data = Cast<UPCGData>(StaticDuplicateObject(Read.Data.Get(), GetTransientPackage())); }
					if (Settings->bTagWithCacheID) { Read.Tags.Add(CacheTag); }

					DataCount++;
				}
				return DataCount;
			};

			if (Settings->bReadAllEntries)
			{
				FPCGExGetCachedDataContext::FStatusRow& Row = Context->StatusRows.Emplace_GetRef();
				Row.Actor = FSoftObjectPath(Actor);

				TArray<TPair<FName, TArray<FPCGTaggedData>>> Entries;
				Cache->ReadAll(Entries);

				if (Entries.IsEmpty())
				{
					PCGE_LOG_C(Verbose, LogOnly, Context, FText::Format(LOCTEXT("CacheMissAny", "Actor '{0}' has no cached entry."), FText::FromString(Actor->GetName())));
					continue;
				}

				Row.bFound = true;
				for (TPair<FName, TArray<FPCGTaggedData>>& Entry : Entries) { Row.DataCount += Ingest(Entry.Key, Entry.Value); }
				continue;
			}

			if (Keys.IsEmpty())
			{
				AddMissRows(FSoftObjectPath(Actor));
				continue;
			}

			TArray<FString> Missing;
			for (const FName Key : Keys)
			{
				FPCGExGetCachedDataContext::FStatusRow& Row = Context->StatusRows.Emplace_GetRef();
				Row.Actor = FSoftObjectPath(Actor);
				Row.CacheID = Key;

				TArray<FPCGTaggedData> Data;
				if (!Cache->Read(Key, Data))
				{
					Missing.Add(FString::Printf(TEXT("'%s'"), *Key.ToString()));
					continue;
				}

				Row.bFound = true;
				Row.DataCount = Ingest(Key, Data);
			}

			if (Missing.IsEmpty()) { continue; }

			const FText Miss = FText::Format(LOCTEXT("CacheMiss", "Actor '{0}' has no cached entry for {1}."), FText::FromString(Actor->GetName()), FText::FromString(FString::Join(Missing, TEXT(", "))));
			if (Missing.Num() == Keys.Num())
			{
				// Every key missing is what a first generation looks like: Status is the branch point, not a warning.
				PCGE_LOG_C(Verbose, LogOnly, Context, Miss);
			}
			else if (!Settings->bQuietMissingPartitionWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, Miss);
			}
		}

		// A reference that did not resolve is a miss too: Status must not read as complete without it.
		for (const PCGExDataCache::FTargetReference& Reference : Unresolved) { AddMissRows(Reference.Actor); }
	}
}

#pragma region UPCGExGetCachedDataSettings

UPCGExGetCachedDataSettings::UPCGExGetCachedDataSettings()
{
	Target = EPCGExDataCacheTarget::Input;
}

#if WITH_EDITOR
FLinearColor UPCGExGetCachedDataSettings::GetNodeTitleColor() const
{
	return PCGEX_NODE_COLOR_OPTIN_NAME(Action);
}

EPCGChangeType UPCGExGetCachedDataSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExGetCachedDataSettings, CustomOutputPins))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

FString UPCGExGetCachedDataSettings::GetAdditionalTitleInformation() const
{
	if (bReadAllEntries) { return TEXT("All"); }
	return PCGExDataCache::MakeTitleCacheID(CacheID, IsPartitionPrefixed());
}

bool UPCGExGetCachedDataSettings::IsPartitionEditable() const
{
	if (bReadAllEntries) { return false; }
	return bPrefixWithPartitionId || IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGExGetCachedDataSettings, bPrefixWithPartitionId));
}

TArray<FPCGPinProperties> UPCGExGetCachedDataSettings::GetSanitizedCustomOutputPins() const
{
	const FName Reserved[] = {PCGPinConstants::DefaultOutputLabel, PCGExDataCache::StatusPinLabel};
	return PCGExDataCache::SanitizePins(CustomOutputPins, Reserved);
}

TArray<FPCGPinProperties> UPCGExGetCachedDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Required: with nothing upstream there is no host to read, so the node is culled rather than run for a miss.
	if (UsesTargetPin())
	{
		PCGEX_PIN_ANY(PCGExDataCache::TargetActorPinLabel, "Actor references naming the actor(s) whose cache to read.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetCachedDataSettings::OutputPinProperties() const
{
	// Order is load-bearing: AdvanceWork culls by pin index (custom pins, then Out, then Status).
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Append(GetSanitizedCustomOutputPins());
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "Cached data whose stored pin label matches no custom output pin.", Normal)
	if (bOutputStatus)
	{
		PCGEX_PIN_PARAM(PCGExDataCache::StatusPinLabel, "One row per target actor and cache ID: Found, DataCount, ActorReference, CacheID.", Normal)
	}
	return PinProperties;
}

FPCGElementPtr UPCGExGetCachedDataSettings::CreateElement() const
{
	return MakeShared<FPCGExGetCachedDataElement>();
}

#pragma endregion

#pragma region FPCGExGetCachedDataContext

void FPCGExGetCachedDataContext::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
{
	FPCGExContext::AddExtraStructReferencedObjects(Collector);
	Reads.AddReferences(Collector);
}

#pragma endregion

#pragma region FPCGExGetCachedDataElement

bool FPCGExGetCachedDataElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(GetCachedData)
	check(IsInGameThread());

	if (!Settings->bReadAllEntries && Settings->CacheID.IsNone())
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidCacheID", "Cache ID is None."));
		return false;
	}

	if (Settings->IsPartitionPrefixed())
	{
		if (Settings->Partitions.IsEmpty())
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoPartitions", "Prefix With Partition Id is enabled but Partitions is empty."));
			return false;
		}

		if (!PCGExDataCache::ResolvePartitionedCacheIDs(Context->ExecutionSource.Get(), Settings->CacheID, Settings->Partitions, Context->Keys))
		{
			// Never the bare Cache ID instead: that would read an entry the user did not name.
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("UnresolvedPartition", "The partition prefix could not be resolved (no execution source, or no valid bounds); nothing was read."));
		}
	}
	else if (!Settings->bReadAllEntries)
	{
		Context->Keys.Add(Settings->CacheID);
	}

	Settings->GatherTargetReferences(Context, Context->TargetReferences);

	// Nothing loads on its own outside a game world: a reference that does not resolve there never will.
	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	const UWorld* World = Source ? Source->GetExecutionState().GetWorld() : nullptr;
	Context->bReferencesMayLoad = World && World->IsGameWorld();

	// Deferred rather than read twice: a host outside the persistent level is duplicated on every read.
	if (Settings->bWaitForCache && !PCGExGetCachedData::IsEverythingAvailable(Context, Settings))
	{
		Context->bDeferredRead = true;
		return true;
	}

	PCGExGetCachedData::ReadTargets(Context, Settings);
	return true;
}

bool FPCGExGetCachedDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(GetCachedData)

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);

		if (Context->bDeferredRead)
		{
			const TWeakPtr<FPCGContextHandle> CtxHandle = Context->GetWeakSelfHandle();
			Context->Wait = MakeShared<PCGExMT::FMainThreadPoll>(Settings->WaitTimeout);

			Context->Wait->OnPollCallback = [CtxHandle]()
			{
				const FPCGContext::FSharedContext<FPCGExGetCachedDataContext> SharedContext(CtxHandle);
				FPCGExGetCachedDataContext* Ctx = SharedContext.Get();

				// A released context has nothing left to wait for.
				return !Ctx || PCGExGetCachedData::IsEverythingAvailable(Ctx, Ctx->GetInputSettings<UPCGExGetCachedDataSettings>());
			};

			Context->Wait->OnCompleteCallback = [CtxHandle]()
			{
				PCGEX_SHARED_TCONTEXT_VOID(GetCachedData, CtxHandle)
				FPCGExGetCachedDataContext* Ctx = SharedContext.Get();
				const UPCGExGetCachedDataSettings* CtxSettings = Ctx->GetInputSettings<UPCGExGetCachedDataSettings>();

				if (Ctx->Wait->HasTimedOut() && !CtxSettings->bQuietTimeoutWarning)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Ctx, LOCTEXT("WaitTimeout", "Timed out waiting for the cache; reading what is available."));
				}

				PCGExGetCachedData::ReadTargets(Ctx, CtxSettings);
			};

			// Refused when the work is already cancelled: nothing will ever be read.
			if (!Context->GetTaskManager()->TryRegisterHandle(Context->Wait)) { return Context->CancelExecution(FString()); }

			return false;
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		// Same order as OutputPinProperties: custom pins, then Out, then Status. One label -> index map drives both routing and culling.
		TMap<FName, int32> PinIndex;
		for (const FPCGPinProperties& Pin : Settings->GetSanitizedCustomOutputPins()) { PinIndex.Add(Pin.Label, PinIndex.Num()); }
		const int32 OutIndex = PinIndex.Num();

		uint64 ActiveMask = 0;
		Context->IncreaseStagedOutputReserve(Context->Reads.TaggedData.Num());

		for (const FPCGTaggedData& Read : Context->Reads.TaggedData)
		{
			const int32* CustomIndex = PinIndex.Find(Read.Pin);
			const int32 Index = CustomIndex ? *CustomIndex : OutIndex;
			ActiveMask |= 1ull << Index;
			Context->StageOutput(const_cast<UPCGData*>(Read.Data.Get()), CustomIndex ? Read.Pin : PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::None, Read.Tags);
		}

		// Cull data pins that got nothing (bit j == output pin index j). Status (OutIndex + 1) is never culled.
		const uint64 DataPinMask = (1ull << (OutIndex + 1)) - 1;
		Context->OutputData.InactiveOutputPinBitmask = ~ActiveMask & DataPinMask;

		if (Settings->bOutputStatus)
		{
			UPCGParamData* Status = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
			UPCGMetadata* Metadata = Status->MutableMetadata();

			FPCGMetadataAttribute<bool>* FoundAttr = Metadata->CreateAttribute<bool>(PCGExDataCache::FoundAttributeName, false, false, true);
			FPCGMetadataAttribute<int32>* CountAttr = Metadata->CreateAttribute<int32>(PCGExDataCache::DataCountAttributeName, 0, false, true);
			FPCGMetadataAttribute<FSoftObjectPath>* ActorAttr = Metadata->CreateAttribute<FSoftObjectPath>(PCGPointDataConstants::ActorReferenceAttribute, FSoftObjectPath(), false, true);
			FPCGMetadataAttribute<FName>* IdAttr = Metadata->CreateAttribute<FName>(PCGExDataCache::CacheIDAttributeName, NAME_None, false, true);

			for (const FPCGExGetCachedDataContext::FStatusRow& Row : Context->StatusRows)
			{
				const PCGMetadataEntryKey Key = Metadata->AddEntry();
				FoundAttr->SetValue(Key, Row.bFound);
				CountAttr->SetValue(Key, Row.DataCount);
				ActorAttr->SetValue(Key, Row.Actor);
				IdAttr->SetValue(Key, Row.CacheID);
			}

			Context->StageOutput(Status, PCGExDataCache::StatusPinLabel, PCGExData::EStaging::Mutable);
		}

		Context->Done();
	}

	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
