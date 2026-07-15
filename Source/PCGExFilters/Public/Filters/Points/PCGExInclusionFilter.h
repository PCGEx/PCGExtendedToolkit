// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExPolyPathFilterFactory.h"
#include "Data/PCGExTaggedData.h"

#include "PCGExInclusionFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExInclusionFilterConfig
{
	GENERATED_BODY()

	FPCGExInclusionFilterConfig()
	{
	}

	/** Projection settings (used for inclusion checks). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Sample inputs.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/** Type of inclusion check to perform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSplineCheckType CheckType = EPCGExSplineCheckType::IsInside;

	/** How the tested point's bounds factor into the check. Center (default) tests the point location only and is
	 *  fastest. Sphere/Box widen the on-boundary band by the bound's reach so the check applies to the whole bound,
	 *  but the meaning depends on the check: "Is Inside" requires the *entire* bound inside, "Is Outside" entirely
	 *  outside, while the On family ("Is On", "...Or On", "...And On") treat the bound as On when it touches/crosses
	 *  a boundary. More expensive (forces the distance path). The hidden "None" value falls back to Center. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExDistance BoundsCheck = EPCGExDistance::Center;

	/** If a point is both inside and outside a spline (if there are multiple ones), decide what value to favor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSplineFilterPick Pick = EPCGExSplineFilterPick::All;

	/** Tolerance value used to determine whether a point is considered on the spline or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double Tolerance = 1;

	/** Scale the tolerance with spline' "thickness" (Scale' length)  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSplineScalesTolerance = false;

	/** If non-zero, will apply an offset (inset) to the data used for inclusion testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double InclusionOffset = 0;

	/** Require a minimum number of inclusions to pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMinInclusionCount = false;

	/** Minimum number of shapes the point must be inside. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMinInclusionCount"))
	int32 MinInclusionCount = 2;

	/** Limit the maximum number of inclusions to pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMaxInclusionCount = false;

	/** Maximum number of shapes the point can be inside. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMaxInclusionCount"))
	int32 MaxInclusionCount = 10;

	/** If enabled, invert the result of the test */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** Expansion on Z axis for 3D tolerance. -1 means infinite height. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	double ExpandZAxis = -1;

	/** Lets you enforce a path winding for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::CounterClockwise;

	/** When projecting, defines the resolution of the polygon created from the spline. Lower values means higher fidelity, but slower execution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, ClampMin=1), AdvancedDisplay)
	double Fidelity = 50;

	/** If enabled, when used with a collection filter, will use collection bounds as a proxy point instead of per-point testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bCheckAgainstDataBounds = false;

	/** If enabled, a collection will never be tested against itself */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Data matching settings. When enabled, only paths whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExFilterMatchingDetails DataMatching;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExInclusionFilterFactory : public UPCGExPolyPathFilterFactory
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExInclusionFilterConfig Config;

	virtual bool SupportsCollectionEvaluation() const override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

protected:
	virtual FName GetInputLabel() const override;
	virtual void InitConfig_Internal() override;
};

namespace PCGExPointFilter
{
	class FInclusionFilter final : public ISimpleFilter
	{
	public:
		explicit FInclusionFilter(const TObjectPtr<const UPCGExInclusionFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
			Handler = TypedFilterFactory->CreateHandler();
			Handler->Init(TypedFilterFactory->Config.CheckType, TypedFilterFactory->Config.BoundsCheck);
			Handler->ToleranceScaleFactor = FVector(0, 1, 1);
		}

		const TObjectPtr<const UPCGExInclusionFilterFactory> TypedFilterFactory;
		TSharedPtr<PCGExPathInclusion::FHandler> Handler;

		bool bCheckAgainstDataBounds = false;

		// Cached input value ranges for the per-point hot path (Test(int32)) -- read directly, no FPoint virtual
		// dispatch. BoundsMin/Max are only consumed by the handler when BoundsCheck != Center.
		TConstPCGValueRange<FTransform> InTransforms;
		TConstPCGValueRange<FVector> InBoundsMin;
		TConstPCGValueRange<FVector> InBoundsMax;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FInclusionFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-inclusion-path-splines"))
class UPCGExInclusionFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(InclusionFilterFactory, "Filter : Inclusion (Path/Splines)", "Creates a filter definition that checks points inclusion against path-like data (paths, splines, polygons).", PCGEX_FACTORY_NAME_PRIORITY)
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExInclusionFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
