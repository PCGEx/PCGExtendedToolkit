// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Math/InterpCurve.h"
#include "Helpers/PCGExSplineSampleHandler.h"

#include "PCGExSplineSampleBuiltinHandlers.generated.h"

class USplineComponent;

namespace PCGExSplineSampling
{
	/** Where one extent of a spline-component source is read from, per station. */
	enum class EExtentSource : uint8
	{
		Constant, // Value
		ScaleX,   // Value x |point scale X|
		ScaleY,   // Value x |point scale Y|
		ScaleZ,   // Value x |point scale Z|
		Curve,    // Value x Curve(input key)
	};

	struct PCGEXELEMENTSPATHS_API FSplineExtent
	{
		EExtentSource Source = EExtentSource::Constant;
		double Value = 0;

		/** Keyed by spline input key (EExtentSource::Curve). */
		FInterpCurveFloat Curve;

		/** Also scale by the component transform's stretch along the extent's axis (the station's right or up). */
		bool bComponentScale = false;
	};

	/** Lateral applies to both sides. */
	struct PCGEXELEMENTSPATHS_API FSplineExtents
	{
		FSplineExtent Lateral;
		FSplineExtent Above;
		FSplineExtent Below;
	};

	/** Game thread. Snapshots a spline component into a source measuring its footprint with Extents.
	 *  The snapshot is read-only whatever the spline backend, so any number of workers may evaluate it. */
	PCGEXELEMENTSPATHS_API TSharedRef<FSource> MakeSplineComponentSource(const USplineComponent* SplineComponent, const FSplineExtents& Extents);

	/** One chain edge, walked from its start node to its end node, or backward. */
	struct PCGEXELEMENTSPATHS_API FChainLink
	{
		int32 Edge = INDEX_NONE;
		bool bReversed = false;
	};

	struct PCGEXELEMENTSPATHS_API FChain
	{
		TArray<FChainLink> Links;
		bool bClosedLoop = false;
	};

	/** Splits an undirected graph into chains with the rules of Break Clusters to Paths. Edges are (start, end) node
	 *  pairs over [0, NumNodes); a chain runs the way most of its EdgeWeights point. */
	PCGEXELEMENTSPATHS_API void BuildChains(int32 NumNodes, TConstArrayView<TPair<int32, int32>> Edges, TConstArrayView<double> EdgeWeights, TArray<FChain>& OutChains);

	PCGEXELEMENTSPATHS_API void RegisterBuiltinHandlers();
	PCGEXELEMENTSPATHS_API void UnregisterBuiltinHandlers();
}

/** Generic fallback for any USplineComponent: node fallbacks scaled by point scale and component stretch. */
UCLASS()
class PCGEXELEMENTSPATHS_API UPCGExBasicSplineSampleHandler : public UPCGExSplineSampleHandler
{
	GENERATED_BODY()

public:
	virtual FTopLevelAssetPath GetComponentClassPath() const override;
	virtual void CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const override;
};

/** UWaterSplineComponent, matched by path and read through reflection: no Water module dependency.
 *  Uses what the owning body authors (RiverWidth, Depth) and the node fallbacks for the rest. */
UCLASS()
class PCGEXELEMENTSPATHS_API UPCGExWaterSplineSampleHandler : public UPCGExSplineSampleHandler
{
	GENERATED_BODY()

public:
	virtual FTopLevelAssetPath GetComponentClassPath() const override;
	virtual void CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const override;
};

/** ULandscapeSplinesComponent: one source per chain of its control-point graph (BuildChains). */
UCLASS()
class PCGEXELEMENTSPATHS_API UPCGExLandscapeSplineSampleHandler : public UPCGExSplineSampleHandler
{
	GENERATED_BODY()

public:
	virtual FTopLevelAssetPath GetComponentClassPath() const override;
	virtual void CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const override;
};
