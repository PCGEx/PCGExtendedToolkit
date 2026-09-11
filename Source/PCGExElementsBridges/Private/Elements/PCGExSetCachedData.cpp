// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSetCachedData.h"

#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"

#include "GameFramework/Actor.h"

#include "PCGExCoreSettingsCache.h"

#define LOCTEXT_NAMESPACE "PCGExSetCachedData"
#define PCGEX_NAMESPACE SetCachedData

namespace PCGExSetCachedData
{
	// The output label a given input label passes through to.
	FName PassThroughLabel(const FName InputLabel)
	{
		return InputLabel == PCGPinConstants::DefaultInputLabel ? PCGPinConstants::DefaultOutputLabel : InputLabel;
	}
}

#pragma region UPCGExSetCachedDataSettings

#if WITH_EDITOR
void UPCGExSetCachedDataSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Clear modes have no data pins, so the execution dependency is the only thing that can order the node.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExSetCachedDataSettings, Mode))
	{
		bExecutionDependencyRequired = IsClearMode();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

FLinearColor UPCGExSetCachedDataSettings::GetNodeTitleColor() const
{
	return IsClearMode() ? PCGEX_NODE_COLOR_NAME(MiscRemove) : PCGEX_NODE_COLOR_OPTIN_NAME(Action);
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGExSetCachedDataSettings::GetPreconfiguredInfo() const
{
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExDataCacheWriteMode>({}, FTEXT("Set Cached Data : {0}"));
}
#endif

void UPCGExSetCachedDataSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExDataCacheWriteMode>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Mode = static_cast<EPCGExDataCacheWriteMode>(PreconfigureInfo.PreconfiguredIndex);
			bExecutionDependencyRequired = IsClearMode();
		}
	}
}

FString UPCGExSetCachedDataSettings::GetAdditionalTitleInformation() const
{
	switch (Mode)
	{
	case EPCGExDataCacheWriteMode::Replace:
	case EPCGExDataCacheWriteMode::Append:
		return CacheID.IsNone() ? FString() : CacheID.ToString();
	case EPCGExDataCacheWriteMode::Clear:
		return CacheID.IsNone() ? TEXT("Clear") : FString::Printf(TEXT("Clear %s"), *CacheID.ToString());
	case EPCGExDataCacheWriteMode::ClearAll:
		return TEXT("Clear All");
	default:
		// Title path: loud but survivable, so an asset with a retired enumerator can still be opened and fixed.
		ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheWriteMode (%d)"), static_cast<int32>(Mode));
		return TEXT("Invalid Mode");
	}
}

TArray<FPCGPinProperties> UPCGExSetCachedDataSettings::GetSanitizedCustomInputPins() const
{
	// Out is reserved too: every custom input pin is mirrored as a same-labelled pass-through output.
	const FName Reserved[] = {PCGPinConstants::DefaultInputLabel, PCGPinConstants::DefaultOutputLabel, PCGExDataCache::TargetActorPinLabel};
	return PCGExDataCache::SanitizePins(CustomInputPins, Reserved);
}

TArray<FPCGPinProperties> UPCGExSetCachedDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Clear modes: no data pins at all; the required execution dependency orders the node.
	if (IsClearMode()) { return PinProperties; }

	// Required only while it is the sole data pin: an unwired Set is culled instead of running for nothing. With
	// custom pins the user may wire any subset, and the component skips writes that have nothing cacheable.
	const TArray<FPCGPinProperties> CustomPins = GetSanitizedCustomInputPins();
	if (CustomPins.IsEmpty())
	{
		PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Data to cache. Stored under the In label; read it back from Get Cached Data's Out pin.", Required)
	}
	else
	{
		PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Data to cache. Stored under the In label; read it back from Get Cached Data's Out pin.", Normal)
	}
	PinProperties.Append(CustomPins);
	PCGEX_PIN_ANY(PCGExDataCache::TargetActorPinLabel, "Actor references naming the actor(s) that host the cache. When connected, overrides the Target setting.", Advanced)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSetCachedDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Clear modes: dependency-only output so downstream nodes can order themselves after the clear.
	if (IsClearMode())
	{
		PCGEX_PIN_DEPENDENCY(PCGPinConstants::DefaultOutputLabel)
		return PinProperties;
	}

	// Pass-through: every data input pin has a same-labelled output (In -> Out).
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "The In data, forwarded.", Normal)
	for (const FPCGPinProperties& Pin : GetSanitizedCustomInputPins())
	{
		FPCGPinProperties& OutPin = PinProperties.Add_GetRef(Pin);
		OutPin.PinStatus = EPCGPinStatus::Normal;
	}
	return PinProperties;
}

FPCGElementPtr UPCGExSetCachedDataSettings::CreateElement() const
{
	return MakeShared<FPCGExSetCachedDataElement>();
}

#pragma endregion

#pragma region FPCGExSetCachedDataElement

bool FPCGExSetCachedDataElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(SetCachedData)

	if (Settings->Mode != EPCGExDataCacheWriteMode::ClearAll && Settings->CacheID.IsNone())
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidCacheID", "Cache ID is None."));
		return false;
	}

	// Clear modes have no Target Actor pin, so this resolves through the Target setting alone.
	TArray<AActor*> Actors;
	Settings->ResolveTargets(Context, /*bCreateWorldActor=*/!Settings->IsClearMode(), Actors);

	Context->TargetActors.Reserve(Actors.Num());
	for (AActor* Actor : Actors) { Context->TargetActors.Add(Actor); }

	return true;
}

bool FPCGExSetCachedDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(SetCachedData)
	check(IsInGameThread());

	IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	const bool bPreview = PCGExDataCache::IsSourceInPreviewMode(Source);
	UObject* Writer = Cast<UObject>(Source);

	bool bAppend = false;
	switch (Settings->Mode)
	{
	case EPCGExDataCacheWriteMode::Replace:
		break;
	case EPCGExDataCacheWriteMode::Append:
		bAppend = true;
		break;
	case EPCGExDataCacheWriteMode::Clear:
	case EPCGExDataCacheWriteMode::ClearAll:
		// Never create a component just to find nothing in it.
		for (const TWeakObjectPtr<AActor>& WeakActor : Context->TargetActors)
		{
			UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::Find(WeakActor.Get());
			if (!Cache) { continue; }
			if (Settings->Mode == EPCGExDataCacheWriteMode::Clear) { Cache->Clear(Settings->CacheID, Writer, bPreview, Settings->bNotifyChange); }
			else { Cache->ClearAll(Writer, bPreview, Settings->bNotifyChange); }
		}
		Context->Done();
		return Context->TryComplete();
	default:
		ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheWriteMode (%d)"), static_cast<int32>(Settings->Mode));
		return Context->CancelExecution(TEXT("Unresolvable write mode."));
	}

	TSet<FName> InputLabels = {PCGPinConstants::DefaultInputLabel};
	for (const FPCGPinProperties& Pin : Settings->GetSanitizedCustomInputPins()) { InputLabels.Add(Pin.Label); }

	// Inputs gathered once: GetInputsByPin filters the whole collection and copies tag sets on every call.
	TArray<const FPCGTaggedData*> Inputs;
	Inputs.Reserve(Context->InputData.TaggedData.Num());
	for (const FPCGTaggedData& Input : Context->InputData.TaggedData)
	{
		if (Input.Data && InputLabels.Contains(Input.Pin)) { Inputs.Add(&Input); }
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : Context->TargetActors)
	{
		AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor)) { continue; }

		UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::FindOrCreate(Actor, bPreview);
		if (!Cache) { continue; }

		// Each target adopts its own private copies: a data object has exactly one outer.
		TArray<FPCGTaggedData> Duplicates;
		Duplicates.Reserve(Inputs.Num());
		for (const FPCGTaggedData* Input : Inputs)
		{
			UPCGData* Duplicate = Input->Data->DuplicateData(Context);
			if (!Duplicate)
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("DuplicateFailed", "Failed to duplicate '{0}'; it will be missing from the cache."), FText::FromString(Input->Data->GetName())));
				continue;
			}

			FPCGTaggedData& Copy = Duplicates.Emplace_GetRef();
			Copy.Data = Duplicate;
			Copy.Tags = Input->Tags;
			Copy.Pin = Input->Pin;
		}

		Cache->Write(Settings->CacheID, bAppend, MoveTemp(Duplicates), Writer, bPreview, Settings->bNotifyChange);
	}

	// Pass-through, so the node can sit inline.
	Context->IncreaseStagedOutputReserve(Inputs.Num());
	for (const FPCGTaggedData* Input : Inputs)
	{
		Context->StageOutput(const_cast<UPCGData*>(Input->Data.Get()), PCGExSetCachedData::PassThroughLabel(Input->Pin), PCGExData::EStaging::None, Input->Tags);
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
