// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExProjectionDetails.h"
#include "Shapes/PCGExFootprintShape.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExSpatialBake.generated.h"

struct FPCGExContext;
struct FPCGTaggedData;
class UPCGData;

/**
 * Author-facing knobs for converting PCG-graph inputs into runtime spatial
 * domain entries. Embedded on consumer settings (one per consumer node);
 * passed by const-ref into PCGExSpatial::Bake::TryBake.
 *
 * Tag prefix: tagged data carrying "<TagPrefix>:<Name>" contribute to channel
 * <Name>. Untagged inputs fall back to FallbackChannel (not skipped).
 * Multiple tags on one data contribute to each named channel.
 *
 * Z band: ZMin / ZMax are read per-input via the shorthand (constant or
 * @Data-domain attribute). Used by spline -> Polygon bakes; point bakes
 * ignore them (point bounds are self-describing).
 */
USTRUCT(BlueprintType)
struct PCGEXSPATIALDOMAINS_API FPCGExExternalDomainBakeSettings
{
	GENERATED_BODY()

	FPCGExExternalDomainBakeSettings();

	/**
	 * Tag prefix that identifies the channel key on a tagged data.
	 * "SpatialChannel:Bounds" -> channel "Bounds". Inputs without any
	 * matching tag fall back to FallbackChannel.
	 */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FName TagPrefix;

	/**
	 * Channel used when a tagged data has no <TagPrefix>:* tag. The data
	 * is still baked -- it just lands on this channel. Default = "Default".
	 */
	UPROPERTY(EditAnywhere, Category = "Settings",
		meta = (GetOptions = "/Script/PCGExSpatialDomains.PCGExSpatialDomainsSettings.GetChannelKeyOptions"))
	FName FallbackChannel;

	/**
	 * Spline-sampling fidelity (squared-distance threshold) forwarded to
	 * FPCGSplineStruct::ConvertSplineToPolyLine. Smaller = denser outline.
	 */
	UPROPERTY(EditAnywhere, Category = "Settings|Spline", meta = (ClampMin = "1.0"))
	double SplineFidelity = 10.0;

	/**
	 * Projection frame for spline -> Polygon bakes. Normal mode reads the
	 * spline data's own projection (with FBestFitPlane fallback); BestFit
	 * computes the plane from sampled points. Mirrors FPolyPath's behavior.
	 */
	UPROPERTY(EditAnywhere, Category = "Settings|Spline", meta = (ShowOnlyInnerProperties))
	FPCGExGeo2DProjectionDetails Projection;

	/** Z band lower extent for spline -> Polygon bakes. Constant or @Data attribute. */
	UPROPERTY(EditAnywhere, Category = "Settings|Z Band")
	FPCGExInputShorthandNameDouble ZMin;

	/** Z band upper extent for spline -> Polygon bakes. Constant or @Data attribute. */
	UPROPERTY(EditAnywhere, Category = "Settings|Z Band")
	FPCGExInputShorthandNameDouble ZMax;
};

namespace PCGExSpatial::Bake
{
	/**
	 * One baked contribution: one shape destined for one channel. One
	 * tagged-data input can produce many (a point cloud emits N shapes;
	 * multi-channel-tagged data emits across channels).
	 */
	struct PCGEXSPATIALDOMAINS_API FBakedEntry
	{
		FName ChannelKey = NAME_None;
		FInstancedStruct Shape;
	};

	/**
	 * Resolve a tagged data's channel key. Looks for a tag of the form
	 * "<TagPrefix>:<Name>" (parsed via PCGExData::FTags); returns <Name>
	 * if found, FallbackChannel otherwise. Single-key semantics: one input
	 * routes to exactly one channel (FTags' prefix:value map is unique-key).
	 *
	 * Public so callers wanting routing without baking can use it.
	 */
	PCGEXSPATIALDOMAINS_API FName ResolveChannelKey(
		const FPCGTaggedData& InData,
		const FPCGExExternalDomainBakeSettings& InSettings);

	/**
	 * Bake one tagged data into shape entries. Per-input-type dispatch:
	 *   UPCGSplineData    -> FPCGExFootprintShape_Polygon (one per spline)
	 *   UPCGBasePointData -> FPCGExFootprintShape_OBB (one per point)
	 *   UPCGVolumeData    -> FPCGExFootprintShape_Volume (its AVolume brush)
	 *   UPCGPrimitiveData -> FPCGExFootprintShape_Primitive (its component)
	 *
	 * The UObject-backed kinds fall back to _OBB over the input's world bounds
	 * when the backing object cannot answer an overlap query -- an approximate box
	 * beats a region that silently reports empty. Warns, except for the no-actor
	 * case (bounds-only volume data, where the box is exact).
	 *
	 * Returns true on any successful contribution; false on unknown input
	 * type or total failure (caller decides whether to warn / skip).
	 * OutEntries is appended to, never cleared -- callers can accumulate
	 * across multiple inputs.
	 */
	PCGEXSPATIALDOMAINS_API bool TryBake(
		const FPCGTaggedData& InData,
		FPCGExContext* InContext,
		const FPCGExExternalDomainBakeSettings& InSettings,
		TArray<FBakedEntry>& OutEntries);
}
