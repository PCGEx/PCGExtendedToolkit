// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGPin.h"
#include "UObject/SoftObjectPath.h"

#include "Core/PCGExSettings.h"
#include "Data/PCGExDataTags.h" // TagSeparator
#include "Helpers/PCGExPartitionActorHelpers.h" // PartitionIdAttributeName

#include "PCGExDataCacheHelpers.generated.h"

class AActor;
class IPCGGraphExecutionSource;
class UPCGComponent;
class UPCGData;
struct FPCGExContext;
struct FPCGExPartitionQuery;

UENUM()
enum class EPCGExDataCacheTarget : uint8
{
	ExecutingActor = 0 UMETA(DisplayName = "Executing Actor", Tooltip = "The actor that owns the executing PCG component. With partitioning, this is the partition actor of the current grid cell."),
	OriginalActor  = 1 UMETA(DisplayName = "Original Actor", Tooltip = "The actor that owns the original (non-partitioned) PCG component. Same as Executing Actor when not partitioned."),
	WorldActor     = 2 UMETA(DisplayName = "PCG World Actor", Tooltip = "The level's PCG World Actor, shared by every component in the world."),
	Input          = 3 UMETA(DisplayName = "Input", Tooltip = "Every actor referenced on the Target Actor pin. An empty pin names no host: nothing is read or written."),
};

namespace PCGExDataCache
{
	const FName TargetActorPinLabel = TEXT("Target Actor");
	const FName StatusPinLabel = TEXT("Status");

	const FName FoundAttributeName = TEXT("Found");
	const FName DataCountAttributeName = TEXT("DataCount");
	const FName CacheIDAttributeName = TEXT("CacheID");

	/** One row of the Target Actor pin. */
	struct FTargetReference
	{
		FSoftObjectPath Actor;

		/** The partition the reference was taken for, when the row says so; empty takes the reference as-is. */
		FString PartitionId;
	};

	/** 'CacheID:<id>' -- a Key:Value tag every PCGEx tag reader parses. */
	inline FString MakeCacheIDTag(const FName InId)
	{
		return CacheIDAttributeName.ToString() + PCGExData::TagSeparator + InId.ToString();
	}

	/** '<PartitionId>_<CacheID>'. Keys are saved inside the host actor, so this format is a wire format. */
	PCGEXELEMENTSBRIDGES_API FName MakePartitionedCacheID(const FString& InPartitionId, const FName InCacheID);

	/** One key per query for the cell InAnchor falls in; duplicates are dropped, first occurrence wins. InComponent may be null. */
	PCGEXELEMENTSBRIDGES_API void ResolvePartitionedCacheIDs(const FVector& InAnchor, const UPCGComponent* InComponent, const FName InCacheID, const TConstArrayView<FPCGExPartitionQuery> InQueries, TArray<FName>& OutCacheIDs);

	/** Same, anchored on the executing source's own cell. False when it has no usable bounds; never yields the bare cache ID. */
	PCGEXELEMENTSBRIDGES_API bool ResolvePartitionedCacheIDs(const IPCGGraphExecutionSource* InSource, const FName InCacheID, const TConstArrayView<FPCGExPartitionQuery> InQueries, TArray<FName>& OutCacheIDs);

	/** Cache ID as a node title shows it: the partition prefix is only known at execution, so it is a placeholder. */
	PCGEXELEMENTSBRIDGES_API FString MakeTitleCacheID(const FName InCacheID, const bool bPartitioned);

	/**
	 * True when InData references no UPCGData outside its own outer chain. Property-based (FReferenceFinder), not the
	 * cooperative VisitDataNetwork virtual, so projection / collision-wrapper operands are caught too.
	 */
	PCGEXELEMENTSBRIDGES_API bool IsSelfContained(const UPCGData* InData);

	/** User-declared pins minus None labels, reserved labels and duplicates. Set and Get must agree on this. */
	PCGEXELEMENTSBRIDGES_API TArray<FPCGPinProperties> SanitizePins(const TArray<FPCGPinProperties>& InPins, const TArrayView<const FName> InReservedLabels);

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
	//~Begin UPCGSettings
	/** The Target Actor pin names hosts; it is never what a disabled node forwards. */
	virtual bool DoesPinSupportPassThrough(UPCGPin* InPin) const override;
	//~End UPCGSettings

	//~Begin UPCGExSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
#endif
	//~End UPCGExSettings

protected:
#if WITH_EDITOR
	/** Target decides which pins exist and which are required: culling is compiled, so the change is structural. */
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

public:
	/** Which actor hosts the cache. Input adds the Target Actor pin, so not overridable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExDataCacheTarget Target = EPCGExDataCacheTarget::ExecutingActor;

	/** 'FSoftObjectPath' attribute read on the Target Actor pin. A component reference resolves to its owner. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Target == EPCGExDataCacheTarget::Input", EditConditionHides))
	FName ActorReferenceAttribute = FName("ActorReference");

	/** 'FString' attribute read next to each reference, as written by Get Partition Actor. A referenced partition actor
	 *  must still stand for that partition: runtime generated ones are pooled and reused. None skips the check. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Target == EPCGExDataCacheTarget::Input", EditConditionHides))
	FName PartitionIdAttribute = PCGExPartitionActors::PartitionIdAttributeName;

	/** Suppress the warnings about target actors that cannot be resolved: none at all, or some of the references. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietMissingTargetWarning = false;

	/** The Target Actor pin only exists, and is only read, under Input. */
	bool UsesTargetPin() const { return Target == EPCGExDataCacheTarget::Input; }

	/** Under Input, every unique reference on the Target Actor pin, in first-seen order; nothing otherwise.
	 *  Read once per execution: the pin does not change, only what its references resolve to. */
	void GatherTargetReferences(FPCGExContext* InContext, TArray<PCGExDataCache::FTargetReference>& OutReferences) const;

	/**
	 * Game thread only (resolves soft paths, may spawn the PCG World Actor). Under Input, the unique actors InReferences
	 * resolve to, in order; otherwise the single actor named by Target, never the pin.
	 * A reference resolves when its actor is loaded and, for a partition actor on a row that names a partition, still
	 * stands for it. Resolve in the same game-thread step that uses the actors: a pooled one can move in between.
	 * bCreateWorldActor: writers spawn a missing world actor, readers only find it. OutUnresolved receives the
	 * references that did not resolve. bQuiet skips the warnings, for callers that resolve repeatedly.
	 */
	void ResolveTargets(FPCGExContext* InContext, const TConstArrayView<PCGExDataCache::FTargetReference> InReferences, const bool bCreateWorldActor, TArray<AActor*>& OutActors, TArray<PCGExDataCache::FTargetReference>* OutUnresolved = nullptr, const bool bQuiet = false) const;
};
