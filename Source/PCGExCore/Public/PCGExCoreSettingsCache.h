// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExSettingsCacheBody.h"

class UPCGPin;

#define PCGEX_CORE_SETTINGS PCGEX_SETTINGS_INST(Core)

#define PCGEX_EMPLACE_PIN_OUT(_ID, _TOOLTIP) PinIndex = PCGEX_CORE_SETTINGS.OutPinInfos.Emplace(FName("PCGEx.Pin." # _ID), TEXT(_TOOLTIP))
#define PCGEX_MAP_PIN_OUT(_ID) PCGEX_CORE_SETTINGS.OutPinInfosMap.Add(FName(TEXT(_ID)), PinIndex);

#define PCGEX_EMPLACE_PIN_IN(_ID, _TOOLTIP) PinIndex = PCGEX_CORE_SETTINGS.InPinInfos.Emplace(FName("PCGEx.Pin." # _ID), TEXT(_TOOLTIP ", supports multiple inputs."))
#define PCGEX_MAP_PIN_IN(_ID) PCGEX_CORE_SETTINGS.InPinInfosMap.Add(FName(TEXT(_ID)), PinIndex);

#define PCGEX_NODE_COLOR_NAME(_COLOR) PCGEX_CORE_SETTINGS.GetColor(FName(#_COLOR))
#define PCGEX_NODE_COLOR_BLEND(_COLOR1, _COLOR2) FLinearColor::LerpUsingHSV(PCGEX_CORE_SETTINGS.GetColor(FName(#_COLOR1)), PCGEX_CORE_SETTINGS.GetColor(FName(#_COLOR2)), 0.3f)
#define PCGEX_NODE_COLOR_OPTIN_NAME(_COLOR) PCGEX_CORE_SETTINGS.GetColorOptIn(FName(#_COLOR))
#define PCGEX_NODE_COLOR_OPTIN(_COLOR) PCGEX_CORE_SETTINGS.GetColorOptIn(_COLOR)

struct PCGEXCORE_API FPCGExCoreSettingsCache
{
	PCGEX_SETTING_CACHE_BODY(Core)
	FVector WorldUp = FVector::UpVector;
	FVector WorldForward = FVector::ForwardVector;

	bool bDefaultCacheNodeOutput = true;
	bool bCacheLoadedResources = true;
	bool bDefaultScopedAttributeGet = true;
	bool bBulkInitData = false;
	bool bUseDelaunator = true;
	bool bAssertOnEmptyThread = true;
	bool bRuntimeAlwaysOffThread = false;

	bool bUseNativeColorsIfPossible = true;
	bool bToneDownOptionalPins = true;

	bool bCacheClusters = true;
	bool bDefaultScopedIndexLookupBuild = true;
	bool bDefaultBuildAndCacheClusters = true;

	int32 SmallPointsSize = 1024;

	bool IsSmallPointSize(const int32 InNum) const
	{
		return InNum <= SmallPointsSize;
	}

	int32 SmallClusterSize = 512;

	int32 PointsDefaultBatchChunkSize = 1024;

	int32 GetPointsBatchChunkSize(const int32 In = -1) const
	{
		return FMath::Max(In <= -1 ? PointsDefaultBatchChunkSize : In, 1);
	}

	int32 ClusterDefaultBatchChunkSize = 512;

	int32 GetClusterBatchChunkSize(const int32 In = -1) const
	{
		return FMath::Max(In <= -1 ? ClusterDefaultBatchChunkSize : In, 1);
	}

#if WITH_EDITOR

	TMap<FName, FLinearColor> ColorsMap;

	FLinearColor GetColor(const FName InColor) const;
	FLinearColor GetColorOptIn(const FName InColor) const;
	FLinearColor GetColorOptIn(const FLinearColor InColor) const;
	FLinearColor GetColorOptIn(const FName InColor, const FLinearColor InNative) const;

	struct FPinInfos
	{
		FName Icon = NAME_None;
		FText Tooltip = FText();

		FPinInfos() = default;

		FPinInfos(const FName InIcon, const FString& InTooltip)
			: Icon(InIcon)
			  , Tooltip(FText::FromString(InTooltip))
		{
		}

		~FPinInfos() = default;
	};

	TArray<FPinInfos> InPinInfos;
	TArray<FPinInfos> OutPinInfos;
	TMap<FName, int32> InPinInfosMap;
	TMap<FName, int32> OutPinInfosMap;

	bool GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip, bool bIsOutPin = false) const;

#endif
};
