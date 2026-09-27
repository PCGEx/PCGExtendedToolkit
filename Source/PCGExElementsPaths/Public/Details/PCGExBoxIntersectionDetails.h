// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Data/PCGExPointElements.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExFuseDetails.h"
#include "Math/OBB/PCGExOBBIntersections.h"

#include "PCGExBoxIntersectionDetails.generated.h"

#define PCGEX_FOREACH_FIELD_INTERSECTION(MACRO)\
MACRO(IsIntersection, bool, false)\
MACRO(CutType, int32, CutTypeValueMapping[EPCGExCutType::Undefined])\
MACRO(Normal, FVector, FVector::ZeroVector)\
MACRO(BoundIndex, int32, -1)

struct FPCGContext;

namespace PCGExMath
{
	namespace OBB
	{
		struct FCut;
	}

	struct FCut;
}

namespace PCGExMatching
{
	class FTargetsHandler;
}

USTRUCT(BlueprintType)
struct PCGEXELEMENTSPATHS_API FPCGExBoxIntersectionDetails
{
	GENERATED_BODY()

	FPCGExBoxIntersectionDetails();

	/** Bounds type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** If enabled, flag newly created intersection points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsIntersection = true;

	/** Name of the attribute to write point intersection boolean to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="IsIntersection", PCG_Overridable, EditCondition="bWriteIsIntersection" ))
	FName IsIntersectionAttributeName = FName("IsIntersection");

	/** If enabled, mark non-intersecting points inside the volume with a boolean value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCutType = true;

	/** Name of the attribute to write point toward inside boolean to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="CutType", PCG_Overridable, EditCondition="bWriteCutType" ))
	FName CutTypeAttributeName = FName("CutType");

	/** Pick which value will be written for each cut type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings", EditFixedSize, meta=( ReadOnlyKeys, DisplayName=" └─ Mapping", EditCondition="bWriteCutType", HideEditConditionToggle))
	TMap<EPCGExCutType, int32> CutTypeValueMapping;

	/** If enabled, mark non-intersecting points inside the volume with a boolean value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNormal = false;

	/** Name of the attribute to write point intersection normal to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Normal", PCG_Overridable, EditCondition="bWriteNormal" ))
	FName NormalAttributeName = FName("Normal");

	/** Write the index of the intersecting bounds to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundIndex = false;

	/** Attribute name for the intersecting bounds index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="BoundIndex", PCG_Overridable, EditCondition="bWriteBoundIndex" ))
	FName BoundIndexAttributeName = FName("BoundIndex");

	/** Forward attributes from intersecting bounds to intersection points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding", meta=(PCG_Overridable))
	FPCGExForwardDetails IntersectionForwarding;

	bool Validate(const FPCGContext* InContext) const;

#define PCGEX_LOCAL_DETAIL_DECL(_NAME, _TYPE, _DEFAULT) TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Writer;
	PCGEX_FOREACH_FIELD_INTERSECTION(PCGEX_LOCAL_DETAIL_DECL)
#undef PCGEX_LOCAL_DETAIL_DECL

	// The facade's output must already be allocated to its final point count: forwarding is sized from it.
	void Init(const TSharedPtr<PCGExData::FFacade>& PointDataFacade, const TSharedPtr<PCGExMatching::FTargetsHandler>& TargetsHandler);

	bool WillWriteAny() const;

	void Mark(const TSharedRef<PCGExData::FPointIO>& InPointIO) const;

	// Writes Cuts[j] onto output point StartIndex + j; the run must be contiguous and owned by the caller.
	void SetIntersections(const int32 StartIndex, TConstArrayView<PCGExMath::OBB::FCut> Cuts);

private:
	TArray<TSharedPtr<PCGExData::FDataForwardHandler>> IntersectionForwardHandlers;
	// Bounds row per output point, -1 where nothing is forwarded; empty when no target has a handler.
	TArray<int32> ForwardSourceRows;
};
