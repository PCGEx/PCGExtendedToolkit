// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTangentsInstancedFactory.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExFactoryData.h"

#include "PCGExTangentsFromSpline.generated.h"

struct FPCGSplineStruct;

/**
 * Transfers the aspect of reference splines onto a path: each point takes the derivative of its closest
 * reference spline at its closest key, scaled by the key span to its neighbours' closest keys (the Hermite
 * subdivision rule Split Spline applies when it inserts a control point).
 */
class FPCGExTangentsFromSpline : public FPCGExTangentsOperation
{
public:
	TConstArrayView<const FPCGSplineStruct*> Sources;
	bool bUseMaxDistance = false;
	FPCGExInputShorthandSelectorDoubleAbs MaxDistance;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade) override;

	virtual void ProcessFirstPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const override;
	virtual void ProcessLastPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const override;
	virtual void ProcessPoint(const UPCGBasePointData* InPointData, const int32 Index, const int32 NextIndex, const int32 PrevIndex, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const override;

protected:
	int32 NumSources = 0;

	// Resolved in PrepareForData, read-only in the Process* hot path: closest key of every point on every source
	// (row-major, NumPoints x NumSources) and the source each point snapped to (-1 when none is in range).
	TArray<float> Keys;
	TArray<int32> BestSource;

	FORCEINLINE float KeyOn(const int32 SourceIndex, const int32 PointIndex) const
	{
		return Keys[PointIndex * NumSources + SourceIndex];
	}

	double KeyDelta(const FPCGSplineStruct& InSpline, const float FromKey, const float ToKey, const FVector& FromLocation, const FVector& ToLocation) const;
	FVector Derivative(const FPCGSplineStruct& InSpline, float Key, const bool bArriveSide) const;
	void Resolve(const FPCGSplineStruct& InSpline, const float Key, double DeltaArrive, double DeltaLeave, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const;
};

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName = "From Spline", PCGExNodeLibraryDoc="paths/common-settings/tangents/from-spline"))
class UPCGExFromSplineTangents : public UPCGExTangentsInstancedFactory
{
	GENERATED_BODY()

public:
	/** Ignore reference splines farther than Max Distance; points with no reference in range fall back to neighbor-based tangents. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Tangents)
	bool bUseMaxDistance = false;

	/** Max distance from a point to its closest reference spline. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Tangents, meta=(PCG_Overridable, EditCondition="bUseMaxDistance"))
	FPCGExInputShorthandSelectorDoubleAbs MaxDistance = FPCGExInputShorthandSelectorDoubleAbs(FName("MaxDistance"), 500.0);

	virtual bool WantsTangentSources() const override
	{
		return true;
	}

	virtual void InitializeInContext(FPCGExContext* InContext, FName InOverridesPinLabel) override;
	virtual void Cleanup() override;
	virtual TSharedPtr<FPCGExTangentsOperation> CreateOperation() const override;

protected:
	// Context-derived in InitializeInContext (borrowed from input data that outlives every operation), so
	// deliberately not part of CopySettingsFrom: a copy that skips InitializeInContext has no sources.
	TArray<const FPCGSplineStruct*> Sources;
};
