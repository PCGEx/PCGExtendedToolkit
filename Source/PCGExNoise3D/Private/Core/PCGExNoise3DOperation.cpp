// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExNoise3DOperation.h"
#include "Helpers/PCGExNoise3DMath.h"

void FPCGExNoise3DOperation::PostInit()
{
	FractalBounding = PCGExNoise3D::Math::CalcFractalBounding(Octaves, Persistence);
	bApplyContrast = !FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER);
	PostInitDerived();
}

double FPCGExNoise3DOperation::GenerateFractal(const FVector& Position) const
{
	if (Octaves <= 1)
	{
		return GenerateRaw(Position * Frequency);
	}

	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += GenerateRaw(Position * Freq) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * FractalBounding;
}

double FPCGExNoise3DOperation::GetDouble(const FVector& Position) const
{
	return ApplyRemap(GenerateFractal(TransformPosition(Position)));
}

FVector2D FPCGExNoise3DOperation::GetVector2D(const FVector& Position) const
{
	// Generate two independent noise values using position offsets
	const double X = GetDouble(Position);
	const double Y = GetDouble(Position + FVector(127.1, 311.7, 74.7));
	return FVector2D(X, Y);
}

FVector FPCGExNoise3DOperation::GetVector(const FVector& Position) const
{
	const double X = GetDouble(Position);
	const double Y = GetDouble(Position + FVector(127.1, 311.7, 74.7));
	const double Z = GetDouble(Position + FVector(269.5, 183.3, 246.1));
	return FVector(X, Y, Z);
}

FVector4 FPCGExNoise3DOperation::GetVector4(const FVector& Position) const
{
	const double X = GetDouble(Position);
	const double Y = GetDouble(Position + FVector(127.1, 311.7, 74.7));
	const double Z = GetDouble(Position + FVector(269.5, 183.3, 246.1));
	const double W = GetDouble(Position + FVector(419.2, 371.9, 168.2));
	return FVector4(X, Y, Z, W);
}

void FPCGExNoise3DOperation::Generate(const TArrayView<const FVector> Positions, TArrayView<double> OutResults) const
{
	check(Positions.Num() == OutResults.Num());
	const int32 Count = Positions.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		OutResults[i] = GetDouble(Positions[i]);
	}
}

void FPCGExNoise3DOperation::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults) const
{
	check(Positions.Num() == OutResults.Num());
	const int32 Count = Positions.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		OutResults[i] = GetVector2D(Positions[i]);
	}
}

void FPCGExNoise3DOperation::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector> OutResults) const
{
	check(Positions.Num() == OutResults.Num());
	const int32 Count = Positions.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		OutResults[i] = GetVector(Positions[i]);
	}
}

void FPCGExNoise3DOperation::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults) const
{
	check(Positions.Num() == OutResults.Num());
	const int32 Count = Positions.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		OutResults[i] = GetVector4(Positions[i]);
	}
}
