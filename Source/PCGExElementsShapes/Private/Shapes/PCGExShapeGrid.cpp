// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Shapes/PCGExShapeGrid.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Sampling/PCGExSamplingCommon.h"

#define LOCTEXT_NAMESPACE "PCGExCreateBuilderGrid"
#define PCGEX_NAMESPACE CreateBuilderGrid

bool FPCGExShapeGridBuilder::PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade)
{
	if (!FPCGExShapeBuilderOperation::PrepareForSeeds(InContext, InSeedDataFacade))
	{
		return false;
	}


	return true;
}

void FPCGExShapeGridBuilder::PrepareShape(const PCGExData::FConstPoint& Seed)
{
	PCGEX_MAKE_SHARED(Grid, PCGExShapes::FGrid, Seed)

	Grid->ComputeFit(BaseConfig);

	const FVector Res = GetResolutionVector(Seed);
	const FVector Size = Grid->Fit.GetSize();

	auto ApplyClamp = [&]()
	{
		Grid->Count.X = Config.AxisClampDetailsX.GetClampedValue(Grid->Count.X);
		Grid->Count.Y = Config.AxisClampDetailsY.GetClampedValue(Grid->Count.Y);
		Grid->Count.Z = Config.AxisClampDetailsZ.GetClampedValue(Grid->Count.Z);
	};

	EPCGExTruncateMode Truncate[3] = {Config.TruncateX, Config.TruncateY, Config.TruncateZ};

	// Compute grid counts based on resolution mode
	if (Config.ResolutionMode == EPCGExResolutionMode::Fixed)
	{
		for (int i = 0; i < 3; i++)
		{
			Grid->Count[i] = FMath::Max(1, PCGExMath::TruncateDbl(Res[i], Truncate[i]));
		}
	}
	else
	{
		for (int i = 0; i < 3; i++)
		{
			Grid->Count[i] = FMath::Max(1, PCGExMath::TruncateDbl(Size[i] / Res[i], Truncate[i]));
		}
	}

	ApplyClamp();

	// Compute extents based on bounds source
	if (BaseConfig.BoundsSource == EPCGExShapeBoundsSource::Fit)
	{
		if (Config.ResolutionMode == EPCGExResolutionMode::Fixed)
		{
			for (int i = 0; i < 3; i++)
			{
				Grid->Extents[i] = (Size[i] / Grid->Count[i]) * 0.5;
			}
		}
		else
		{
			for (int i = 0; i < 3; i++)
			{
				Grid->Extents[i] = Res[i] * 0.5;
			}
		}

		const EPCGExApplySampledComponentFlags FitFlags = static_cast<EPCGExApplySampledComponentFlags>(Config.AdjustFit);

#define PCGEX_ADJUST_FIT(_AXIS) \
	if (EnumHasAnyFlags(FitFlags, EPCGExApplySampledComponentFlags::_AXIS)) { Grid->Extents._AXIS = (Size._AXIS / Grid->Count._AXIS) * 0.5; }

		PCGEX_ADJUST_FIT(X)
		PCGEX_ADJUST_FIT(Y)
		PCGEX_ADJUST_FIT(Z)

#undef PCGEX_ADJUST_FIT
	}
	else
	{
		Grid->Extents = BaseConfig.DefaultExtents;
	}

	for (int i = 0; i < 3; i++)
	{
		Grid->Offset[i] = Grid->Extents[i] + (Size[i] - (Grid->Count[i] * (Grid->Extents[i] * 2))) * 0.5;
	}

	Grid->bClosedLoop = false;
	Grid->NumPoints = Grid->Count.X * Grid->Count.Y * Grid->Count.Z;

	ValidateShape(Grid);

	Shapes[Seed.Index] = StaticCastSharedPtr<PCGExShapes::FShape>(Grid);
}

void FPCGExShapeGridBuilder::BuildShape(const TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, const bool bOwnsData)
{
	const TSharedPtr<PCGExShapes::FGrid> Grid = StaticCastSharedPtr<PCGExShapes::FGrid>(InShape);

	const FVector Center = Grid->Fit.GetCenter();
	const FVector Corner = Center - Grid->Fit.GetExtent();

	const double XStep = Grid->Extents.X * 2;
	const double YStep = Grid->Extents.Y * 2;
	const double ZStep = Grid->Extents.Z * 2;

	const FVector MaxBounds = FVector(XStep, YStep, ZStep) * 0.5;
	const FVector MinBounds = -MaxBounds;

	TPCGValueRange<FTransform> OutTransforms = Scope.Data->GetTransformValueRange(false);
	TPCGValueRange<FVector> OutBoundsMin = Scope.Data->GetBoundsMinValueRange(false);
	TPCGValueRange<FVector> OutBoundsMax = Scope.Data->GetBoundsMaxValueRange(false);

	PCGExMT::ParallelOrSequential(
		Scope.Count,
		[&](const int32 i)
		{
			const int32 WriteIndex = Scope.Start + i;

			OutBoundsMin[WriteIndex] = MinBounds;
			OutBoundsMax[WriteIndex] = MaxBounds;

			const int32 X = i % Grid->Count.X;
			const int32 Y = (i / Grid->Count.X) % Grid->Count.Y;
			const int32 Z = i / (Grid->Count.X * Grid->Count.Y);

			FVector Target;
			FVector Point = FVector(
					Corner.X + (X * XStep),
					Corner.Y + (Y * YStep),
					Corner.Z + (Z * ZStep))
				+ Grid->Offset;

			if (Config.PointsLookAt == EPCGExShapePointLookAt::None)
			{
				OutTransforms[WriteIndex] = FTransform(FRotator::ZeroRotator, Point, FVector::OneVector);
			}
			else
			{
				switch (Config.PointsLookAt)
				{
				case EPCGExShapePointLookAt::Seed:
					Target = Center;
					break;
				case EPCGExShapePointLookAt::None:
					Target = Point + FVector(0, 0, 1);
					break;
				default:
					Target = Point;
					break;
				}

				OutTransforms[WriteIndex] = FTransform(
					PCGExMath::MakeLookAtTransform(Point - Target, FVector::UpVector, Config.LookAtAxis).GetRotation(),
					Point, FVector::OneVector
					);
			}
		});

	if (bOwnsData && Grid->bClosedLoop)
	{
		PCGExPaths::Helpers::SetClosedLoop(InDataFacade->GetOut(), true);
	}
}

#if WITH_EDITOR
void UPCGExCreateShapeGridSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif


PCGEX_SHAPE_BUILDER_BOILERPLATE(Grid)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
