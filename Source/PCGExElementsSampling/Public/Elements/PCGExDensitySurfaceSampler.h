// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExDensitySampling.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"

#include "PCGExDensitySurfaceSampler.generated.h"

namespace PCGExDensitySurfaceSampler
{
	inline const FName SurfaceLabel = TEXT("Surface");
	inline const FName BoundingShapeLabel = TEXT("Bounding Shape");
}

/**
 * Samples surfaces with a density read off the surface itself, such as a landscape layer: candidates are thinned
 * before they become points, so sparse areas cost little. Blue noise, jittered grid or Poisson disk layouts.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/density-surface-sampler"))
class UPCGExDensitySurfaceSamplerSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(DensitySurfaceSampler, "Density Surface Sampler", "Samples surfaces with a density read off the surface, such as a landscape layer. Blue noise, jittered grid or Poisson disk.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}
#endif

	virtual bool UseSeed() const override
	{
		return true;
	}

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Points per square meter where the density is 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0", PCG_Overridable))
	float MaxDensity = 0.1f;

	/** How candidates are laid out before the density thins them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExDensityDistribution Distribution = EPCGExDensityDistribution::BlueNoise;

	/** Which cells survive where the density is below 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = " ├─ Thinning", EditCondition = "Distribution == EPCGExDensityDistribution::JitteredGrid", EditConditionHides))
	EPCGExDensityGridThinning GridThinning = EPCGExDensityGridThinning::BlueNoise;

	/** Where points sit in their cell: 0 at the center, 1 anywhere the point extents allow. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0", ClampMax = "1", PCG_Overridable, DisplayName = " └─ Looseness", EditCondition = "Distribution == EPCGExDensityDistribution::JitteredGrid", EditConditionHides))
	float Looseness = 1.0f;

	/** Point radius relative to the spacing at max density. Near 0.83 the kept density stays close to Max Density x density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0.01", PCG_Overridable, DisplayName = " └─ Radius Factor", EditCondition = "Distribution == EPCGExDensityDistribution::PoissonDisk", EditConditionHides))
	double PoissonRadiusFactor = 0.83;

	/** Toggles Strict Spacing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bStrictSpacing = false;

	/** When enabled, drops points closer to a higher-priority point than this factor x the spacing expected at the local
	 *  density. Partition borders stay seamless. Ignored by Poisson Disk, which always spaces its points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0.01", PCG_Overridable, DisplayName = "Strict Spacing", EditCondition = "bStrictSpacing"))
	double SpacingFactor = 0.75;

	/** Without a Bounding Shape, sample the whole surface instead of the component bounds. Can create many points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bUnbounded = false;

	/** What drives the density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_Overridable))
	EPCGExDensityDriverSource DriverSource = EPCGExDensityDriverSource::Attribute;

	/** Attribute the surface projection writes, such as a landscape layer name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_Overridable, DisplayName = " └─ Attribute", EditCondition = "DriverSource == EPCGExDensityDriverSource::Attribute", EditConditionHides))
	FName DriverAttribute = NAME_None;

	/** Toggles Driver Curve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bUseDriverCurve = false;

	/** When enabled, remaps the driver (clamped to 0-1) to the density; otherwise the driver is the density. Ignored with a Constant driver. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_NotOverridable, EditCondition = "bUseDriverCurve"))
	FPCGExWeightCurve DriverCurve = FPCGExWeightCurve(0, 1);

	/** Toggles Modifiers. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bUseModifiers = false;

	/** When enabled, more attributes multiply or mask the density. They cost no extra projection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_NotOverridable, EditCondition = "bUseModifiers"))
	TArray<FPCGExDensityModifier> Modifiers;

	/** How the density is read off the surface. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_Overridable))
	EPCGExDensityEvaluation Evaluation = EPCGExDensityEvaluation::Exact;

	/** Lattice spacing, in cm. Aligned to a landscape, it rounds to whole landscape quads. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (ClampMin = "1", PCG_Overridable, DisplayName = " ├─ Map Spacing", EditCondition = "Evaluation == EPCGExDensityEvaluation::DensityMap", EditConditionHides))
	double DensityMapSpacing = 400;

	/** On a landscape, lattice nodes sit on its vertices. At one quad per step the map reproduces the layers exactly. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Density", meta = (PCG_Overridable, DisplayName = " └─ Align to Landscape", EditCondition = "Evaluation == EPCGExDensityEvaluation::DensityMap", EditConditionHides))
	bool bAlignToLandscapeVertices = true;

	/** Extents of the points to create (half their size). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable))
	FVector PointExtents = FVector(50.0);

	/** Hardness of the points' bounds, from 0 (soft falloff) to 1 (a box). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (ClampMin = "0", ClampMax = "1", PCG_Overridable))
	float PointSteepness = 1.0f;

	/** Copy the surface's attributes onto the points, such as every landscape layer. Off skips their per-point cost. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable))
	bool bOutputSurfaceAttributes = true;

	/** Write the local density (0-1) to $Density instead of 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable))
	bool bWriteLocalDensity = false;

	/** Toggles Rank. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteRank = false;

	/** When enabled, writes each point's rank (0-1) to this attribute. Keeping ranks below a value downstream thins the
	 *  points and keeps them evenly spread. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, DisplayName = "Rank", EditCondition = "bWriteRank"))
	FName RankAttributeName = FName("Rank");
};

struct FPCGExDensitySurfaceSamplerContext final : FPCGExContext
{
	friend class FPCGExDensitySurfaceSamplerElement;

	TSharedPtr<PCGExDensitySampling::FConfig> Config;
	TArray<TSharedPtr<PCGExDensitySampling::FSurfaceJob>> Jobs;
};

class FPCGExDensitySurfaceSamplerElement final : public IPCGExElement
{
public:
	/** Depends on the component bounds when it falls back to them. */
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;

	/** Reads external data (landscapes): a full CRC lets unchanged results stop change propagation. */
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override
	{
		return true;
	}

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(DensitySurfaceSampler)

	/** Waits on the surfaces' spatial-query preparation through DynamicDependencies, which must be set synchronously. */
	virtual bool SupportsDetachedExecute() const override
	{
		return false;
	}

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
