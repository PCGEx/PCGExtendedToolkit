// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Shapes/PCGExShapePolygon.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateBuilderPolygon"
#define PCGEX_NAMESPACE CreateBuilderPolygon;

#if WITH_EDITOR
void FPCGExShapePolygonConfig::ApplyDeprecation()
{
	NumVertices.Update(NumVerticesInput_DEPRECATED, NumVerticesAttribute_DEPRECATED, NumVerticesConstant_DEPRECATED);
	AddSkeleton.Update(AddSkeletonInput_DEPRECATED, AddSkeletonAttribute_DEPRECATED, bAddSkeleton_DEPRECATED);
	FPCGExShapeConfigBase::ApplyDeprecation();
}
#endif

bool FPCGExShapePolygonBuilder::PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade)
{
	if (!FPCGExShapeBuilderOperation::PrepareForSeeds(InContext, InSeedDataFacade))
	{
		return false;
	}
	NumVertices = Config.NumVertices.GetValueSetting();
	if (!NumVertices->Init(InSeedDataFacade))
	{
		return false;
	}

	HasSkeleton = Config.AddSkeleton.GetValueSetting();
	if (!HasSkeleton->Init(InSeedDataFacade))
	{
		return false;
	}

	PCGEX_VALIDATE_NAME_C(InContext, Config.AngleAttribute)
	PCGEX_VALIDATE_NAME_C(InContext, Config.EdgeIndexAttribute)
	PCGEX_VALIDATE_NAME_C(InContext, Config.OnHullAttribute)
	PCGEX_VALIDATE_NAME_C(InContext, Config.EdgeAlphaAttribute)

	return true;
}

void FPCGExShapePolygonBuilder::PrepareShape(const PCGExData::FConstPoint& Seed)
{
	PCGEX_MAKE_SHARED(Polygon, PCGExShapes::FPolygon, Seed);

	Polygon->ComputeFit(BaseConfig);
	Polygon->Radius = Polygon->Fit.GetExtent().Length();

	Polygon->NumVertices = NumVertices->Read(Seed.Index);
	Polygon->bHasSkeleton = HasSkeleton->Read(Seed.Index);
	Polygon->EdgeLength = 2.f * Polygon->Radius * FMath::Sin(PI / static_cast<float>(Polygon->NumVertices));
	Polygon->InRadius = 0.5 * Polygon->EdgeLength * (1.0 / FMath::Tan(PI / static_cast<float>(Polygon->NumVertices)));

	Polygon->Config = &Config;

	TArray<FVector> VertexPositions = {};

	const double Increment = TWO_PI / static_cast<double>(Polygon->NumVertices);
	switch (Config.PolygonOrientation)
	{
	case EPCGExPolygonFittingMethod::VertexForward:
		break;
	case EPCGExPolygonFittingMethod::EdgeForward:
		Polygon->Orientation = Increment * .5;
		break;
	case EPCGExPolygonFittingMethod::Custom:
		Polygon->Orientation = Config.CustomPolygonOrientation;
		break;
	}

	FVector Min = {MAX_FLT, MAX_FLT, 0};
	FVector Max = -Min;

	for (int32 i = 0; i < Polygon->NumVertices; i++)
	{
		const double Theta = Polygon->Orientation + Increment * i;
		const FVector Point = FVector(FMath::Cos(Theta), FMath::Sin(Theta), 0.f);
		Min = Min.ComponentMin(Point);
		Max = Max.ComponentMax(Point);
	}

	// This is only really required for polygons where n % 4 == 0, as they can be expanded to fit the corners of the shape
	FVector AbsMin = Min.GetAbs();

	// Get the lowest difference in X and Y and the unit circle
	const float MinXScaleDiff = FMath::Min(1.0 - Max.X, 1.0 - AbsMin.X);
	const float MinYScaleDiff = FMath::Min(1.0 - Max.Y, 1.0 - AbsMin.Y);

	// Get the lowest of those two

	// Scale up by 1 / (1 - min diff) so if min diff is 0, this will be 1, but if greater, it will expand to fit
	if (const float MinScaleDiff = FMath::Min(MinXScaleDiff, MinYScaleDiff);
		!FMath::IsNearlyZero(MinScaleDiff))
	{
		Polygon->ScaleAdjustment = 1.0; //1.0 / (1.0 - MinXScaleDiff);
		Polygon->EdgeLength *= Polygon->ScaleAdjustment;
		Polygon->InRadius *= Polygon->ScaleAdjustment;
		Polygon->Radius *= Polygon->ScaleAdjustment;
	}

	const bool UseDistance = Config.ResolutionMode == EPCGExResolutionMode::Distance;
	const float Res = GetResolution(Seed);
	Polygon->PointsPerEdge = UseDistance ? static_cast<int32>(FMath::Max(Polygon->EdgeLength / Res, 1)) : Res;

	if (Polygon->bHasSkeleton)
	{
		switch (Config.SkeletonConnectionMode)
		{
		case EPCGExPolygonSkeletonConnectionType::Vertex:
			Polygon->bConnectSkeletonToVertices = true;
			break;
		case EPCGExPolygonSkeletonConnectionType::Edge:
			Polygon->bConnectSkeletonToEdges = true;
			break;
		case EPCGExPolygonSkeletonConnectionType::Both:
			Polygon->bConnectSkeletonToVertices = true;
			Polygon->bConnectSkeletonToEdges = true;
			break;
		}

		if (Polygon->bConnectSkeletonToVertices)
		{
			Polygon->PointsPerSpoke = UseDistance ? FMath::Max(Polygon->Radius / Res, 1) : Res;
		}
		if (Polygon->bConnectSkeletonToEdges)
		{
			Polygon->PointsPerEdgeSpoke = UseDistance ? FMath::Max(Polygon->InRadius / Res, 1) : Res;
		}
	}


	Polygon->NumPoints = (Polygon->PointsPerEdge + Polygon->PointsPerSpoke + Polygon->PointsPerEdgeSpoke) * Polygon->NumVertices;

	// Compute extents based on bounds source
	if (BaseConfig.BoundsSource == EPCGExShapeBoundsSource::Fit)
	{
		// Use edge segment spacing as the primary metric
		const double Spacing = Polygon->PointsPerEdge > 0 ? Polygon->EdgeLength / Polygon->PointsPerEdge : Polygon->EdgeLength;
		Polygon->Extents = FVector(Spacing * 0.5);
	}
	else
	{
		Polygon->Extents = BaseConfig.DefaultExtents;
	}

	ValidateShape(Polygon);
	Shapes[Seed.Index] = StaticCastSharedPtr<PCGExShapes::FShape>(Polygon);
}

void FPCGExShapePolygonBuilder::BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, const bool bIsolated)
{
	const TSharedPtr<PCGExShapes::FPolygon> Polygon = StaticCastSharedPtr<PCGExShapes::FPolygon>(InShape);

	TSharedPtr<PCGExData::TBuffer<double>> AngleBuffer = nullptr;
	if (Polygon->Config->bWriteAngleAttribute)
	{
		AngleBuffer = InDataFacade->GetWritable<double>(Polygon->Config->AngleAttribute, 0, true, PCGExData::EBufferInit::New);
		if (!AngleBuffer)
		{
			return;
		}
	}

	TSharedPtr<PCGExData::TBuffer<int32>> EdgeIndexBuffer = nullptr;
	if (Polygon->Config->bWriteEdgeIndexAttribute)
	{
		EdgeIndexBuffer = InDataFacade->GetWritable<int32>(Polygon->Config->EdgeIndexAttribute, -1, true, PCGExData::EBufferInit::New);
		if (!EdgeIndexBuffer)
		{
			return;
		}
	}

	TSharedPtr<PCGExData::TBuffer<double>> EdgeAlphaBuffer = nullptr;
	if (Polygon->Config->bWriteEdgeAlphaAttribute)
	{
		EdgeAlphaBuffer = InDataFacade->GetWritable<double>(Polygon->Config->EdgeAlphaAttribute, 0, true, PCGExData::EBufferInit::New);
		if (!EdgeAlphaBuffer)
		{
			return;
		}
	}

	TSharedPtr<PCGExData::TBuffer<bool>> HullFlagBuffer = nullptr;
	if (Polygon->Config->bWriteHullAttribute)
	{
		HullFlagBuffer = InDataFacade->GetWritable<bool>(Polygon->Config->OnHullAttribute, false, false, PCGExData::EBufferInit::New);
		if (!HullFlagBuffer)
		{
			return;
		}
	}

	const double Increment = TWO_PI / static_cast<double>(Polygon->NumVertices);
	const double Offset = Polygon->Orientation;
	FVector Target = FVector::ZeroVector;

	const FVector Extents = Polygon->Fit.GetExtent();
	const FVector Center = Polygon->Fit.GetCenter();

	TPCGValueRange<FTransform> OutTransforms = Scope.Data->GetTransformValueRange(false);

	int32 WriteIndex = Scope.Start;

	auto AppendPoint = [&](const FVector& Pt, const double Angle, const bool IsOnHull, const int32 EdgeIndex, const double Alpha)
	{
		OutTransforms[WriteIndex] = FTransform(PCGExMath::MakeLookAtTransform(Pt - Target, FVector::UpVector, Config.LookAtAxis).GetRotation(), Pt, FVector::OneVector);

		if (AngleBuffer)
		{
			AngleBuffer->SetValue(WriteIndex, Angle);
		}
		if (HullFlagBuffer)
		{
			HullFlagBuffer->SetValue(WriteIndex, IsOnHull);
		}
		if (EdgeIndexBuffer)
		{
			EdgeIndexBuffer->SetValue(WriteIndex, EdgeIndex);
		}
		if (EdgeAlphaBuffer)
		{
			EdgeAlphaBuffer->SetValue(WriteIndex, Alpha);
		}
	};

	for (int32 i = 0; i < Polygon->NumVertices; i++)
	{
		const double StartTheta = Offset + Increment * i;
		const double EndTheta = StartTheta + Increment;

		const FVector Start = Center + FVector(Extents.X * FMath::Cos(StartTheta), Extents.Y * FMath::Sin(StartTheta), 0.f) * Polygon->ScaleAdjustment;
		const FVector End = Center + FVector(Extents.X * FMath::Cos(EndTheta), Extents.Y * FMath::Sin(EndTheta), 0.f) * Polygon->ScaleAdjustment;
		FVector Delta = (End - Start) / Polygon->PointsPerEdge;

		const double Degrees = FMath::RadiansToDegrees(StartTheta);

		// Outer edges
		for (int32 j = 0; j < Polygon->PointsPerEdge; j++)
		{
			const FVector Point = Start + Delta * j;
			if (Config.PointsLookAt == EPCGExShapePointLookAt::None)
			{
				Target = End;
			}
			AppendPoint(Point, Degrees, true, i, static_cast<double>(j) / static_cast<double>(Polygon->PointsPerEdge));

			WriteIndex++;
		}

		// Centre -> Vertices
		Delta = (Start - Center) / Polygon->PointsPerSpoke;
		for (int32 j = 0; j < Polygon->PointsPerSpoke; j++)
		{
			FVector Point = Center + Delta * j;

			if (Config.PointsLookAt == EPCGExShapePointLookAt::None)
			{
				Target = Start;
			}
			AppendPoint(Point, Degrees, false, i, static_cast<double>(j) / static_cast<double>(Polygon->PointsPerSpoke));

			WriteIndex++;
		}

		// Center -> Midpoints
		FVector MidPoint = Start + (End - Start) * .5;
		Delta = (MidPoint - Center) / Polygon->PointsPerEdgeSpoke;
		for (int32 j = 0; j < Polygon->PointsPerEdgeSpoke; j++)
		{
			FVector Point = Center + Delta * j;

			if (Config.PointsLookAt == EPCGExShapePointLookAt::None)
			{
				Target = MidPoint;
			}
			AppendPoint(Point, Degrees, false, i, static_cast<double>(j) / static_cast<double>(Polygon->PointsPerEdgeSpoke));

			WriteIndex++;
		}

		if (!Polygon->bHasSkeleton && Polygon->Config->bIsClosedLoop && bIsolated)
		{
			PCGExPaths::Helpers::SetClosedLoop(InDataFacade->Source, true);
		}
	}
}

#if WITH_EDITOR
void UPCGExCreateShapePolygonSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		// Rewire NumVertices
		PCGEX_SHORTHAND_RENAME_PIN(NumVerticesAttribute, NumVerticesConstant, NumVertices)

		// Rewire Add Skeleton — old bool constant was `bAddSkeleton` (derived display "Add Skeleton")
		PCGEX_SHORTHAND_RENAME_PIN(AddSkeletonAttribute, bAddSkeleton, AddSkeleton)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateShapePolygonSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 11)
	{
		Config.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif


PCGEX_SHAPE_BUILDER_BOILERPLATE(Polygon)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
