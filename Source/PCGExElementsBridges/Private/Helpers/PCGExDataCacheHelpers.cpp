// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDataCacheHelpers.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGNode.h"
#include "PCGWorldActor.h"
#include "Helpers/PCGHelpers.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h" // FReferenceFinder

#include "PCGExVersion.h"
#include "Core/PCGExContext.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExDataCacheHelpers"

namespace PCGExDataCache
{
	AActor* GetSourceActor(const IPCGGraphExecutionSource* InSource)
	{
		if (!InSource) { return nullptr; }
#if PCGEX_ENGINE_VERSION >= 508
		return InSource->GetExecutionState().GetTypedTarget<AActor>();
#else
		const UPCGComponent* Component = Cast<UPCGComponent>(InSource);
		return Component ? Component->GetOwner() : nullptr;
#endif
	}

	bool IsSourceInPreviewMode(const IPCGGraphExecutionSource* InSource)
	{
		if (!InSource) { return false; }
#if PCGEX_ENGINE_VERSION >= 508
		return InSource->GetExecutionState().IsInPreviewMode();
#else
		const UPCGComponent* Component = Cast<UPCGComponent>(InSource);
		return Component && Component->IsInPreviewMode();
#endif
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

	bool IsTargetPinConnected(const UPCGSettings* InSettings)
	{
		const UPCGNode* Node = InSettings ? Cast<UPCGNode>(InSettings->GetOuter()) : nullptr;
		return Node && Node->IsInputPinConnected(TargetActorPinLabel);
	}
}

#pragma region UPCGExDataCacheSettingsBase

void UPCGExDataCacheSettingsBase::ResolveTargets(FPCGExContext* InContext, const bool bCreateWorldActor, TArray<AActor*>& OutActors) const
{
	check(IsInGameThread());
	check(InContext);

	// Pin data wins over the enum, like the engine's Add Component target pin.
	bool bPinHasData = false;
	TSet<AActor*> Unique;
	TArray<FSoftObjectPath> Paths;

	for (const FPCGTaggedData& TaggedData : InContext->InputData.TaggedData)
	{
		if (TaggedData.Pin != PCGExDataCache::TargetActorPinLabel || !TaggedData.Data) { continue; }
		bPinHasData = true;

		PCGExData::Helpers::BulkReadSoftPaths(TaggedData.Data, ActorReferenceAttribute, Paths);
		if (Paths.IsEmpty())
		{
			// Rows without a readable attribute is a user error worth surfacing; an empty input is not.
			const TSharedPtr<IPCGAttributeAccessorKeys> Keys = PCGExData::Helpers::GetKeys(TaggedData.Data);
			if (Keys && Keys->GetNum() > 0)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(LOCTEXT("MissingActorReferenceAttribute", "Target actor data has no readable '{0}' attribute."), FText::FromName(ActorReferenceAttribute)));
			}
			continue;
		}

		for (const FSoftObjectPath& Path : Paths)
		{
			UObject* Object = Path.ResolveObject();
			AActor* Actor = Cast<AActor>(Object);
			if (!Actor)
			{
				if (const UActorComponent* Component = Cast<UActorComponent>(Object)) { Actor = Component->GetOwner(); }
			}

			if (IsValid(Actor)) { Unique.Add(Actor); }
		}
	}

	if (bPinHasData)
	{
		OutActors.Reserve(OutActors.Num() + Unique.Num());
		for (AActor* Actor : Unique) { OutActors.Add(Actor); }
	}
	else if (IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get())
	{
		const IPCGGraphExecutionState& State = Source->GetExecutionState();
		AActor* Actor = nullptr;

		switch (Target)
		{
		case EPCGExDataCacheTarget::ExecutingActor:
			Actor = InContext->GetTargetActor(nullptr);
			break;
		case EPCGExDataCacheTarget::OriginalActor:
			Actor = PCGExDataCache::GetSourceActor(State.GetOriginalSource());
			// A source with no original (non-component execution) is its own original.
			if (!Actor) { Actor = InContext->GetTargetActor(nullptr); }
			break;
		case EPCGExDataCacheTarget::WorldActor:
			Actor = bCreateWorldActor ? PCGHelpers::GetPCGWorldActor(State.GetWorld()) : PCGHelpers::FindPCGWorldActor(State.GetWorld());
			break;
		default:
			ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheTarget (%d)"), static_cast<int32>(Target));
			break;
		}

		if (IsValid(Actor)) { OutActors.Add(Actor); }
	}

	if (OutActors.IsEmpty() && !bQuietMissingTargetWarning)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, LOCTEXT("NoTargetActor", "No target actor could be resolved."));
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
