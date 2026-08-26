// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExCollisionDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExRaycastFilter.generated.h"

class UPrimitiveComponent;

namespace PCGExRaycastFilter
{
	const FName SourceActorReferencesLabel = FName("Actor References");
	const FName SourcePrimitivesLabel = FName("Primitives");
}

UENUM()
enum class EPCGExRaycastTestMode : uint8
{
	AnyHit          = 0 UMETA(DisplayName = "Any Hit", Tooltip="Pass if there is any hit"),
	CompareDistance = 1 UMETA(DisplayName = "Compare Distance", Tooltip="Compare hit distance against a threshold"),
};

UENUM()
enum class EPCGExRaycastOriginMode : uint8
{
	PointPosition  = 0 UMETA(DisplayName = "Point Position", Tooltip="Use the point's position directly"),
	OffsetWorld    = 1 UMETA(DisplayName = "Offset (World)", Tooltip="Point position + offset in world space"),
	OffsetRelative = 2 UMETA(DisplayName = "Offset (Relative)", Tooltip="Point position + offset transformed by point rotation/scale"),
	WorldPosition  = 3 UMETA(DisplayName = "World Position", Tooltip="Use offset value as absolute world position"),
};

USTRUCT(BlueprintType)
struct FPCGExRaycastFilterConfig
{
	GENERATED_BODY()

	FPCGExRaycastFilterConfig()
	{
	}

	/** Surface source - whether to trace against any surface or only specific actors */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExSurfaceSource SurfaceSource = EPCGExSurfaceSource::All;

	/** Name of the attribute that contains a path to an actor in the level, usually from a GetActorData PCG Node in point mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SurfaceSource == EPCGExSurfaceSource::ActorReferences", EditConditionHides))
	FName ActorReference = FName("ActorReference");


	/** Test mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExRaycastTestMode TestMode = EPCGExRaycastTestMode::AnyHit;

	/** Comparison operator for distance comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="TestMode == EPCGExRaycastTestMode::CompareDistance", EditConditionHides))
	EPCGExComparison Comparison = EPCGExComparison::StrictlySmaller;

	/** Distance threshold for comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TestMode == EPCGExRaycastTestMode::CompareDistance", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs DistanceThreshold = FPCGExInputShorthandSelectorDoubleAbs(FName("DistanceThreshold"), 500.0);

	/** Tolerance for nearly equal/not equal comparisons */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TestMode == EPCGExRaycastTestMode::CompareDistance && (Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual)", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** What to return when there is no hit in CompareDistance mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="TestMode == EPCGExRaycastTestMode::CompareDistance", EditConditionHides))
	EPCGExFilterFallback NoHitFallback = EPCGExFilterFallback::Fail;

	/** Collision settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExCollisionDetails CollisionSettings;

	/** How to determine the ray origin */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Ray", meta=(PCG_NotOverridable))
	EPCGExRaycastOriginMode OriginMode = EPCGExRaycastOriginMode::PointPosition;

	/** Origin offset or world position depending on mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Ray", meta=(PCG_Overridable, EditCondition="OriginMode != EPCGExRaycastOriginMode::PointPosition", EditConditionHides))
	FPCGExInputShorthandSelectorVector Origin = FPCGExInputShorthandSelectorVector(FName("OriginOffset"), FVector::ZeroVector);

	/** Trace direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Ray", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDirection Direction = FPCGExInputShorthandSelectorDirection(FString("$Rotation.Down"), FVector::DownVector);

	/** Transform the direction using the point's transform */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Ray", meta=(PCG_NotOverridable))
	bool bTransformDirection = false;

	/** Maximum trace distance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Ray", meta=(PCG_Overridable, ClampMin=0))
	FPCGExInputShorthandSelectorDoubleAbs MaxDistance = FPCGExInputShorthandSelectorDoubleAbs(FName("MaxDistance"), 1000.0);

	/** Whether the result of the filter should be inverted. Note: Inversion is applied AFTER the test, not to fallback values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bInvert = false;

	void Sanitize()
	{
	}
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExRaycastFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExRaycastFilterConfig Config;

	bool bUseInclude = false;
	TMap<AActor*, int32> IncludedActors;
	TSet<UPrimitiveComponent*> IncludedPrimitives;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual bool WantsPreparation(FPCGExContext* InContext) override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

namespace PCGExPointFilter
{
	class FRaycastFilter final : public ISimpleFilter
	{
	public:
		explicit FRaycastFilter(const TObjectPtr<const UPCGExRaycastFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExRaycastFilterFactory> TypedFilterFactory;

		FPCGExCollisionDetails CollisionSettings;

		TSharedPtr<PCGExDetails::TSettingValue<double>> SphereRadiusGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> BoxHalfExtentsGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> OriginGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> DirectionGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> MaxDistanceGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> DistanceThresholdGetter;

		TConstPCGValueRange<FTransform> InTransforms;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FRaycastFilter() override
		{
		}

	protected:
		bool DoTrace(const FVector& Start, const FVector& End, const FQuat& Orientation, const int32 Index, FHitResult& OutHit) const;
		bool DoTraceMulti(const FVector& Start, const FVector& End, const FQuat& Orientation, const int32 Index, FHitResult& OutHit) const;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-raycast"))
class UPCGExRaycastFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(RaycastFilterFactory, "Filter : Raycast", "Filters points based on raycast results against surfaces.")
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExRaycastFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return Config.SurfaceSource != EPCGExSurfaceSource::All;
	}
#endif
};
