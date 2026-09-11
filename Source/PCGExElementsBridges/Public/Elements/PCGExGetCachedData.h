// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPin.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Helpers/PCGExDataCacheHelpers.h"

#include "PCGExGetCachedData.generated.h"

/**
 * Get Cached Data.
 * Reads data stored on the target actor's PCGEx Data Cache component by Set Cached Data. The cache is empty on
 * the first generation, so branch on the Status pin. Data pins with nothing to output are deactivated.
 * Cached data is handed out by pointer, so the cache is a hidden second consumer: a downstream node with
 * Steal Data enabled would mutate the persisted objects in place. The target is resolved and read on the
 * game thread during preparation, staging happens off-thread.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Misc", meta = (Keywords = "pcgex cache read restore previous generation data", PCGExNodeLibraryDoc = "utilities/data-cache/get-cached-data"))
class UPCGExGetCachedDataSettings : public UPCGExDataCacheSettingsBase
{
	GENERATED_BODY()

	friend class FPCGExGetCachedDataElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetCachedData, "Get Cached Data", "Reads data stored on the target actor's PCGEx Data Cache component by Set Cached Data. Empty on the first generation; branch on the Status pin.");

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }
	virtual FLinearColor GetNodeTitleColor() const override;
#endif

	virtual FString GetAdditionalTitleInformation() const override;

	/** Data pins with nothing on them gray out instead of vanishing, so downstream wires survive a cache miss. */
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** ID to read. Must match the ID used on Set Cached Data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "!bReadAllEntries"))
	FName CacheID = FName("Default");

	/** Read every entry on the cache instead of a single ID. Enable Tag With Cache ID to tell them apart. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bReadAllEntries = false;

	/** Extra output pins: cached data whose stored pin label matches one exactly is routed there, the rest goes to
	 *  Out. Copy-paste the Set node's Custom Input Pins here. Out and Status are reserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pins", meta = (TitleProperty = "{Label}"))
	TArray<FPCGPinProperties> CustomOutputPins;

	/** Tag every output with 'CacheID:<id>' so entries can be told apart, mostly useful with Read All Entries. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	bool bTagWithCacheID = false;

	/** Emit a Status attribute set: one row per target actor with Found, DataCount, ActorReference and CacheID. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	bool bOutputStatus = true;

	/** Custom output pins minus None labels, reserved labels and duplicates; the pins actually declared. */
	TArray<FPCGPinProperties> GetSanitizedCustomOutputPins() const;
};

struct FPCGExGetCachedDataContext final : FPCGExContext
{
	struct FStatusRow
	{
		FSoftObjectPath Actor;
		bool bFound = false;
		int32 DataCount = 0;
	};

	/** Cached data copied out during Boot (game thread), pin = the label it was stored with. Doubles as the GC root:
	 *  StageOutput(None) does not root, and the cache may drop its own reference before we flush. */
	FPCGDataCollection Reads;

	/** One per target actor; a single not-found row when no actor resolved, so Status always has something to branch on. */
	TArray<FStatusRow> StatusRows;

protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;
};

class FPCGExGetCachedDataElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetCachedData)
	// Target resolution touches actors; the read itself is a pointer copy.
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	/** The cache changes with no dependency-CRC change; a cached result would be stale. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

	/** Every Set stores fresh objects; a content CRC keeps downstream cached when the data is actually unchanged. */
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override { return true; }

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
