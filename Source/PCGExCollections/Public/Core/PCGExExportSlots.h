// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPtr.h"

#include "PCGExExportSlots.generated.h"

class UPCGExAssetCollection;

/**
 * One export handler's hand-off for a Shared-scope slot, captured per PCGDataAsset entry during
 * export and merged across sibling entries by the shared compaction. Persisted (editor-only) so a
 * sibling rebuild can recompact without re-walking this entry's source.
 */
USTRUCT()
struct PCGEXCOLLECTIONS_API FPCGExExportSlotCapture
{
	GENERATED_BODY()

	UPROPERTY()
	FName SlotId = NAME_None;

	/** Entry snapshots of the slot's registered EntryStruct, in local-index order. */
	UPROPERTY()
	TArray<FInstancedStruct> Entries;

	/** One int32 per point on the slot's pin: -1 = no pick; else a local entry index, packed with the
	 *  secondary index when the slot supports secondaries (FPCGExLevelExportContext::PackLocalPick). */
	UPROPERTY()
	TArray<int32> LocalPicks;

	/** Per-property "inherited defaults" view aggregated by the handler (mesh property components).
	 *  Recomputed on every export; feeds the shared collection's CollectionProperties. */
	UPROPERTY(Transient)
	TArray<FInstancedStruct> InheritedDefaults;
};

/**
 * One collection slot of the PCGDataAsset machinery: embedded working object plus its externalized
 * mirror. Held per entry for PerEntry-scope slots and on the type state for Shared-scope slots.
 * Collection is null in external mode once externalized (see ExternalizeSlotCollectionsFor).
 */
USTRUCT()
struct PCGEXCOLLECTIONS_API FPCGExExportCollectionSlot
{
	GENERATED_BODY()

	UPROPERTY()
	FName SlotId = NAME_None;

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExAssetCollection> Collection;

	UPROPERTY()
	TSoftObjectPtr<UPCGExAssetCollection> External;
};

namespace PCGExExportSlots
{
	template <typename TSlot>
	TSlot* Find(TArray<TSlot>& Slots, const FName SlotId)
	{
		return Slots.FindByPredicate([SlotId](const TSlot& S) { return S.SlotId == SlotId; });
	}

	template <typename TSlot>
	const TSlot* Find(const TArray<TSlot>& Slots, const FName SlotId)
	{
		return Slots.FindByPredicate([SlotId](const TSlot& S) { return S.SlotId == SlotId; });
	}

	template <typename TSlot>
	TSlot& FindOrAdd(TArray<TSlot>& Slots, const FName SlotId)
	{
		if (TSlot* Found = Find(Slots, SlotId))
		{
			return *Found;
		}
		TSlot& Added = Slots.AddDefaulted_GetRef();
		Added.SlotId = SlotId;
		return Added;
	}
}
