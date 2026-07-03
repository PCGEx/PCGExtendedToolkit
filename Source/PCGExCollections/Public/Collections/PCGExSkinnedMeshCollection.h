// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"

#include "Collections/PCGExMeshCollection.h"
#include "Engine/SkinnedAsset.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "MeshSelectors/PCGSkinnedMeshDescriptor.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExSkinnedMeshCollection.generated.h"

class UPCGExSkinnedMeshCollection;
class UInstancedSkinnedMeshComponent;

namespace PCGExSkinnedMeshCollection
{
	/** MicroCache for skinned mesh entries. Mirrors PCGExMeshCollection::FMicroCache but reports
	 *  TypeIds::SkinnedMesh so type-aware code paths can distinguish it. Reuses the material
	 *  override entry types from PCGExMeshCollection (descriptor-agnostic). */
	class PCGEXCOLLECTIONS_API FMicroCache : public PCGExAssetCollection::FMicroCache
	{
		int32 HighestMaterialIndex = -1;

	public:
		FMicroCache() = default;

		virtual PCGExAssetCollection::FTypeId GetTypeId() const override
		{
			return PCGExAssetCollection::TypeIds::SkinnedMesh;
		}

		int32 GetHighestIndex() const
		{
			return HighestMaterialIndex;
		}

		void ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideSingleEntry>& Overrides, int32 InSlotIndex = -1);
		void ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideCollection>& Overrides);
	};
}

/**
 * Skinned mesh collection entry. References a USkinnedAsset (or, via the base SubCollection
 * property, any collection type as a subcollection). Mirrors FPCGExMeshCollectionEntry
 * feature-for-feature, but with a single skinned-mesh component descriptor
 * (FPCGSoftSkinnedMeshComponentDescriptor) used directly -- no PCGEx wrapper.
 *
 * AnimationIndex on each spawned FPCGSkinnedMeshInstance is a per-instance selector-scoped
 * concern handled by the staged selector, not by the collection entry.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Skinned Mesh Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExSkinnedMeshCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExSkinnedMeshCollectionEntry() = default;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::SkinnedMesh;
	}

	// Skinned-Mesh-Specific Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<USkinnedAsset> SkinnedAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExMaterialVariantsMode MaterialVariants = EPCGExMaterialVariantsMode::None;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" ├─ Slot Index", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Single", EditConditionHides))
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variants", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Single", EditConditionHides))
	TArray<FPCGExMaterialOverrideSingleEntry> MaterialOverrideVariants;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Variants", EditCondition="!bIsSubCollection && MaterialVariants == EPCGExMaterialVariantsMode::Multi", EditConditionHides))
	TArray<FPCGExMaterialOverrideCollection> MaterialOverrideVariantsList;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides, DisplayAfter="Variations"))
	EPCGExEntryVariationMode DescriptorSource = EPCGExEntryVariationMode::Local;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Skinned Mesh Settings", EditCondition="!bIsSubCollection && DescriptorSource == EPCGExEntryVariationMode::Local", EditConditionHides, DisplayAfter="DescriptorSource"))
	FPCGSoftSkinnedMeshComponentDescriptor Descriptor;

	// Asset & Material Handling

	virtual void GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	void GetMaterialPaths(int32 PickIndex, TSet<FSoftObjectPath>& OutPaths) const;
	void ApplyMaterials(int32 PickIndex, FPCGSoftSkinnedMeshComponentDescriptor& InDescriptor) const;

	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;

	void InitPCGSoftSkinnedDescriptor(const UPCGExSkinnedMeshCollection* ParentCollection, FPCGSoftSkinnedMeshComponentDescriptor& TargetDescriptor) const;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
#endif

	virtual void BuildMicroCache() override;

	// Typed MicroCache Access
	PCGExSkinnedMeshCollection::FMicroCache* GetSkinnedMeshMicroCache() const
	{
		return static_cast<PCGExSkinnedMeshCollection::FMicroCache*>(MicroCache.Get());
	}
};

/**
 * Concrete collection for skinned meshes (UE 5.8+ SkinnedMeshComponent / SkinnedAsset).
 * Mirrors UPCGExMeshCollection but holds a single global descriptor.
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Skinned Mesh", meta=(ToolTip = "A weighted collection of skinned meshes with optional material overrides."))
class PCGEXCOLLECTIONS_API UPCGExSkinnedMeshCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExSkinnedMeshCollectionEntry)
	friend struct FPCGExSkinnedMeshCollectionEntry;

public:
	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::SkinnedMesh;
	}

	// Skinned-Mesh-Specific Properties

	UPROPERTY(EditAnywhere, Category = Settings)
	EPCGExGlobalVariationRule GlobalDescriptorMode = EPCGExGlobalVariationRule::PerEntry;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Global Skinned Mesh Settings"))
	FPCGSoftSkinnedMeshComponentDescriptor GlobalDescriptor;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExSkinnedMeshCollectionEntry> Entries;


#if WITH_EDITOR
	// Editor Functions

	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;

	UFUNCTION()
	void EDITOR_DisableCollisions();

	UFUNCTION()
	void EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode DescriptorSource);
#endif
};
