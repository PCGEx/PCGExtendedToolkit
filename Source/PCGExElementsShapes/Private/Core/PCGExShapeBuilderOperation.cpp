// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Core/PCGExShapeBuilderOperation.h"

#include "Core/PCGExShape.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"

bool FPCGExShapeBuilderOperation::PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade)
{
	SeedFacade = InSeedDataFacade;

	if (BaseConfig.bThreeDimensions)
	{
		if (!InitSettingValue(ResolutionVector, BaseConfig.ResolutionVector.GetValueSetting(), InSeedDataFacade))
		{
			return false;
		}
	}
	else
	{
		if (!InitSettingValue(Resolution, BaseConfig.Resolution.GetValueSetting(), InSeedDataFacade))
		{
			return false;
		}
	}

	if (!BaseConfig.Fitting.Init(InContext, InSeedDataFacade))
	{
		return false;
	}

	const int32 NumSeeds = SeedFacade->GetNum();
	PCGExArrayHelpers::InitArray(Shapes, NumSeeds);
	return true;
}

void FPCGExShapeBuilderOperation::PrepareShape(const PCGExData::FConstPoint& Seed)
{
	Shapes[Seed.Index] = MakeShared<PCGExShapes::FShape>(Seed);
}

void FPCGExShapeBuilderOperation::ValidateShape(const TSharedPtr<PCGExShapes::FShape> Shape)
{
	if (BaseConfig.bRemoveBelow && Shape->NumPoints < BaseConfig.MinPointCount)
	{
		Shape->bValid = 0;
	}
	if (BaseConfig.bRemoveAbove && Shape->NumPoints > BaseConfig.MaxPointCount)
	{
		Shape->bValid = 0;
	}
}

double FPCGExShapeBuilderOperation::GetResolution(const PCGExData::FConstPoint& Seed) const
{
	const double Res = Resolution->Read(Seed.Index);
	if (BaseConfig.ResolutionMode == EPCGExResolutionMode::Distance)
	{
		return FMath::Abs(Res);
	}
	return FMath::Abs(Res);
}

FVector FPCGExShapeBuilderOperation::GetResolutionVector(const PCGExData::FConstPoint& Seed) const
{
	const FVector Res = ResolutionVector->Read(Seed.Index);
	if (BaseConfig.ResolutionMode == EPCGExResolutionMode::Distance)
	{
		return FVector(FMath::Abs(Res.X), FMath::Abs(Res.Y), FMath::Abs(Res.Z));
	}

	return FVector(FMath::Abs(Res.X), FMath::Abs(Res.Y), FMath::Abs(Res.Z));
}
