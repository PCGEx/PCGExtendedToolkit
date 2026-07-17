// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExSettings.h"

#include "PCGExCustomVersion.h"
#include "PCGExVersion.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "PCGExSettingsCacheBody.h"
#include "PCGPin.h"
#include "Core/PCGExContext.h"
#include "Styling/SlateStyle.h"

#include "Helpers/PCGSettingsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSettings"

#if WITH_EDITOR
void UPCGExSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
}

void UPCGExSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	// No fresh-node guard: PCGExDataVersion is resolved in Serialize, so per-block PCGEX_IF_VERSION_LOWER
	// gates already distinguish legacy from current data.
	PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);

	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	PCGExApplyDeprecation(InOutNode);
	
	Super::ApplyDeprecation(InOutNode);
	
	PCGEX_UPDATE_DATA_VERSION_TO_LATEST
	
	ensure(PCGExDataVersion == PCGExVersion::Latest);
}

void UPCGExSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
}

void UPCGExSettings::ResolveDataVersion()
{
	// Source the deprecation version from the package custom version: the engine fills UserDataVersion
	// from GetUserCustomVersionGuid() during Serialize, and it lives in the archive header so it is never
	// dropped by delta serialization. Legacy assets predating the custom version have UserDataVersion < 0:
	// keep their captured per-object PCGExDataVersion if present, else (never stamped) treat as oldest.
	if (UserDataVersion >= 0) { PCGExDataVersion = UserDataVersion; }
	else if (PCGExDataVersion == INDEX_NONE) { PCGExDataVersion = 0; }
	// A node in a legacy package (UserDataVersion < 0) that was never version-stamped (INDEX_NONE) predates
	// deprecation stamping entirely, so it must run ALL deprecation -> treat as oldest (0), NOT current.
	// Genuinely-new nodes never reach this branch: any package a current build saves records the PCGEx
	// custom version in its archive header, so they load with UserDataVersion >= 0 (the branch above).
	// else: keep the captured legacy PCGExDataVersion as-is.
}

bool UPCGExSettings::GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip) const
{
	return PCGEX_CORE_SETTINGS.GetPinExtraIcon(InPin, OutExtraIcon, OutTooltip, InPin->IsOutputPin());
}

void UPCGExSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		const bool bIsInstanced = Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference);
		if (bIsInstanced)
		{
			DirtyCache();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGExSettings::PostLoad()
{
	Super::PostLoad();
}

FGuid UPCGExSettings::GetUserCustomVersionGuid()
{
	return FPCGExCustomVersion::GUID;
}

void UPCGExSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	// After Super, UserDataVersion holds this package's PCGEx custom version (-1 if it predates it).
	// Resolve the effective deprecation version before PostLoad and the graph's deprecation pass run.
	// Fresh nodes need no explicit stamp: any package saved by a current build writes the PCGEx custom
	// version, so UserDataVersion >= 0 on reload and ResolveDataVersion resolves them via that (never the
	// INDEX_NONE->oldest path). Do NOT stamp on save here — it mutates the CDO/templates and poisons the
	// delta-serialization baseline, making legacy assets inherit Latest and skip all deprecation.
	if (Ar.IsLoading()) { ResolveDataVersion(); }
#endif
}

bool UPCGExSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (PCGEX_CORE_SETTINGS.bToneDownOptionalPins && !InPin->Properties.IsRequiredPin() && !InPin->IsOutputPin())
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGExData::EIOInit UPCGExSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::NoInit;
}

bool UPCGExSettings::GetForceOffThreadPrepare(const FPCGExContext* InContext) const
{
	return bForceOffThreadPrepare || (PCGEX_CORE_SETTINGS.bRuntimeAlwaysOffThread && InContext->IsRuntimeGen());
}

bool UPCGExSettings::GetForceOffThreadExecute(const FPCGExContext* InContext) const
{
	return bForceOffThreadExecute || (PCGEX_CORE_SETTINGS.bRuntimeAlwaysOffThread && InContext->IsRuntimeGen());
}

bool UPCGExSettings::WantsResourcesCached() const
{
	PCGEX_GET_OPTION_STATE(CacheLoadedResources, bCacheLoadedResources)
}

#if WITH_EDITOR
void UPCGExSettings::EDITOR_OpenNodeDocumentation() const
{
	const FString META_PCGExDocURL = TEXT("PCGExNodeLibraryDoc");
	const FString META_PCGExDocNodeLibraryBaseURL = TEXT("https://pcgex.gitbook.io/pcgex/node-library/");

	const FString URL = META_PCGExDocNodeLibraryBaseURL + GetClass()->GetMetaData(*META_PCGExDocURL);
	FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
}
#endif

bool UPCGExSettings::SupportsDataStealing() const
{
	return false;
}

bool UPCGExSettings::ShouldCache() const
{
	if (!IsCacheable())
	{
		return false;
	}
	PCGEX_GET_OPTION_STATE(CacheData, bDefaultCacheNodeOutput)
}

bool UPCGExSettings::WantsScopedAttributeGet() const
{
	PCGEX_GET_OPTION_STATE(ScopedAttributeGet, bDefaultScopedAttributeGet)
}

bool UPCGExSettings::WantsBulkInitData() const
{
	PCGEX_GET_OPTION_STATE(BulkInitData, bBulkInitData)
}

#undef LOCTEXT_NAMESPACE
