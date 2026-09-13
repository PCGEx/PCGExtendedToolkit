// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPin.h"

#include "Core/PCGExSettings.h"
#include "Data/PCGExDataTags.h" // TagSeparator

#include "PCGExDataCacheHelpers.generated.h"

class AActor;
class IPCGGraphExecutionSource;
class UPCGData;
struct FPCGExContext;

UENUM()
enum class EPCGExDataCacheTarget : uint8
{
	ExecutingActor = 0 UMETA(DisplayName = "Executing Actor", Tooltip = "The actor that owns the executing PCG component. With partitioning, this is the partition actor of the current grid cell."),
	OriginalActor  = 1 UMETA(DisplayName = "Original Actor", Tooltip = "The actor that owns the original (non-partitioned) PCG component. Same as Executing Actor when not partitioned."),
	WorldActor     = 2 UMETA(DisplayName = "PCG World Actor", Tooltip = "The level's PCG World Actor, shared by every component in the world."),
};

namespace PCGExDataCache
{
	const FName TargetActorPinLabel = TEXT("Target Actor");
	const FName StatusPinLabel = TEXT("Status");

	const FName FoundAttributeName = TEXT("Found");
	const FName DataCountAttributeName = TEXT("DataCount");
	const FName CacheIDAttributeName = TEXT("CacheID");

	/** 'CacheID:<id>' -- a Key:Value tag every PCGEx tag reader parses. */
	inline FString MakeCacheIDTag(const FName InId)
	{
		return CacheIDAttributeName.ToString() + PCGExData::TagSeparator + InId.ToString();
	}

	/**
	 * True when InData references no UPCGData outside its own outer chain. Property-based (FReferenceFinder), not the
	 * cooperative VisitDataNetwork virtual, so projection / collision-wrapper operands are caught too.
	 */
	PCGEXELEMENTSBRIDGES_API bool IsSelfContained(const UPCGData* InData);

	/** User-declared pins minus None labels, reserved labels and duplicates. Set and Get must agree on this. */
	PCGEXELEMENTSBRIDGES_API TArray<FPCGPinProperties> SanitizePins(const TArray<FPCGPinProperties>& InPins, const TArrayView<const FName> InReservedLabels);

	/** True when the settings live on a node whose Target Actor pin has an edge. */
	PCGEXELEMENTSBRIDGES_API bool IsTargetPinConnected(const UPCGSettings* InSettings);

	/** The actor a source executes on (its owner). 5.7's execution-state interface has no target query, hence the seam. */
	PCGEXELEMENTSBRIDGES_API AActor* GetSourceActor(const IPCGGraphExecutionSource* InSource);

	/** Whether a source generates in preview editing mode. Same 5.7 seam as GetSourceActor. */
	PCGEXELEMENTSBRIDGES_API bool IsSourceInPreviewMode(const IPCGGraphExecutionSource* InSource);
}

/** Shared target-actor surface of Set Cached Data and Get Cached Data. */
UCLASS(Abstract)
class PCGEXELEMENTSBRIDGES_API UPCGExDataCacheSettingsBase : public UPCGExSettings
{
	GENERATED_BODY()

public:
	/** Which actor hosts the cache. Ignored when the Target Actor pin is connected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "IsTargetPinUnconnected()"))
	EPCGExDataCacheTarget Target = EPCGExDataCacheTarget::ExecutingActor;

	/** 'FSoftObjectPath' attribute read on the Target Actor pin when it is connected. A component reference resolves to its owner. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName ActorReferenceAttribute = FName("ActorReference");

	/** Suppress the warning when no target actor could be resolved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietMissingTargetWarning = false;

	/**
	 * Game thread only (resolves soft paths, may spawn the PCG World Actor). When the Target Actor pin carries data,
	 * every unique actor it references is a target; otherwise the single actor named by Target. Warns (unless quiet)
	 * when nothing resolves. bCreateWorldActor: writers spawn a missing world actor, readers only find it.
	 */
	void ResolveTargets(FPCGExContext* InContext, const bool bCreateWorldActor, TArray<AActor*>& OutActors) const;

protected:
#if WITH_EDITOR
	UFUNCTION()
	bool IsTargetPinUnconnected() const { return !PCGExDataCache::IsTargetPinConnected(this); }
#endif
};
