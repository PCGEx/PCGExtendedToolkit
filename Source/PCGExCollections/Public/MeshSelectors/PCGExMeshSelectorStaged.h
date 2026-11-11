// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "PCGExPropertyFloatPacker.h"
#include "PCGExMeshSelectorStaged.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), DisplayName="[PCGEx] Staging Data", meta=(PCGExNodeLibraryDoc="staging/utilities/staging-data-mesh-selector"))
class UPCGExMeshSelectorStaged : public UPCGMeshSelectorBase
{
	GENERATED_BODY()

public:
	virtual bool SelectMeshInstances(FPCGStaticMeshSpawnerContext& Context, const UPCGStaticMeshSpawnerSettings* Settings, const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& OutMeshInstances, UPCGBasePointData* OutPointData) const override;
	virtual void Serialize(FArchive& Ar) override;

	/** Staging layer this selector reads staged picks from (and strips from output points). None = default layer (PCGEx/CollectionEntry); otherwise the layer name is appended (PCGEx/CollectionEntry/<layer>). Other layers' attributes pass through untouched. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, AdvancedDisplay)
	FName StagingLayer = NAME_None;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bApplyMaterialOverrides = true;

	/** Apply to each point the material swap table of the last Staging : Swap it passed (that Map on the Overrides pin), to
	 *  override slots and to the mesh's own materials. Independent of Apply Material Overrides. */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bApplyMaterialSwaps = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForceDisableCollisions = false;

	/** Spawn every instance with the entry's Reverse Culling as-is, instead of flipping it for points whose transform
	 *  is mirrored (negative scale). */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForceEntryReverseCulling = false;

	/** Add the input data's tags to every spawned component, after the entry, category and collection tags.
	 *  Inputs with different tags never share an ISM component. Off on selectors saved before it existed. */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bForwardInputDataTags = true;

	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(InlineEditConditionToggle))
	bool bUseTemplateDescriptor = true;

	/** Ignores per-entry collection descriptors, pushing only mesh, materials & tags. On by default for legacy reasons -- turn it off if your collections set per-entry details. */
	UPROPERTY(EditAnywhere, Category = MeshSelector, meta=(DisplayName="Descriptor Override", EditCondition="bUseTemplateDescriptor"))
	FPCGSoftISMComponentDescriptor TemplateDescriptor;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bUseTimeSlicing = false;

	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bOutputPoints = true;

	/** When enabled, silently skips input data that is missing the staging hash attribute instead of logging an error. */
	UPROPERTY(EditAnywhere, Category = MeshSelector)
	bool bQuietMissingStagingDataWarning = false;

	/** Per-entry collection properties packed into the spawned component's Custom Primitive Data. */
	// No ShowOnlyInnerProperties: it re-categorizes promoted members under THEIR own Category,
	// scattering them into "Settings" instead of showing them here.
	UPROPERTY(EditAnywhere, Category = "MeshSelector|Custom Primitive Data", meta=(DisplayName="Layout"))
	FPCGExPackedFloatLayout CustomPrimitiveDataLayout;

#if WITH_EDITORONLY_DATA
	/** Logs the resolved layout and each entry's floats. PCGNoHash: hashing diagnostics would rebuild every ISM on toggle. */
	UPROPERTY(EditAnywhere, Category = "MeshSelector|Custom Primitive Data", meta=(DisplayName="Debug Layout", PCGNoHash))
	bool bDebugCustomPrimitiveData = false;
#endif

#if WITH_EDITOR
	/** Drives Layout > Populate From. A CallInEditor button can't: FObjectDetails renders those from
	 *  a class layout that only runs for a details view's root objects, never a nested sub-object. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

namespace PCGExCollections
{
	class FMaterialSwapLibrary;
}

struct FPCGExAssetCollectionEntry;
class UPCGExAssetCollection;
class UMaterialInterface;

namespace PCGExMeshSelectorStaged
{
	/** Each point's material swap table on InLayer (0 = none); left empty when the points carry no stamp. */
	PCGEXCOLLECTIONS_API void ReadSwapStamps(const UPCGBasePointData* InPointData, FName InLayer, TArray<uint32>& OutTables);

	/** Data tags of the input InContext is selecting for, flattened like PCGExData::FTags (value tags included), minus
	 *  PCGEx's cluster pairing tags. */
	PCGEXCOLLECTIONS_API void GetInputDataTags(const FPCGStaticMeshSpawnerContext& InContext, TArray<FName>& OutTags);

	/**
	 * Moves the points of InOutList whose key differs from the kept one into copies of the list, one per key in first-seen
	 * order. The kept key is InPreferred when a point has it, else the first point's. Returns the kept key.
	 */
	template <typename KeyOfT>
	int32 SplitByKey(FPCGMeshInstanceList& InOutList, KeyOfT&& InKeyOf, const int32 InPreferred, TArray<TPair<int32, FPCGMeshInstanceList>>& OutSplits)
	{
		TArray<int32>& Indices = InOutList.InstancesIndices;
		if (Indices.IsEmpty())
		{
			return InPreferred;
		}

		TArray<int32> Keys;
		Keys.SetNumUninitialized(Indices.Num());
		bool bHasPreferred = false;
		for (int32 i = 0; i < Indices.Num(); i++)
		{
			Keys[i] = InKeyOf(Indices[i]);
			bHasPreferred |= Keys[i] == InPreferred;
		}

		const int32 Kept = bHasPreferred ? InPreferred : Keys[0];

		int32 WriteIndex = 0;
		for (int32 i = 0; i < Indices.Num(); i++)
		{
			const int32 PointIndex = Indices[i];
			const int32 Key = Keys[i];
			if (Key == Kept)
			{
				Indices[WriteIndex++] = PointIndex;
				continue;
			}

			TPair<int32, FPCGMeshInstanceList>* Split = OutSplits.FindByPredicate([Key](const TPair<int32, FPCGMeshInstanceList>& Candidate) { return Candidate.Key == Key; });
			if (!Split)
			{
				FPCGMeshInstanceList Copy = InOutList;
				Copy.InstancesIndices.Reset();
				Copy.Instances.Reset();
				Split = &OutSplits.Emplace_GetRef(Key, MoveTemp(Copy));
			}
			Split->Value.InstancesIndices.Add(PointIndex);
		}

		Indices.SetNum(WriteIndex, EAllowShrinking::No);
		return Kept;
	}

	struct FOutcomeSplit
	{
		int32 NumUnknown = 0;   // Points whose table the library doesn't hold: they keep the materials they had.
		bool bAnyTable = false; // Some point follows a table the library holds.
	};

	/**
	 * Gives the points of InOutList the descriptor their table (InPointTables, by point index; one the library doesn't hold
	 * counts as none) makes of InEntry. Points whose tables give the same descriptor share a list, since two equal
	 * descriptors from one input would share a component: InOutList keeps the first point's, the others go to OutLists.
	 */
	PCGEXCOLLECTIONS_API FOutcomeSplit SplitByOutcome(FPCGMeshInstanceList& InOutList, TConstArrayView<uint32> InPointTables, const PCGExCollections::FMaterialSwapLibrary& InLibrary, FName InLayer, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, TConstArrayView<TSoftObjectPtr<UMaterialInterface>> InMeshMaterials, TArray<FPCGMeshInstanceList>& OutLists);
}
