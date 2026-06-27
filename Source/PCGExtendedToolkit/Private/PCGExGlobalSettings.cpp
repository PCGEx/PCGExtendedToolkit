// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExGlobalSettings.h"

#include "CoreMinimal.h"

#include "PCGExCoreSettingsCache.h"

void UPCGExGlobalSettings::PostLoad()
{
	Super::PostLoad();
	UpdateSettingsCaches();
}

#if WITH_EDITOR
void UPCGExGlobalSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateSettingsCaches();
}
#endif

void UPCGExGlobalSettings::UpdateSettingsCaches() const
{
#define PCGEX_PUSH_SETTING(_MODULE, _SETTING) PCGEX_SETTINGS_INST(_MODULE)._SETTING = _SETTING;

#if WITH_EDITOR
#define PCGEX_PUSH_COLOR(_COLOR) PCGEX_CORE_SETTINGS.ColorsMap.Add(FName(#_COLOR), Color##_COLOR);
#else
#define PCGEX_PUSH_COLOR(_COLOR)
#endif

	// Push values to module settings cache

#pragma region Core

	PCGEX_PUSH_SETTING(Core, WorldUp)
	PCGEX_PUSH_SETTING(Core, WorldForward)

	PCGEX_PUSH_SETTING(Core, bDefaultCacheNodeOutput)
	PCGEX_PUSH_SETTING(Core, bDefaultScopedAttributeGet)
	PCGEX_PUSH_SETTING(Core, bBulkInitData)
	PCGEX_PUSH_SETTING(Core, bCacheLoadedResources)
	PCGEX_PUSH_SETTING(Core, bUseDelaunator)
	PCGEX_PUSH_SETTING(Core, bAssertOnEmptyThread)
	PCGEX_PUSH_SETTING(Core, bRuntimeAlwaysOffThread)

	PCGEX_PUSH_SETTING(Core, bUseNativeColorsIfPossible)
	PCGEX_PUSH_SETTING(Core, bToneDownOptionalPins)

	PCGEX_PUSH_SETTING(Core, bCacheClusters)
	PCGEX_PUSH_SETTING(Core, bDefaultScopedIndexLookupBuild)
	PCGEX_PUSH_SETTING(Core, bDefaultBuildAndCacheClusters)

	PCGEX_PUSH_SETTING(Core, SmallPointsSize)
	PCGEX_PUSH_SETTING(Core, SmallClusterSize)
	PCGEX_PUSH_SETTING(Core, PointsDefaultBatchChunkSize)
	PCGEX_PUSH_SETTING(Core, ClusterDefaultBatchChunkSize)

#if WITH_EDITOR

	// Push colors
	PCGEX_PUSH_COLOR(Constant)
	PCGEX_PUSH_COLOR(Debug)
	PCGEX_PUSH_COLOR(Misc)
	PCGEX_PUSH_COLOR(MiscWrite)
	PCGEX_PUSH_COLOR(MiscAdd)
	PCGEX_PUSH_COLOR(MiscRemove)
	PCGEX_PUSH_COLOR(Sampling)
	PCGEX_PUSH_COLOR(ClusterGenerator)
	PCGEX_PUSH_COLOR(ClusterOp)
	PCGEX_PUSH_COLOR(Pathfinding)
	PCGEX_PUSH_COLOR(Path)
	PCGEX_PUSH_COLOR(FilterHub)
	PCGEX_PUSH_COLOR(Transform)
	PCGEX_PUSH_COLOR(Action)
	PCGEX_PUSH_COLOR(BlendOp)
	PCGEX_PUSH_COLOR(MatchRule)
	PCGEX_PUSH_COLOR(Filter)
	PCGEX_PUSH_COLOR(FilterPoint)
	PCGEX_PUSH_COLOR(FilterCollection)
	PCGEX_PUSH_COLOR(FilterCluster)
	PCGEX_PUSH_COLOR(FilterVtx)
	PCGEX_PUSH_COLOR(FilterEdge)
	PCGEX_PUSH_COLOR(VtxProperty)
	PCGEX_PUSH_COLOR(NeighborSampler)
	PCGEX_PUSH_COLOR(FillControl)
	PCGEX_PUSH_COLOR(Heuristics)
	PCGEX_PUSH_COLOR(HeuristicsFeedback)
	PCGEX_PUSH_COLOR(Probe)
	PCGEX_CORE_SETTINGS.ColorsMap.Add(FName("PointState"), ColorClusterState);
	PCGEX_PUSH_COLOR(ClusterState)
	PCGEX_PUSH_COLOR(Picker)
	PCGEX_PUSH_COLOR(TexParam)
	PCGEX_PUSH_COLOR(Shape)
	PCGEX_PUSH_COLOR(Tensor)
	PCGEX_PUSH_COLOR(SortRule)
	PCGEX_PUSH_COLOR(PartitionRule)
	PCGEX_PUSH_COLOR(Noise3D)
	PCGEX_PUSH_COLOR(Selector)

#endif

#pragma endregion

#pragma region Blending

	PCGEX_PUSH_SETTING(Blending, DefaultBooleanBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultFloatBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultDoubleBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultInteger32BlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultInteger64BlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultVector2BlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultVectorBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultVector4BlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultQuaternionBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultTransformBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultRotatorBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultStringBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultNameBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultSoftObjectPathBlendMode)
	PCGEX_PUSH_SETTING(Blending, DefaultSoftClassPathBlendMode)

#pragma endregion

#undef PCGEX_PUSH_SETTING
#undef PCGEX_PUSH_COLOR
}
