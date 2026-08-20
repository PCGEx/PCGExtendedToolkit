// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExPointElements.h"
#include "Math/PCGExMathAxis.h"
#include "Paths/PCGExPath.h"
#include "SubPoints/PCGExSubPointsInstancedFactory.h"
#include "PCGExOrientOperation.generated.h"

class UPCGExOrientInstancedFactory;

class PCGEXELEMENTSPATHS_API FPCGExOrientOperation : public FPCGExOperation
{
public:
	const UPCGExOrientInstancedFactory* Factory = nullptr;
	TSharedPtr<PCGExPaths::FPath> Path;

	virtual bool PrepareForData(const TSharedRef<PCGExData::FFacade>& InDataFacade, const TSharedRef<PCGExPaths::FPath>& InPath)
	{
		Path = InPath;
		return true;
	}

	virtual FTransform ComputeOrientation(const PCGExData::FConstPoint& Point, const double DirectionMultiplier) const
	{
		return Point.GetTransform();
	}
};

/**
 * 
 */
UCLASS(Abstract, meta=(PCGExNodeLibraryDoc="paths/transform/path-orient/orient-operation"))
class PCGEXELEMENTSPATHS_API UPCGExOrientInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	EPCGExAxis OrientAxis = EPCGExAxis::Forward;
	EPCGExAxis UpAxis = EPCGExAxis::Up;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override
	{
		Super::CopySettingsFrom(Other);
		if (const UPCGExOrientInstancedFactory* TypedOther = Cast<UPCGExOrientInstancedFactory>(Other))
		{
			OrientAxis = TypedOther->OrientAxis;
			UpAxis = TypedOther->UpAxis;
		}
	}

	virtual TSharedPtr<FPCGExOrientOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);
};
