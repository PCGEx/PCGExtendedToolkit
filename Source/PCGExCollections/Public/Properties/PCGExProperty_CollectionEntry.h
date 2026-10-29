// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"

#include "PCGExProperty_CollectionEntry.generated.h"

class UPCGExAssetCollection;

/** Authored identity of one collection entry: the collection (soft, never loaded by the property) + its stable EntryId. */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExCollectionEntryRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Property")
	TSoftObjectPtr<UPCGExAssetCollection> Collection;

	/** FPCGExAssetCollectionEntry::EntryId of the picked entry; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Property")
	int32 EntryId = 0;

	/** Schema-authored (structural): locks the collection so overrides can only pick an entry within it. Off = overrides may retarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Property")
	bool bLockCollection = true;

	bool operator==(const FPCGExCollectionEntryRef& Other) const
	{
		return Collection == Other.Collection && EntryId == Other.EntryId && bLockCollection == Other.bLockCollection;
	}
};

/**
 * Entry-picker property: outputs the canonical pick hash (PickHash::Pack) of the authored entry as int64,
 * under the staging-layer name PCGEx/CollectionEntry/<output name>, and contributes the collection to the
 * hosting node's "Map" sidecar. Downstream, any FPickUnpacker consumer reads it with StagingLayer = <name> --
 * consumers default to StagingLayer = None (the bare default layer), which a column can never produce:
 * schema columns are always named, and the bare layer is reserved for staging producers (by design).
 *
 * "Loaded or null": the property never loads its collection. Hosting nodes preload it through
 * RegisterAssetDependencies (GatherSoftObjectPaths surfaces the path); an unloaded collection or an unknown
 * EntryId resolves to hash 0 (unresolvable downstream) with one warning per (collection, property) per session;
 * an unset pick (EntryId 0) resolves to 0 silently. Parallel writers bind their sources first (BindWriteSources).
 *
 * Collection is structural: the schema pins it, overrides pick the entry (SyncStructuralFromSchema).
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue, PCGExStructuralValue), DisplayName="Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExProperty_CollectionEntry : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FPCGExCollectionEntryRef Value;

protected:
	TSharedPtr<PCGExData::TBuffer<int64>> OutputBuffer;

	/** Collections copied through this instance (clone-side; CopyValueFrom records them). Transient AND
	 *  reflected on purpose: FInstancedStruct copies are per-UPROPERTY, and sidecar flushes read a copy. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPCGExAssetCollection>> TouchedCollections;

	/** Writer-side collections by path: filled by BindWriteSources and by CopyValueFrom (both single-threaded on a
	 *  writer instance), only read by writes. Empty on shared source instances. */
	TMap<FSoftObjectPath, TWeakObjectPtr<UPCGExAssetCollection>> ResolvedCollections;

	/** InRef's collection from ResolvedCollections, else a GC-guarded resolve (writes run on workers). */
	UPCGExAssetCollection* FindCollection(const FPCGExCollectionEntryRef& InRef) const;

	/** Hash for InRef in InCollection, 0 when the collection is null or the id is unknown. OutCollection set only on success. */
	static uint64 ResolveHash(const FPCGExCollectionEntryRef& InRef, UPCGExAssetCollection* InCollection, UPCGExAssetCollection*& OutCollection);

	/** ResolveHash plus a once-per-(collection, property)-per-session warning on failure. Touches no instance state. */
	static uint64 ResolveHashWarned(const FPCGExCollectionEntryRef& InRef, UPCGExAssetCollection* InCollection, FName InPropertyName, UPCGExAssetCollection*& OutCollection);

	/** ResolveHashWarned on Value. */
	uint64 ResolveOwnHash() const;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void BindWriteSources(TConstArrayView<const FPCGExProperty*> InSources) override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;
	virtual bool SyncStructuralFromSchema(const FPCGExProperty& Schema) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Integer64;
	}

	/** The int64 it outputs is a pick hash derived from Value: it says nothing another type could take as a value. */
	virtual bool SupportsValueConversion() const override
	{
		return false;
	}

	virtual FName GetTypeName() const override
	{
		return FName("CollectionEntry");
	}

	virtual FName GetDisplayTypeName() const override;

	virtual FName ResolveOutputAttributeName(FName InEffectiveName) const override;
	virtual FName GetOutputSidecarPin() const override;
	virtual void WriteOutputSidecar(UPCGMetadata* InSidecar) const override;

	/** A pick hash does not survive a float round-trip -- opted out of packed-float layouts entirely. */
	virtual int32 GetPackedFloatCount() const override
	{
		return 0;
	}

	virtual bool PackFloats(TArrayView<float> OutFloats) const override
	{
		return false;
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	virtual void GatherOutputDependencies(TSet<FSoftObjectPath>& OutPaths) const override;
};
