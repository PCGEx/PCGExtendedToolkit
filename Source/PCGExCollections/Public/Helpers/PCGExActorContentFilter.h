// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExActorContentFilter.generated.h"

class AActor;
class UPCGExAssetCollection;

/**
 * Abstract base for actor content filtering.
 * Instanced on collections/exporters via EditInlineNew/DefaultToInstanced.
 * All actual work is editor-only.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class PCGEXCOLLECTIONS_API UPCGExActorContentFilter : public UObject
{
	GENERATED_BODY()

public:
	/** Infrastructure checks shared by all callers: hidden, editor-only,
	 *  main-world-only, ALevelScriptActor, AInfo, ABrush, ANavigationData. */
	static bool IsInfrastructureActor(AActor* Actor);

	/** Convenience: delegates to filter if non-null, else falls back to IsInfrastructureActor. */
	static bool StaticPassesFilter(
		const UPCGExActorContentFilter* Filter, AActor* Actor,
		UPCGExAssetCollection* OwningCollection = nullptr, int32 EntryIndex = -1);

	/** Override for custom filtering logic.
	 *  OwningCollection + EntryIndex provide optional context about which collection/entry
	 *  is being processed. May be null/-1 when called outside an entry context. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Filtering")
	bool PassesFilter(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const;

	virtual bool PassesFilter_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const;
};

/**
 * Default content filter with tag/class include/exclude lists.
 */
UCLASS(DisplayName = "Default Actor Content Filter")
class PCGEXCOLLECTIONS_API UPCGExDefaultActorContentFilter : public UPCGExActorContentFilter
{
	GENERATED_BODY()

public:
	/** If non-empty, only actors with at least one of these tags pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FName> IncludeTags;

	/** Actors with any of these tags are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FName> ExcludeTags;

	/** If non-empty, only actors of these classes (or subclasses) pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<TSoftClassPtr<AActor>> IncludeClasses;

	/** Actors of these classes (or subclasses) are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<TSoftClassPtr<AActor>> ExcludeClasses;

	virtual bool PassesFilter_Implementation(AActor* Actor, UPCGExAssetCollection* OwningCollection, int32 EntryIndex) const override;
};
