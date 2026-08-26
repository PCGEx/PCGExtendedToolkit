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
 * an unset pick (EntryId 0) resolves to 0 silently.
 *
 * Collection is structural: the schema pins it, overrides pick the entry (SyncStructuralFromSchema).
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Collection Entry")
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

	/** Hash for InRef, 0 when its collection isn't loaded or the id is unknown. OutCollection set only on success. */
	static uint64 ResolveHash(const FPCGExCollectionEntryRef& InRef, UPCGExAssetCollection*& OutCollection);

	/** ResolveHash plus a once-per-(collection, property)-per-session warning on failure. Stateless
	 *  w.r.t. the property instance -- safe on SHARED source instances (Tuple rows, Distribute) and
	 *  from parallel writers. */
	static uint64 ResolveHashWarned(const FPCGExCollectionEntryRef& InRef, FName InPropertyName, UPCGExAssetCollection*& OutCollection);

	/** ResolveHashWarned on Value. */
	uint64 ResolveOwnHash() const;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
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

	virtual FName GetTypeName() const override
	{
		return FName("CollectionEntry");
	}

	virtual FName GetDisplayTypeName() const override;

	virtual FName ResolveOutputAttributeName(FName InEffectiveName) const override;
	virtual FName GetOutputSidecarPin() const override;
	virtual void WriteOutputSidecar(UPCGMetadata* InSidecar) const override;

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	virtual void GatherOutputDependencies(TSet<FSoftObjectPath>& OutPaths) const override;
};
