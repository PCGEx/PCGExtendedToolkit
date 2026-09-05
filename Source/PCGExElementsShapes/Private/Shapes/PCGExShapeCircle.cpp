// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Shapes/PCGExShapeCircle.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateBuilderCircle"
#define PCGEX_NAMESPACE CreateBuilderCircle

#if WITH_EDITOR
void FPCGExShapeCircleConfig::ApplyDeprecation()
{
	StartAngle.Update(StartAngleInput_DEPRECATED, StartAngleAttribute_DEPRECATED, StartAngleConstant_DEPRECATED);
	EndAngle.Update(EndAngleInput_DEPRECATED, EndAngleAttribute_DEPRECATED, EndAngleConstant_DEPRECATED);
	FPCGExShapeConfigBase::ApplyDeprecation();
}
#endif

bool FPCGExShapeCircleBuilder::PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade)
{
	if (!FPCGExShapeBuilderOperation::PrepareForSeeds(InContext, InSeedDataFacade))
	{
		return false;
	}

	StartAngle = Config.StartAngle.GetValueSetting();
	if (!StartAngle->Init(InSeedDataFacade))
	{
		return false;
	}

	EndAngle = Config.EndAngle.GetValueSetting();
	if (!EndAngle->Init(InSeedDataFacade))
	{
		return false;
	}

	return true;
}

void FPCGExShapeCircleBuilder::PrepareShape(const PCGExData::FConstPoint& Seed)
{
	PCGEX_MAKE_SHARED(Circle, PCGExShapes::FCircle, Seed)

	Circle->ComputeFit(BaseConfig);

	Circle->StartAngle = FMath::DegreesToRadians(StartAngle->Read(Seed.Index));
	Circle->EndAngle = FMath::DegreesToRadians(EndAngle->Read(Seed.Index));
	Circle->AngleRange = FMath::Abs(Circle->EndAngle - Circle->StartAngle);

	Circle->Radius = Circle->Fit.GetExtent().Length();
	Circle->bClosedLoop = Config.bIsClosedLoop || FMath::IsNearlyEqual(Circle->AngleRange, TWO_PI);

	if (Config.ResolutionMode == EPCGExResolutionMode::Distance)
	{
		Circle->NumPoints = (Circle->Radius * Circle->AngleRange) / GetResolution(Seed);
	}
	else
	{
		Circle->NumPoints = GetResolution(Seed);
	}

	// Compute extents based on bounds source
	if (BaseConfig.BoundsSource == EPCGExShapeBoundsSource::Fit)
	{
		// Arc length between adjacent points: (AverageRadius * AngleRange) / NumPoints
		const FVector FitExtents = Circle->Fit.GetExtent();
		const double AvgRadius = (FitExtents.X + FitExtents.Y) * 0.5;
		const double Spacing = Circle->NumPoints > 0 ? (AvgRadius * Circle->AngleRange) / Circle->NumPoints : 0;
		Circle->Extents = FVector(Spacing * 0.5);
	}
	else
	{
		Circle->Extents = BaseConfig.DefaultExtents;
	}

	ValidateShape(Circle);

	Shapes[Seed.Index] = StaticCastSharedPtr<PCGExShapes::FShape>(Circle);
}

void FPCGExShapeCircleBuilder::BuildShape(const TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, const bool bOwnsData)
{
	const TSharedPtr<PCGExShapes::FCircle> Circle = StaticCastSharedPtr<PCGExShapes::FCircle>(InShape);

	const double Increment = Circle->AngleRange / Circle->NumPoints;
	FVector Target = FVector::ZeroVector;

	const FVector Extents = Circle->Fit.GetExtent();
	const FVector Center = Circle->Fit.GetCenter();

	TPCGValueRange<FTransform> OutTransforms = Scope.Data->GetTransformValueRange(false);

	for (int32 i = 0; i < Circle->NumPoints; i++)
	{
		const double A = (Circle->StartAngle + Increment * 0.5) + i * Increment;

		const FVector P = Center + FVector(Extents.X * FMath::Cos(A), Extents.Y * FMath::Sin(A), 0);

		if (Config.PointsLookAt == EPCGExShapePointLookAt::None)
		{
			Target = Center + FVector(Extents.X * FMath::Cos(A + 0.001), Extents.Y * FMath::Sin(A + 0.001), 0);
		}

		OutTransforms[Scope.Start + i] = FTransform(PCGExMath::MakeLookAtTransform(P - Target, FVector::UpVector, Config.LookAtAxis).GetRotation(), P, FVector::OneVector);
	}

	// Mark @Data.IsClosed if a single circle "owns" the data
	if (bOwnsData && Circle->bClosedLoop)
	{
		PCGExPaths::Helpers::SetClosedLoop(InDataFacade->GetOut(), true);
	}
}

#if WITH_EDITOR
void UPCGExCreateShapeCircleSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		PCGEX_SHORTHAND_RENAME_PIN(StartAngleAttribute, StartAngleConstant, StartAngle)

		PCGEX_SHORTHAND_RENAME_PIN(EndAngleAttribute, EndAngleConstant, EndAngle)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateShapeCircleSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif


PCGEX_SHAPE_BUILDER_BOILERPLATE(Circle)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
