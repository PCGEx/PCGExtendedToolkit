// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Elements/PCGActorSelector.h"

#include "PCGExCollisionDetails.generated.h"

struct FPCGExContext;
struct FHitResult;
class UWorld;
class AActor;

UENUM()
enum class EPCGExCollisionFilterType : uint8
{
	Channel    = 0 UMETA(DisplayName = "Channel", ToolTip="Channel"),
	ObjectType = 1 UMETA(DisplayName = "Object Type", ToolTip="Object Type"),
	Profile    = 2 UMETA(DisplayName = "Profile", ToolTip="Profile"),
};

UENUM()
enum class EPCGExTraceMode : uint8
{
	Line   = 0 UMETA(DisplayName = "Line", Tooltip="Simple line trace"),
	Sphere = 1 UMETA(DisplayName = "Sphere", Tooltip="Sphere sweep"),
	Box    = 2 UMETA(DisplayName = "Box", Tooltip="Box sweep"),
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/collision-details"))
struct PCGEXFOUNDATIONS_API FPCGExCollisionDetails
{
	GENERATED_BODY()

	FPCGExCollisionDetails()
	{
	}

	/** Trace mode - line, sphere sweep, or box sweep */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExTraceMode TraceMode = EPCGExTraceMode::Line;

	/** Sphere radius for sphere sweep traces */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TraceMode == EPCGExTraceMode::Sphere", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs SphereRadius = FPCGExInputShorthandSelectorDoubleAbs(FName("SphereRadius"), 50.0);

	/** Box half extents for box sweep traces */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TraceMode == EPCGExTraceMode::Box", EditConditionHides))
	FPCGExInputShorthandSelectorVector BoxHalfExtents = FPCGExInputShorthandSelectorVector(FName("BoxHalfExtents"), FVector(25.0));

	/** Trace against complex collision geometry instead of simple bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTraceComplex = false;

	/** Collision type to check against. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCollisionFilterType CollisionType = EPCGExCollisionFilterType::Channel;

	/** Collision channel to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::Channel", EditConditionHides, Bitmask, BitmaskEnum="/Script/Engine.ECollisionChannel"))
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldDynamic;

	/** Collision object type to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::ObjectType", EditConditionHides, Bitmask, BitmaskEnum="/Script/Engine.EObjectTypeQuery"))
	int32 CollisionObjectType = ObjectTypeQuery1;

	/** Collision profile to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::Profile", EditConditionHides))
	FName CollisionProfileName = NAME_None;

	/** Ignore the actor that owns the PCG component -- everything on that actor, not just its PCG content. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Ignore a procedural selection of actors */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bIgnoreActors = false;

	/** Actor selection criteria for actors to ignore in collision checks. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bIgnoreActors"))
	FPCGActorSelectorSettings IgnoredActorSelector;

	TArray<AActor*> IgnoredActors;
	UWorld* World = nullptr;

	void Init(FPCGExContext* InContext);
	void Update(FCollisionQueryParams& InCollisionParams) const;

	// Line traces
	bool Linecast(const FVector& From, const FVector& To, FHitResult& HitResult) const;
	bool Linecast(const FVector& From, const FVector& To) const;
	bool StrongLinecast(const FVector& From, const FVector& To) const;
	bool Linecast(const FVector& From, const FVector& To, bool bStrong) const;

	// Multi-line traces
	bool LinecastMulti(const FVector& From, const FVector& To, TArray<FHitResult>& OutHits) const;

	// Sphere sweeps
	bool SphereSweep(const FVector& From, const FVector& To, const double Radius, FHitResult& HitResult, const FQuat& Orientation = FQuat::Identity) const;
	bool SphereSweep(const FVector& From, const FVector& To, const double Radius, const FQuat& Orientation = FQuat::Identity) const;
	bool SphereSweepMulti(const FVector& From, const FVector& To, const double Radius, TArray<FHitResult>& OutHits, const FQuat& Orientation = FQuat::Identity) const;

	// Box sweeps
	bool BoxSweep(const FVector& From, const FVector& To, const FVector& HalfExtents, FHitResult& HitResult, const FQuat& Orientation = FQuat::Identity) const;
	bool BoxSweep(const FVector& From, const FVector& To, const FVector& HalfExtents, const FQuat& Orientation = FQuat::Identity) const;
	bool BoxSweepMulti(const FVector& From, const FVector& To, const FVector& HalfExtents, TArray<FHitResult>& OutHits, const FQuat& Orientation = FQuat::Identity) const;
};
