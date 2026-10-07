// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDataCacheHelpers.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGNode.h"
#include "PCGWorldActor.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGHelpers.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h" // FPropertyChangedEvent
#include "UObject/UObjectGlobals.h" // FReferenceFinder

#include "PCGExVersion.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExPartitionDetails.h"
#include "Helpers/PCGExActorHelpers.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExDataCacheHelpers"

namespace PCGExDataCacheHelpers
{
	FString ComposePartitionedCacheID(const FString& InPartitionId, const FName InCacheID)
	{
		return InPartitionId + TEXT("_") + InCacheID.ToString();
	}

	// The loaded actor a reference names (a component reference resolves to its owner), or null when a row that names a
	// partition points at a partition actor that no longer stands for it.
	AActor* ResolveReference(const PCGExDataCache::FTargetReference& InReference)
	{
		UObject* Object = InReference.Actor.ResolveObject();
		AActor* Actor = Cast<AActor>(Object);
		if (!Actor)
		{
			if (const UActorComponent* Component = Cast<UActorComponent>(Object)) { Actor = Component->GetOwner(); }
		}

		if (!IsValid(Actor)) { return nullptr; }

		if (!InReference.PartitionId.IsEmpty())
		{
			const APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Actor);
			if (PartitionActor && !PCGExPartitionActors::StandsFor(PartitionActor, InReference.PartitionId)) { return nullptr; }
		}

		return Actor;
	}
}

namespace PCGExDataCache
{
	FName MakePartitionedCacheID(const FString& InPartitionId, const FName InCacheID)
	{
		return FName(PCGExDataCacheHelpers::ComposePartitionedCacheID(InPartitionId, InCacheID));
	}

	void ResolvePartitionedCacheIDs(const FVector& InAnchor, const UPCGComponent* InComponent, const FName InCacheID, const TConstArrayView<FPCGExPartitionQuery> InQueries, TArray<FName>& OutCacheIDs)
	{
		OutCacheIDs.Reserve(OutCacheIDs.Num() + InQueries.Num());
		for (const FPCGExPartitionQuery& Query : InQueries)
		{
			OutCacheIDs.AddUnique(MakePartitionedCacheID(PCGExPartitionGrid::FormatId(PCGExPartitionGrid::MakeCell(Query, InAnchor, InComponent)), InCacheID));
		}
	}

	bool ResolvePartitionedCacheIDs(const IPCGGraphExecutionSource* InSource, const FName InCacheID, const TConstArrayView<FPCGExPartitionQuery> InQueries, TArray<FName>& OutCacheIDs)
	{
		FVector Anchor = FVector::ZeroVector;
		if (!PCGExPartitionGrid::TryGetAnchor(InSource, Anchor)) { return false; }

		ResolvePartitionedCacheIDs(Anchor, Cast<UPCGComponent>(InSource), InCacheID, InQueries, OutCacheIDs);
		return true;
	}

	FString MakeTitleCacheID(const FName InCacheID, const bool bPartitioned)
	{
		if (InCacheID.IsNone()) { return FString(); }
		return bPartitioned ? PCGExDataCacheHelpers::ComposePartitionedCacheID(TEXT("{Partition}"), InCacheID) : InCacheID.ToString();
	}

	bool IsSelfContained(const UPCGData* InData)
	{
		if (!InData) { return false; }

		TArray<UObject*> Referenced;
		FReferenceFinder Finder(Referenced, /*InOuter=*/nullptr, /*bInRequireDirectOuter=*/false);
		Finder.FindReferences(const_cast<UPCGData*>(InData));

		for (const UObject* Object : Referenced)
		{
			if (Object && Object->IsA<UPCGData>() && !Object->IsIn(InData)) { return false; }
		}
		return true;
	}

	TArray<FPCGPinProperties> SanitizePins(const TArray<FPCGPinProperties>& InPins, const TArrayView<const FName> InReservedLabels)
	{
		TArray<FPCGPinProperties> Pins;
		Pins.Reserve(InPins.Num());

		TSet<FName> Seen;
		Seen.Append(InReservedLabels);

		for (const FPCGPinProperties& Pin : InPins)
		{
			if (Pin.Label.IsNone() || Seen.Contains(Pin.Label)) { continue; }
			Seen.Add(Pin.Label);
			Pins.Add(Pin);
		}

		return Pins;
	}
}

#pragma region UPCGExDataCacheSettingsBase

#if WITH_EDITOR
void UPCGExDataCacheSettingsBase::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 4)
	{
		// A wired Target Actor pin means Input. Set before the pin update, which only keeps the pin under Input.
		for (const TObjectPtr<UPCGPin>& Pin : InputPins)
		{
			if (Pin && Pin->Properties.Label == PCGExDataCache::TargetActorPinLabel && Pin->IsConnected())
			{
				Target = EPCGExDataCacheTarget::Input;
				break;
			}
		}

		// Target lost its override pin. UPCGNode::UpdatePins renames a lone stale pin onto a lone new one, edges included.
		RetireInputPin(InOutNode, GET_MEMBER_NAME_CHECKED(UPCGExDataCacheSettingsBase, Target));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

EPCGChangeType UPCGExDataCacheSettingsBase::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExDataCacheSettingsBase, Target))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

bool UPCGExDataCacheSettingsBase::DoesPinSupportPassThrough(UPCGPin* InPin) const
{
	return Super::DoesPinSupportPassThrough(InPin) && InPin->Properties.Label != PCGExDataCache::TargetActorPinLabel;
}

void UPCGExDataCacheSettingsBase::GatherTargetReferences(FPCGExContext* InContext, TArray<PCGExDataCache::FTargetReference>& OutReferences) const
{
	check(InContext);

	OutReferences.Reset();
	if (!UsesTargetPin()) { return; }

	TArray<FSoftObjectPath> Paths;
	TArray<FString> PartitionIds;

	// Rows without a readable attribute is a user error worth surfacing; an empty input is not.
	if (!PCGExData::Helpers::BulkReadUniqueSoftPaths(InContext->InputData, PCGExDataCache::TargetActorPinLabel, ActorReferenceAttribute, Paths, PartitionIdAttribute, &PartitionIds))
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("MissingActorReferenceAttribute", "Target actor data has no readable '{0}' attribute."), FText::FromName(ActorReferenceAttribute)));
	}

	OutReferences.Reserve(Paths.Num());
	for (int32 i = 0; i < Paths.Num(); i++)
	{
		PCGExDataCache::FTargetReference& Reference = OutReferences.Emplace_GetRef();
		Reference.Actor = Paths[i];
		Reference.PartitionId = MoveTemp(PartitionIds[i]);
	}
}

void UPCGExDataCacheSettingsBase::ResolveTargets(FPCGExContext* InContext, const TConstArrayView<PCGExDataCache::FTargetReference> InReferences, const bool bCreateWorldActor, TArray<AActor*>& OutActors, TArray<PCGExDataCache::FTargetReference>* OutUnresolved, const bool bQuiet) const
{
	check(IsInGameThread());
	check(InContext);

	IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
	AActor* Actor = nullptr;
	TArray<PCGExDataCache::FTargetReference> Unresolved;

	switch (Target)
	{
	case EPCGExDataCacheTarget::ExecutingActor:
		Actor = Source ? InContext->GetTargetActor(nullptr) : nullptr;
		break;
	case EPCGExDataCacheTarget::OriginalActor:
		if (Source)
		{
			Actor = PCGExHelpers::GetSourceActor(Source->GetExecutionState().GetOriginalSource());
			// A source with no original (non-component execution) is its own original.
			if (!Actor) { Actor = InContext->GetTargetActor(nullptr); }
		}
		break;
	case EPCGExDataCacheTarget::WorldActor:
		if (Source)
		{
			UWorld* World = Source->GetExecutionState().GetWorld();
			Actor = bCreateWorldActor ? PCGHelpers::GetPCGWorldActor(World) : PCGHelpers::FindPCGWorldActor(World);
		}
		break;
	case EPCGExDataCacheTarget::Input:
		// Never falls back to another target: an empty pin names no host.
		for (const PCGExDataCache::FTargetReference& Reference : InReferences)
		{
			if (AActor* Referenced = PCGExDataCacheHelpers::ResolveReference(Reference)) { OutActors.AddUnique(Referenced); }
			else { Unresolved.Add(Reference); }
		}
		break;
	default:
		ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheTarget (%d)"), static_cast<int32>(Target));
		break;
	}

	if (IsValid(Actor)) { OutActors.Add(Actor); }

	if (!bQuiet && !bQuietMissingTargetWarning)
	{
		if (OutActors.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, LOCTEXT("NoTargetActor", "No target actor could be resolved."));
		}
		else if (!Unresolved.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("UnresolvedTargets", "{0} target reference(s) could not be resolved: not loaded, or no longer standing for their partition."), FText::AsNumber(Unresolved.Num())));
		}
	}

	if (OutUnresolved) { *OutUnresolved = MoveTemp(Unresolved); }
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
