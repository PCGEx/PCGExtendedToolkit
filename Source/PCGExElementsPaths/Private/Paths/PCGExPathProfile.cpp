// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExPathProfile.h"

#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSubdivisionDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/Geo/PCGExGeo.h"
#include "Math/RotationMatrix.h"
#include "Utils/PCGValueRange.h"

namespace PCGExPaths::Profile
{
	// Resolves how many subdivision points fit in Dist, and how far apart they sit.
	// Attribute-driven factors bypass property clamps, so both modes are hardened here:
	// counts are clamped into int32 range before the cast (out-of-range double->int32 is UB),
	// and distance factors get a min segment length of 1 so degenerate spacing can't explode
	// the count on huge spans (e.g. wide reflex arcs).
	int32 ResolveSubdivisions(const double Dist, const double Factor, const bool bIsCount, double& OutStepSize)
	{
		int32 SubdivCount = 0;

		if (bIsCount)
		{
			SubdivCount = static_cast<int32>(FMath::Clamp(Factor, 0.0, static_cast<double>(MAX_int32)));
			OutStepSize = Dist / (static_cast<double>(SubdivCount) + 1.0);
		}
		else if (Factor > KINDA_SMALL_NUMBER)
		{
			const double StepDist = FMath::Max(Factor, 1.0);
			SubdivCount = static_cast<int32>(FMath::Clamp(FMath::Floor(Dist / StepDist), 0.0, static_cast<double>(MAX_int32)));
			OutStepSize = FMath::Min(Dist, StepDist);
		}
		else
		{
			OutStepSize = Dist;
		}

		return SubdivCount;
	}

	void SubdivideLine(TArray<FVector>& Out, const FVector& A, const FVector& B, const double Factor, const bool bIsCount)
	{
		double StepSize = 0;
		const int32 SubdivCount = ResolveSubdivisions(FVector::Dist(A, B), Factor, bIsCount, StepSize);

		PCGExArrayHelpers::InitArray(Out, SubdivCount);
		const FVector Dir = (B - A).GetSafeNormal();
		for (int i = 0; i < SubdivCount; i++)
		{
			Out[i] = A + Dir * (StepSize + i * StepSize);
		}
	}

	void SubdivideLineKeepCorner(TArray<FVector>& Out, const FVector& A, const FVector& Corner, const FVector& B, const double Factor, const bool bIsCount)
	{
		double StepSize = 0;
		// Two mirrored legs plus the corner; keep the doubled count inside int32 range
		const int32 SubdivCount = FMath::Min(ResolveSubdivisions(FVector::Dist(A, Corner), Factor, bIsCount, StepSize), (MAX_int32 - 1) / 2);

		PCGExArrayHelpers::InitArray(Out, SubdivCount * 2 + 1);

		if (SubdivCount == 0)
		{
			Out[0] = Corner;
		}
		else
		{
			int32 WriteIndex = 0;
			FVector Dir = (Corner - A).GetSafeNormal();
			for (int i = 0; i < SubdivCount; i++)
			{
				Out[WriteIndex++] = A + Dir * (StepSize + i * StepSize);
			}

			Out[WriteIndex++] = Corner;

			Dir = (B - Corner).GetSafeNormal();
			for (int i = 0; i < SubdivCount; i++)
			{
				Out[WriteIndex++] = Corner + Dir * (StepSize + i * StepSize);
			}
		}
	}

	void SubdivideArc(TArray<FVector>& Out, const PCGExMath::Geo::FExCenterArc& Arc, const FVector& A, const FVector& B, const double Factor, const bool bIsCount)
	{
		if (Arc.bIsLine)
		{
			// Fallback to line since we can't infer a proper radius
			SubdivideLine(Out, A, B, Factor, bIsCount);
			return;
		}

		double DistStepSize = 0;
		const int32 SubdivCount = ResolveSubdivisions(Arc.GetLength(), Factor, bIsCount, DistStepSize);

		const double StepSize = 1.0 / (static_cast<double>(SubdivCount) + 1.0);
		PCGExArrayHelpers::InitArray(Out, SubdivCount);

		for (int i = 0; i < SubdivCount; i++)
		{
			Out[i] = Arc.GetLocationOnArc(StepSize + i * StepSize);
		}
	}

	void SubdivideCustom(TArray<FVector>& Out, const TArray<FVector>& ProfilePositions, const FVector& A, const FVector& B, const FVector& PlaneNormal, const double MainAxisSize, const double CrossAxisSize)
	{
		const int32 SubdivCount = ProfilePositions.Num() - 2;

		PCGExArrayHelpers::InitArray(Out, SubdivCount);

		if (SubdivCount == 0)
		{
			return;
		}

		const double ProfileSize = FVector::Dist(B, A);
		const FVector ProjectionNormal = (B - A).GetSafeNormal(1E-08, FVector::ForwardVector);
		const FQuat ProjectionQuat = FRotationMatrix::MakeFromZX(PlaneNormal, ProjectionNormal).ToQuat();

		for (int i = 0; i < SubdivCount; i++)
		{
			FVector Pos = ProfilePositions[i + 1];
			Pos.X *= ProfileSize;
			Pos.Y *= MainAxisSize;
			Pos.Z *= CrossAxisSize;
			Out[i] = A + ProjectionQuat.RotateVector(Pos);
		}
	}

	void SubdivideManhattan(TArray<FVector>& Out, const FPCGExManhattanDetails& Details, const int32 Index, const FVector& A, const FVector& B, const FVector* Corner)
	{
		Out.Reset();
		double OutDist = 0;

		if (Corner)
		{
			Details.ComputeSubdivisions(A, *Corner, Index, Out, OutDist);
			Out.Emplace(*Corner);
			Details.ComputeSubdivisions(*Corner, B, Index, Out, OutDist);
		}
		else
		{
			Details.ComputeSubdivisions(A, B, Index, Out, OutDist);
		}
	}

	double ResolveAxisSize(const EPCGExPathProfileScaling Scaling, const double Scale, const double UniformSize, const double ScaleReference)
	{
		switch (Scaling)
		{
		case EPCGExPathProfileScaling::Scale: return ScaleReference * Scale;
		case EPCGExPathProfileScaling::Distance: return Scale;
		default: return UniformSize;
		}
	}

	bool TryBuildCustomProfile(FPCGExContext* InContext, const FName InPinLabel, TSharedPtr<PCGExData::FFacade>& OutFacade, TArray<FVector>& OutPositions)
	{
		const TSharedPtr<PCGExData::FPointIO> ProfileIO = PCGExData::TryGetSingleInput(InContext, InPinLabel, false, true);
		if (!ProfileIO)
		{
			return false;
		}

		if (ProfileIO->GetNum() < 2)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Custom profile must have at least two points."));
			return false;
		}

		OutFacade = MakeShared<PCGExData::FFacade>(ProfileIO.ToSharedRef());

		TConstPCGValueRange<FTransform> ProfileTransforms = ProfileIO->GetIn()->GetConstTransformValueRange();
		PCGExArrayHelpers::InitArray(OutPositions, ProfileTransforms.Num());

		const FVector Start = ProfileTransforms[0].GetLocation();
		const FVector End = ProfileTransforms[ProfileTransforms.Num() - 1].GetLocation();

		const double ProfileLength = FVector::Dist(Start, End);
		if (ProfileLength <= KINDA_SMALL_NUMBER)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Custom profile first and last points must not overlap."));
			return false;
		}

		const double Factor = 1 / ProfileLength;

		const FVector ProjectionNormal = (End - Start).GetSafeNormal(1E-08, FVector::ForwardVector);
		const FQuat ProjectionQuat = FQuat::FindBetweenNormals(ProjectionNormal, FVector::ForwardVector);

		for (int i = 0; i < ProfileTransforms.Num(); i++)
		{
			OutPositions[i] = ProjectionQuat.RotateVector((ProfileTransforms[i].GetLocation() - Start) * Factor);
		}

		return true;
	}
}
