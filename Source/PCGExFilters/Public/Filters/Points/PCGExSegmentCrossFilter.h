// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExPolyPathFilterFactory.h"
#include "Data/PCGExTaggedData.h"
#include "Paths/PCGExPathIntersectionDetails.h"

#include "PCGExSegmentCrossFilter.generated.h"

namespace PCGExMatching
{
	class FDataMatcher;
}

UENUM()
enum class EPCGExSegmentCrossWinding : uint8
{
	ToNext = 0 UMETA(DisplayName = "To Next", ToolTip="Segment is current point to next point (canon)."),
	ToPrev = 1 UMETA(DisplayName = "To Prev", ToolTip="Segment is current point to previous point (inversed direction)."),
};

USTRUCT(BlueprintType)
struct FPCGExSegmentCrossFilterConfig
{
	GENERATED_BODY()

	FPCGExSegmentCrossFilterConfig()
	{
	}

	/** Sample inputs.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/** Tolerance value used to determine whether a point is considered on the spline or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExPathIntersectionDetails IntersectionSettings;

	/** Segment definition. Useful when flagging segments "backward" (e.g so the end point is flagged instead of the first point) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExSegmentCrossWinding Direction = EPCGExSegmentCrossWinding::ToNext;

	/** If enabled, invert the result of the test */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** When projecting, defines the resolution of the polygon created from the spline. Lower values means higher fidelity, but slower execution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, ClampMin=1), AdvancedDisplay)
	double Fidelity = 50;

	/** If enabled, a collection will never be tested against itself */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Data matching settings. When enabled, only paths whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFilterMatchingDetails DataMatching;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExSegmentCrossFilterFactory : public UPCGExPolyPathFilterFactory
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSegmentCrossFilterConfig Config;

	virtual bool SupportsProxyEvaluation() const override { return false; }

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

protected:
	virtual FName GetInputLabel() const override;
	virtual void InitConfig_Internal() override;
};

namespace PCGExPointFilter
{
	class FSegmentCrossFilter final : public ISimpleFilter
	{
	public:
		explicit FSegmentCrossFilter(const TObjectPtr<const UPCGExSegmentCrossFilterFactory>& InFactory)
			: ISimpleFilter(InFactory), TypedFilterFactory(InFactory)
		{
			Handler = TypedFilterFactory->CreateHandler();
			Handler->Init(EPCGExSplineCheckType::IsOn);
			Handler->ToleranceScaleFactor = FVector(0, 1, 1);
		}

		bool bClosedLoop = false;
		bool bMatchingFailed = false;
		int32 LastIndex = 0;

		const TObjectPtr<const UPCGExSegmentCrossFilterFactory> TypedFilterFactory;
		TSharedPtr<PCGExPathInclusion::FHandler> Handler;

		// Per-point matching -- see FDistanceFilter for full explanation.
		TSharedPtr<PCGExMatching::FDataMatcher> InverseMatcher;
		bool bNoMatchResult = false;

		TConstPCGValueRange<FTransform> InTransforms;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FSegmentCrossFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/math/filter-segment-cross"))
class UPCGExSegmentCrossFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(SegmentCrossFilterFactory, "Filter : Segment Cross", "Creates a filter definition that checks points SegmentCross against path-like data (paths, splines, polygons).", PCGEX_FACTORY_NAME_PRIORITY)
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSegmentCrossFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif
};
