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

	// Data on In and on the custom input pins, in input order. Gathered in one pass: GetInputsByPin filters the whole
	// collection and copies tag sets on every call.
	void GatherInputs(const FPCGExContext* InContext, const UPCGExSetCachedDataSettings* InSettings, TArray<const FPCGTaggedData*>& OutInputs)
	{
		TSet<FName> InputLabels = {PCGPinConstants::DefaultInputLabel};
		for (const FPCGPinProperties& Pin : InSettings->GetSanitizedCustomInputPins()) { InputLabels.Add(Pin.Label); }

		OutInputs.Reserve(InContext->InputData.TaggedData.Num());
		for (const FPCGTaggedData& Input : InContext->InputData.TaggedData)
		{
			if (Input.Data && InputLabels.Contains(Input.Pin)) { OutInputs.Add(&Input); }
		}
	}
}

#pragma region UPCGExSetCachedDataSettings

#if WITH_EDITOR
void UPCGExSetCachedDataSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	bNeededDependencyBeforeEdit = NeedsExecutionDependency();
	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGExSetCachedDataSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Only an edit that moves the need rewrites the flag: set by hand, it survives every other edit.
	const bool bNeedsDependency = NeedsExecutionDependency();
	if (bNeedsDependency != bNeededDependencyBeforeEdit)
	{
		bExecutionDependencyRequired = bNeedsDependency;
		bNeededDependencyBeforeEdit = bNeedsDependency;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

EPCGChangeType UPCGExSetCachedDataSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(UPCGExSetCachedDataSettings, Mode)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPCGExSetCachedDataSettings, CustomInputPins))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
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
			bExecutionDependencyRequired = NeedsExecutionDependency();
		}
	}
}

FString UPCGExSetCachedDataSettings::GetAdditionalTitleInformation() const
{
	const FString Id = PCGExDataCache::MakeTitleCacheID(CacheID, IsPartitionPrefixed());

	switch (Mode)
	{
	case EPCGExDataCacheWriteMode::Replace:
	case EPCGExDataCacheWriteMode::Append:
		return Id;
	case EPCGExDataCacheWriteMode::Clear:
		return Id.IsEmpty() ? TEXT("Clear") : FString::Printf(TEXT("Clear %s"), *Id);
	case EPCGExDataCacheWriteMode::ClearAll:
		return TEXT("Clear All");
	default:
		// Title path: loud but survivable, so an asset with a retired enumerator can still be opened and fixed.
		ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheWriteMode (%d)"), static_cast<int32>(Mode));
		return TEXT("Invalid Mode");
	}
}

bool UPCGExSetCachedDataSettings::IsPartitionEditable() const
{
	if (Mode == EPCGExDataCacheWriteMode::ClearAll) { return false; }
	return bPrefixWithPartitionId || IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGExSetCachedDataSettings, bPrefixWithPartitionId));
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

	// Clear modes: no data pins at all. See NeedsExecutionDependency for what orders the node.
	if (IsClearMode())
	{
		if (UsesTargetPin())
		{
			PCGEX_PIN_ANY(PCGExDataCache::TargetActorPinLabel, "Actor references naming the actor(s) whose cache to clear.", Required)
		}
		return PinProperties;
	}

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

	// Optional even under Input, so the node keeps forwarding its inputs when no host is named.
	if (UsesTargetPin())
	{
		PCGEX_PIN_ANY(PCGExDataCache::TargetActorPinLabel, "Actor references naming the actor(s) that host the cache.", Normal)
	}
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

	Context->CacheID = Settings->CacheID;

	if (!Settings->IsClearMode())
	{
		// Nothing to write: no host is resolved, so the run creates neither a cache component nor the PCG World Actor.
		TArray<const FPCGTaggedData*> Inputs;
		PCGExSetCachedData::GatherInputs(Context, Settings, Inputs);
		if (Inputs.IsEmpty()) { return true; }
	}

	if (Settings->IsPartitionPrefixed())
	{
		TArray<FName> Keys;
		if (!PCGExDataCache::ResolvePartitionedCacheIDs(Context->ExecutionSource.Get(), Settings->CacheID, MakeArrayView(&Settings->Partition, 1), Keys))
		{
			// Never the bare Cache ID instead: that would write to, or clear, an entry the user did not name.
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("UnresolvedPartition", "The partition prefix could not be resolved (no execution source, or no valid bounds); the cache was left untouched."));
			return true;
		}

		Context->CacheID = Keys[0];
	}

	Settings->GatherTargetReferences(Context, Context->TargetReferences);
	Context->bTouchesCache = true;

	return true;
}

bool FPCGExSetCachedDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(SetCachedData)
	check(IsInGameThread());

	IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	const bool bPreview = PCGExDataCache::IsSourceInPreviewMode(Source);
	UObject* Writer = Cast<UObject>(Source);

	// Resolved in the step that writes or clears: a pooled partition actor can be handed to another cell in between.
	TArray<AActor*> Hosts;
	if (Context->bTouchesCache)
	{
		Settings->ResolveTargets(Context, Context->TargetReferences, /*bCreateWorldActor=*/!Settings->IsClearMode(), Hosts);
	}

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
		for (const AActor* Host : Hosts)
		{
			UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::Find(Host);
			if (!Cache) { continue; }
			if (Settings->Mode == EPCGExDataCacheWriteMode::Clear) { Cache->Clear(Context->CacheID, Writer, bPreview, Settings->bNotifyChange); }
			else { Cache->ClearAll(Writer, bPreview, Settings->bNotifyChange); }
		}
		Context->Done();
		return Context->TryComplete();
	default:
		ensureMsgf(false, TEXT("Unresolvable EPCGExDataCacheWriteMode (%d)"), static_cast<int32>(Settings->Mode));
		return Context->CancelExecution(TEXT("Unresolvable write mode."));
	}

	TArray<const FPCGTaggedData*> Inputs;
	PCGExSetCachedData::GatherInputs(Context, Settings, Inputs);

	for (AActor* Host : Hosts)
	{
		UPCGExDataCacheComponent* Cache = UPCGExDataCacheComponent::FindOrCreate(Host, bPreview);
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

		Cache->Write(Context->CacheID, bAppend, MoveTemp(Duplicates), Writer, bPreview, Settings->bNotifyChange);
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
