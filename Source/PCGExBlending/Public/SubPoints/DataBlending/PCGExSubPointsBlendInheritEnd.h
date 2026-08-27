// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSubPointsBlendOperation.h"


#include "PCGExSubPointsBlendInheritEnd.generated.h"

class FPCGExSubPointsBlendInheritEnd : public FPCGExSubPointsBlendOperation
{
public:
	virtual void BlendSubPoints(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, PCGExData::FScope& Scope, const PCGExPaths::FPathMetrics& Metrics) const override;
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Inherit Last", PCGExNodeLibraryDoc="paths/common-settings/sub-points-blending/inherit-last"))
class UPCGExSubPointsBlendInheritEnd : public UPCGExSubPointsBlendInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExSubPointsBlendOperation> CreateOperation() const override;

protected:
	virtual EPCGExBlendingType GetDefaultBlending() const override
	{
		return EPCGExBlendingType::Copy; // = B, the gap's last bracket
	}
};
