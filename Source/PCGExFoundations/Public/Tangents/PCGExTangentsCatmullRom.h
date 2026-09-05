// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTangentsInstancedFactory.h"
#include "Factories/PCGExFactoryData.h"
#include "PCGExTangentsCatmullRom.generated.h"

class FPCGExTangentsCatmullRom : public FPCGExTangentsOperation
{
public:
	virtual void ProcessPoint(const UPCGBasePointData* InPointData, const int32 Index, const int32 NextIndex, const int32 PrevIndex, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const override
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		const FVector A = InTransforms[PrevIndex].GetLocation();
		const FVector C = InTransforms[NextIndex].GetLocation();

		const FVector Dir = (C - A);

		OutArrive = (Dir * 0.5f) * ArriveScale;
		OutLeave = (Dir * 0.5f) * LeaveScale;
	}
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Catmull-Rom", PCGExNodeLibraryDoc="paths/common-settings/tangents/catmull-rom"))
class UPCGExCatmullRomTangents : public UPCGExTangentsInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExTangentsOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(TangentsCatmullRom)
		return NewOperation;
	}
};
