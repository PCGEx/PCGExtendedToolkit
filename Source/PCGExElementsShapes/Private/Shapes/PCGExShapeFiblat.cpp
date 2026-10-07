// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Shapes/PCGExShapeFiblat.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"

#define LOCTEXT_NAMESPACE "PCGExCreateBuilderFiblat"
#define PCGEX_NAMESPACE CreateBuilderFiblat

bool FPCGExShapeFiblatBuilder::PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade)
{
	if (!FPCGExShapeBuilderOperation::PrepareForSeeds(InContext, InSeedDataFacade))
	{
		return false;
	}

	if (!InitSettingValue(Phi, Config.Phi.GetValueSetting(), InSeedDataFacade))
	{
		return false;
	}

	return true;
}

void FPCGExShapeFiblatBuilder::PrepareShape(const PCGExData::FConstPoint& Seed)
{
	PCGEX_MAKE_SHARED(Fiblat, PCGExShapes::FFiblat, Seed)

	Fiblat->ComputeFit(BaseConfig);
	Fiblat->Radius = Fiblat->Fit.GetExtent().Length();

	if (Config.ResolutionMode == EPCGExResolutionMode::Distance)
	{
		const double Spacing = GetResolution(Seed) * 100;
		const double Radius = Fiblat->Radius;

		// Estimate number of points based on surface area and desired spacing
		if (Spacing > 0)
		{
			const double SurfaceArea = 4.0 * PI * Radius * Radius;
			const double AreaPerPoint = Spacing * Spacing;
			Fiblat->NumPoints = FMath::Max(1, static_cast<int32>(SurfaceArea / AreaPerPoint));
		}
		else
		{
			Fiblat->NumPoints = 100; // fallback
		}
	}
	else
	{
		Fiblat->NumPoints = GetResolution(Seed);
	}

	switch (Config.PhiConstant)
	{
	case EPCGExFibPhiConstant::GoldenRatio:
		Fiblat->Phi = (FMath::Sqrt(5.0) - 1.0) * 0.5;
		break;
	case EPCGExFibPhiConstant::SqRootOfTwo:
		Fiblat->Phi = FMath::Sqrt(2.0);
		break;
	case EPCGExFibPhiConstant::Irrational:
		Fiblat->Phi = (9 + FMath::Sqrt(221.0)) * 0.1;
		break;
	case EPCGExFibPhiConstant::SqRootOfTthree:
		Fiblat->Phi = FMath::Sqrt(2.0);
		break;
	case EPCGExFibPhiConstant::Ln2:
		Fiblat->Phi = 0.69314718056;
		break;
	case EPCGExFibPhiConstant::Custom:
		Fiblat->Phi = Phi->Read(Seed.Index);
		break;
	}

	Fiblat->Epsilon = Config.Epsilon;

	// Compute extents based on bounds source
	if (BaseConfig.BoundsSource == EPCGExShapeBoundsSource::Fit)
	{
		// Approximate spacing from surface area distribution
		const FVector FitExtents = Fiblat->Fit.GetExtent();
		const double AvgRadius = (FitExtents.X + FitExtents.Y + FitExtents.Z) / 3.0;
		const double SurfaceArea = 4.0 * PI * AvgRadius * AvgRadius;
		const double AreaPerPoint = Fiblat->NumPoints > 0 ? SurfaceArea / Fiblat->NumPoints : SurfaceArea;
		const double Spacing = FMath::Sqrt(AreaPerPoint);
		Fiblat->Extents = FVector(Spacing * 0.5);
	}
	else
	{
		Fiblat->Extents = BaseConfig.DefaultExtents;
	}

	ValidateShape(Fiblat);

	Shapes[Seed.Index] = StaticCastSharedPtr<PCGExShapes::FShape>(Fiblat);
}

void FPCGExShapeFiblatBuilder::BuildShape(const TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, const bool bOwnsData)
{
	const TSharedPtr<PCGExShapes::FFiblat> Fiblat = StaticCastSharedPtr<PCGExShapes::FFiblat>(InShape);

	const FVector Extents = Fiblat->Fit.GetExtent();
	const FVector Center = Fiblat->Fit.GetCenter();
	FVector Target = Center; //FVector::ZeroVector;

	TPCGValueRange<FTransform> OutTransforms = Scope.Data->GetTransformValueRange(false);

	const int32 Count = Fiblat->NumPoints;

	PCGEX_PARALLEL_FOR(
		Count,

		const FVector P = Center + (Fiblat->ComputeFibLatPoint(i, Count) * Extents);
		OutTransforms[Scope.Start + i] = FTransform(PCGExMath::MakeLookAtTransform(P - Target, FVector::UpVector, Config.LookAtAxis).GetRotation(), P, FVector::OneVector);
		)
}

#if WITH_EDITOR
void UPCGExCreateShapeFiblatSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif


PCGEX_SHAPE_BUILDER_BOILERPLATE(Fiblat)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
