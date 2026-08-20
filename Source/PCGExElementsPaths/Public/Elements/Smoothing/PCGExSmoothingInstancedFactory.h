// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExOpStats.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"

#include "PCGExSmoothingInstancedFactory.generated.h"

namespace PCGExSmooth
{
	class FProcessor;
}

namespace PCGExBlending
{
	class IBlender;
}

class PCGEXELEMENTSPATHS_API FPCGExSmoothingOperation : public FPCGExOperation
{
	friend class PCGExSmooth::FProcessor;

public:
	virtual void SmoothSingle(const int32 TargetIndex, const double Smoothing, const double Influence, TArray<PCGEx::FOpStats>& Trackers)
	{
	}

protected:
	TSharedPtr<PCGExData::FPointIO> Path;
	TSharedPtr<PCGExBlending::IBlender> Blender;
	bool bClosedLoop = false;
};

/**
 * 
 */
UCLASS(Abstract, meta=(PCGExNodeLibraryDoc="paths/transform/path-smooth/smoothing"))
class PCGEXELEMENTSPATHS_API UPCGExSmoothingInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExSmoothingOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);
};
