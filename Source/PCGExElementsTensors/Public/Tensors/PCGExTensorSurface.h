// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"
#include "Details/PCGExCollisionDetails.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExTensorSurface.generated.h"

class UPCGSurfaceData;
class UPrimitiveComponent;
class AActor;
class UWorld;

UENUM(BlueprintType)
enum class EPCGExSurfaceTensorMode : uint8
{
	AlongSurface    = 0 UMETA(DisplayName = "Along Surface", ToolTip = "Project reference direction onto surface tangent plane"),
	TowardSurface   = 1 UMETA(DisplayName = "Toward Surface", ToolTip = "Direct toward nearest surface point"),
	AwayFromSurface = 2 UMETA(DisplayName = "Away From Surface", ToolTip = "Direct away from nearest surface point"),
	SurfaceNormal   = 3 UMETA(DisplayName = "Surface Normal", ToolTip = "Use the surface normal directly at the nearest point"),
	Orbit           = 4 UMETA(DisplayName = "Orbit", ToolTip = "Circular motion around the surface (cross product of normal and reference)"),
};

USTRUCT(BlueprintType)
struct FPCGExTensorSurfaceConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorSurfaceConfig()
		: FPCGExTensorConfigBase(false, true)
	{
	}

	/** How to compute the tensor direction from the surface */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSurfaceTensorMode Mode = EPCGExSurfaceTensorMode::AlongSurface;

	/** Maximum search distance for surface detection */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = 0.001))
	double MaxDistance = 1000;

	/** Reference axis used for AlongSurface and Orbit modes - this axis from the probe transform will be projected/used */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Mode == EPCGExSurfaceTensorMode::AlongSurface || Mode == EPCGExSurfaceTensorMode::Orbit", EditConditionHides))
	EPCGExAxis ReferenceAxis = EPCGExAxis::Forward;

	/** When enabled, use surface normal for AwayFromSurface instead of direction from nearest point */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Mode == EPCGExSurfaceTensorMode::AwayFromSurface", EditConditionHides))
	bool bUseNormalForAway = true;

	/** Whether to use world collision queries to find surfaces */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sources", meta = (PCG_NotOverridable))
	bool bUseWorldCollision = false;

	/** Collision settings for world collision queries */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sources", meta = (PCG_Overridable, DisplayName = " └─ Collision Settings", EditCondition = "bUseWorldCollision", EditConditionHides))
	FPCGExCollisionDetails CollisionSettings;

	/** Name of the attribute containing actor reference paths (when using Actor References input) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sources", meta = (PCG_Overridable))
	FName ActorReferenceAttribute = FName("ActorReference");

	/** If enabled, the tensor returns zero when no surface is found instead of failing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bReturnZeroOnMiss = false;
};

/** Surface hit result */
struct FPCGExSurfaceHit
{
	FVector Location = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	double Distance = TNumericLimits<double>::Max();

	bool IsValid() const
	{
		return Distance < TNumericLimits<double>::Max();
	}

	void UpdateIfCloser(const FVector& InLocation, const FVector& InNormal, double InDistance)
	{
		if (InDistance < Distance)
		{
			Location = InLocation;
			Normal = InNormal;
			Distance = InDistance;
		}
	}
};

/**
 * Surface tensor operation - samples surfaces to compute tensor directions
 */
class FPCGExTensorSurface : public PCGExTensorOperation
{
public:
	FPCGExTensorSurfaceConfig Config;

	FPCGProjectionParams ProjectionParams;

	// Cached world pointer
	TWeakObjectPtr<UWorld> World;

	// Cached primitives from actor references
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedPrimitives;

	// Cached surfaces for PCGSurface mode
	TArray<TWeakObjectPtr<const UPCGSurfaceData>> CachedSurfaces;

	// Collision params
	FCollisionQueryParams CollisionParams;

	// Flags for what sources are available
	bool bHasWorldCollision = false;
	bool bHasPrimitives = false;
	bool bHasSurfaces = false;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;
	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;

protected:
	/** Find the nearest surface across all available sources */
	bool FindNearestSurface(const FVector& Position, FPCGExSurfaceHit& OutHit) const;

	/** Check world collision for nearest surface */
	void CheckWorldCollision(const FVector& Position, FPCGExSurfaceHit& OutHit) const;

	/** Check cached primitives for nearest surface */
	void CheckPrimitives(const FVector& Position, FPCGExSurfaceHit& OutHit) const;

	/** Check PCG surfaces for nearest surface */
	void CheckPCGSurfaces(const FVector& Position, FPCGExSurfaceHit& OutHit) const;

	/** Compute the tensor direction based on mode and surface data */
	FVector ComputeDirection(const FTransform& InProbe, const FPCGExSurfaceHit& Hit) const;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExTensorSurfaceFactory : public UPCGExTensorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTensorSurfaceConfig Config;

	// Cached world
	TWeakObjectPtr<UWorld> CachedWorld;

	// Cached primitives from actor references
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedPrimitives;

	// Cached PCG surfaces
	TArray<TWeakObjectPtr<const UPCGSurfaceData>> CachedSurfaces;

	// Source availability flags
	bool bHasWorldCollision = false;
	bool bHasPrimitives = false;
	bool bHasSurfaces = false;

	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;

protected:
	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult InitInternalData(FPCGExContext* InContext) override;

	/** Initialize actor references from input data */
	bool InitActorReferences(FPCGExContext* InContext);

	/** Initialize PCG surface data from input */
	bool InitPCGSurfaces(FPCGExContext* InContext);
};

UCLASS(Hidden, MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Tensors|Params", meta = (PCGExNodeLibraryDoc = "tensors/tensors/tensor-surface"))
class UPCGExCreateTensorSurfaceSettings : public UPCGExTensorFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(TensorSurface, "Tensor : Surface", "A tensor that samples nearby surfaces to compute direction fields. Uses all connected surface sources additively.")
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorSurfaceConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
