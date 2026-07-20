// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"

#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Engine/StaticMesh.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExMeshCollection.generated.h"

class UPCGExMeshCollection;

// Material Override Structures

UENUM(BlueprintType)
enum class EPCGExMaterialVariantsMode : uint8
{
	None   = 0 UMETA(DisplayName = "None", ToolTip="No variants.", ActionIcon="STF_None"),
	Single = 1 UMETA(DisplayName = "Single Slot", ToolTip="Single-slot variants, for when there is only a single material slot override.", ActionIcon="SingleMat"),
	Multi  = 2 UMETA(DisplayName = "Multi Slots", ToolTip="Multi-slot variants, more admin, for when there is multiple material slots for the entry.", ActionIcon="MultiMat"),
};

USTRUCT(BlueprintType, DisplayName="[PCGEx] Material Override Entry")
struct PCGEXCOLLECTIONS_API FPCGExMaterialOverrideEntry
{
	GENERATED_BODY()

	FPCGExMaterialOverrideEntry() = default;

	/** Material slot index. -1 uses the index inside the container. */
	UPROPERTY(EditAnywhere, Category = Settings)
	int32 SlotIndex = -1;

	UPROPERTY(EditAnywhere, Category = Settings)
	TSoftObjectPtr<UMaterialInterface> Material = nullptr;
};

USTRUCT(BlueprintType, DisplayName="[PCGEx] Material Override Collection")
struct PCGEXCOLLECTIONS_API FPCGExMaterialOverrideCollection
{
	GENERATED_BODY()

	virtual ~FPCGExMaterialOverrideCollection() = default;
	FPCGExMaterialOverrideCollection() = default;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(ClampMin=1))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGExMaterialOverrideEntry> Overrides;

	virtual void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const;
	int32 GetHighestIndex() const;
};

USTRUCT(BlueprintType, DisplayName="[PCGEx] Material Override Single Entry")
struct PCGEXCOLLECTIONS_API FPCGExMaterialOverrideSingleEntry
{
	GENERATED_BODY()

	FPCGExMaterialOverrideSingleEntry() = default;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(ClampMin=1))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, Category = Settings)
	TSoftObjectPtr<UMaterialInterface> Material = nullptr;
};

namespace PCGExMeshCollection
{
	/** MicroCache for mesh entries. Builds weighted pick arrays from material override entries.
	 *  Tracks HighestMaterialIndex for pre-sizing material override arrays on descriptors. */
	class PCGEXCOLLECTIONS_API FMicroCache : public PCGExAssetCollection::FMicroCache
	{
		int32 HighestMaterialIndex = -1;

	public:
		FMicroCache() = default;

		virtual PCGExAssetCollection::FTypeId GetTypeId() const override
		{
			return PCGExAssetCollection::TypeIds::Mesh;
		}

		int32 GetHighestIndex() const
		{
			return HighestMaterialIndex;
		}

		void ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideSingleEntry>& Overrides, int32 InSlotIndex = -1);
		void ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideCollection>& Overrides);
	};
}

/** Mesh collection-level globals. Mirrors UPCGExMeshCollection's Settings|Global members 1:1 -- keep names in sync. */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Mesh Collection Globals")
struct PCGEXCOLLECTIONS_API FPCGExMeshCollectionGlobals : public FPCGExCollectionTypeGlobals
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExGlobalVariationRule GlobalDescriptorMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" ├─ Global ISM Settings"))
	FSoftISMComponentDescriptor GlobalISMDescriptor;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Global SM Settings"))
	FPCGExStaticMeshComponentDescriptor GlobalSMDescriptor;
};

/**
 * Mesh collection entry. References a UStaticMesh (or, via the base SubCollection property,
 * any collection type as a subcollection). Adds mesh-specific features on top of the base entry:
 * - Material variants: weighted material override sets (single-slot or multi-slot)
 * - ISM/SM descriptors: per-entry component settings (collision, rendering, etc.)
 * - DescriptorSource: local vs global descriptor inheritance
 *
 * Example of what custom collections can learn from this:
 * - BuildMicroCache() creates a mesh-specific FMicroCache for material variant picking
 * - UpdateStaging() loads the mesh to extract bounds and sockets
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Mesh Collection Entry", meta=(ShortName="Mesh"))
struct PCGEXCOLLECTIONS_API FPCGExMeshCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExMeshCollectionEntry() = default;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Mesh;
	}

	// Mesh-Specific Properties 

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> StaticMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExMaterialVariantsMode MaterialVariants = EPCGExMaterialVariantsMode::None;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" ├─ Slot Index", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Single", EditConditionHides))
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variants", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Single", EditConditionHides))
	TArray<FPCGExMaterialOverrideSingleEntry> MaterialOverrideVariants;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variants", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Multi", EditConditionHides))
	TArray<FPCGExMaterialOverrideCollection> MaterialOverrideVariantsList;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides, DisplayAfter="Variations", InvalidEnumValues="None"))
	EPCGExEntryVariationMode DescriptorSource = EPCGExEntryVariationMode::Local;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" ├─ ISM Settings", EditCondition="!bIsSubCollection && DescriptorSource == EPCGExEntryVariationMode::Local", EditConditionHides, DisplayAfter="DescriptorSource"))
	FSoftISMComponentDescriptor ISMDescriptor;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ SM Settings", EditCondition="!bIsSubCollection && DescriptorSource == EPCGExEntryVariationMode::Local", EditConditionHides, DisplayAfter="ISMDescriptor"))
	FPCGExStaticMeshComponentDescriptor SMDescriptor;

	/** Identity hash of the source actor's UPCGExPropertyCollectionComponent (when present).
	 *  Folded into the entry's content hash so two mesh-classified actors that share the
	 *  same mesh + descriptor + materials but author distinct property-component values land
	 *  in distinct shared entries -- preserving their per-instance PropertyOverrides through
	 *  CompactSharedMesh. 0 means "no property component on the source actor".
	 *
	 *  Persisted: CompactSharedMesh's PreservedByHash lookup (which carries user edits to
	 *  Tags/Category/PropertyOverrides across rebuilds) matches entries by content hash;
	 *  loaded shared entries need the same hash they had at export so the match resolves.
	 *  If the hashing algorithm ever changes, one full rebuild regenerates every entry's
	 *  hash to the new scheme. */
	UPROPERTY()
	uint32 PropertyComponentHash = 0;

	// Asset & Material Handling

	virtual void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	void GetMaterialPaths(int32 PickIndex, TSet<FSoftObjectPath>& OutPaths) const;
	void ApplyMaterials(int32 PickIndex, UStaticMeshComponent* TargetComponent) const;
	void ApplyMaterials(int32 PickIndex, FPCGSoftISMComponentDescriptor& Descriptor) const;

	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;
	virtual void ResolveGlobalsToLocal(const UPCGExAssetCollection* InSourceCollection) override;

	/**
	 * Resolve descriptor inheritance into TargetDescriptor. Accepts any host -- globals read
	 * through the seam; hosts without a mesh block (or null) fall back to the local descriptor.
	 */
	void InitPCGSoftISMDescriptor(const UPCGExAssetCollection* ParentCollection, FPCGSoftISMComponentDescriptor& TargetDescriptor) const;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
#endif

	virtual void BuildMicroCache() override;

	// Typed MicroCache Access
	PCGExMeshCollection::FMicroCache* GetMeshMicroCache() const
	{
		return static_cast<PCGExMeshCollection::FMicroCache*>(MicroCache.Get());
	}
};

/**
 * Concrete collection for static meshes. Demonstrates the full collection pattern:
 * - PCGEX_ASSET_COLLECTION_BODY macro wires up all base class virtuals
 * - GetTypeId() returns TypeIds::Mesh
 * - Global descriptor settings (ISM/SM) can overrule per-entry descriptors
 * - EDITOR_AddBrowserSelectionInternal handles drag-drop from content browser
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Mesh", meta=(ToolTip = "A weighted collection of static meshes with optional material overrides."))
class PCGEXCOLLECTIONS_API UPCGExMeshCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExMeshCollectionEntry)
	friend struct FPCGExMeshCollectionEntry;

public:
	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Mesh;
	}

	// Mesh-Specific Properties

	UPROPERTY(EditAnywhere, Category = "Settings|Global")
	EPCGExGlobalVariationRule GlobalDescriptorMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = "Settings|Global", meta=(DisplayName=" ├─ Global ISM Settings"))
	FSoftISMComponentDescriptor GlobalISMDescriptor;

	UPROPERTY(EditAnywhere, Category = "Settings|Global", meta=(DisplayName=" └─ Global SM Settings"))
	FPCGExStaticMeshComponentDescriptor GlobalSMDescriptor;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExMeshCollectionEntry> Entries;

protected:
	virtual bool GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const override;

public:
#if WITH_EDITOR
	// Editor Functions

	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;

	UFUNCTION()
	void EDITOR_DisableCollisions();

	UFUNCTION()
	void EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode DescriptorSource);
#endif
};
