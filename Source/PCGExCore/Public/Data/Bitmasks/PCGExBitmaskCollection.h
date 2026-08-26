// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBitmaskDetails.h"
#include "Engine/DataAsset.h"

#include "PCGExBitmaskCollection.generated.h"

class UPCGExBitmaskCollection;

USTRUCT(BlueprintType, DisplayName="[PCGEx] Bitmask Collection Entry")
struct PCGEXCORE_API FPCGExBitmaskCollectionEntry
{
	GENERATED_BODY()
	virtual ~FPCGExBitmaskCollectionEntry() = default;

	FPCGExBitmaskCollectionEntry() = default;

	/** Unique name to reference this bitmask entry. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FName Identifier = NAME_None;

	/** The bitmask value for this entry. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExBitmask Bitmask;

	/** Optional direction associated with this entry (used for directional adjacency). */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector Direction = FVector::UpVector;

	FORCEINLINE FVector GetDirection() const
	{
		return Direction.GetSafeNormal();
	}

	FORCEINLINE void GetDirection(FVector& OutDir) const
	{
		OutDir = Direction.GetSafeNormal();
	}

	PCGExBitmask::FCachedRef CachedBitmask;

	virtual void EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const;
	void RebuildCache();
};

namespace PCGExBitmaskCollection
{
	class PCGEXCORE_API FCache : public TSharedFromThis<FCache>
	{
	public:
		TArray<PCGExBitmask::FCachedRef> Bitmasks;
		TMap<FName, int32> BitmaskMap;
		TArray<FString> Identifiers;

		explicit FCache() = default;
		~FCache() = default;

		bool TryGetBitmask(const FName Identifier, int64& OutBitmask) const;
		bool TryGetBitmask(const FName Identifier, PCGExBitmask::FCachedRef& OutCachedBitmask) const;

		bool IsEmpty() const
		{
			return Bitmasks.IsEmpty();
		}
	};
}

UCLASS(BlueprintType, DisplayName="[PCGEx] Bitmask Collection", meta=(ToolTip = "A collection of named bitmask values for tagging and adjacency testing.", PCGExNodeLibraryDoc="metadata/bitmasks/bitmask-collection"))
class PCGEXCORE_API UPCGExBitmaskCollection : public UDataAsset
{
	mutable FRWLock CacheLock;

	GENERATED_BODY()

	friend struct FPCGExBitmaskCollectionEntry;
	friend class UPCGExMeshSelectorBase;

public:
	PCGExBitmaskCollection::FCache* LoadCache();
	virtual void InvalidateCache();

	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;

	virtual void EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const;

#if WITH_EDITOR
	bool HasCircularDependency(const UPCGExBitmaskCollection* OtherCollection) const;
	bool HasCircularDependency(TSet<const UPCGExBitmaskCollection*>& InReferences) const;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	TArray<FName> EDITOR_GetIdentifierOptions() const;
#endif


#if WITH_EDITORONLY_DATA
	/** Dev notes/comments. Editor-only data.  */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayPriority=-1, MultiLine))
	FString Notes;
#endif

	virtual void BuildCache();

	virtual bool IsValidIndex(const int32 InIndex) const
	{
		return false;
	}

	virtual int32 NumEntries() const
	{
		return 0;
	}

	/** Named bitmask entries in this collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(TitleProperty="{Identifier}   |   {Direction}"))
	TArray<FPCGExBitmaskCollectionEntry> Entries;

protected:
	UPROPERTY()
	bool bCacheNeedsRebuild = true;

	TSharedPtr<PCGExBitmaskCollection::FCache> Cache;

#if WITH_EDITOR
	void EDITOR_SetDirty()
	{
		InvalidateCache();
	}
#endif
};
