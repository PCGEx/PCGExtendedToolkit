// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExNoise3DCommon.h"
#include "UObject/Object.h"
#include "Factories/PCGExOperation.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Math/PCGExMathContrast.h"
#include "Utils/PCGExCurveLookup.h"

/**
 * Base class for all noise operations
 * Thread-safe after initialization
 */
class PCGEXNOISE3D_API FPCGExNoise3DOperation : public FPCGExOperation
{
public:
	/** Weight factor for blending */
	double WeightFactor = 1.0;

	/** Optional remap curve LUT */
	PCGExFloatLUT RemapLUT = nullptr;

	/** Seed for noise generation */
	int32 Seed = 1337;

	/** Frequency (scale) of the noise */
	double Frequency = 1.0;

	/** Number of octaves for fractal noise */
	int32 Octaves = 1;

	/** Lacunarity (frequency multiplier per octave) */
	double Lacunarity = 2.0;

	/** Persistence (amplitude multiplier per octave) */
	double Persistence = 0.5;

	/** Whether to invert output */
	bool bInvert = false;

	/** Contrast adjustment (1.0 = no change, >1 = more contrast, <1 = less) */
	double Contrast = 1.0;

	/** Contrast curve type */
	EPCGExContrastCurve ContrastCurve = EPCGExContrastCurve::Power;

	/** Post-remap scale multiplier */
	double Scale = 1.0;

	/** Whether or not this operation wants transformed inputs */
	bool bApplyTransform = false;

	/** Transform applied to inputs */
	FTransform Transform = FTransform::Identity;

	EPCGExNoiseBlendMode BlendMode = EPCGExNoiseBlendMode::Blend;

	virtual ~FPCGExNoise3DOperation() override = default;

	//
	// Single-point generation (override in derived classes)
	//

	/**
	 * Generate scalar noise at position
	 * @param Position World position
	 * @return Noise value in [0, 1] range (before Scale)
	 */
	virtual double GetDouble(const FVector& Position) const;

	/**
	 * Generate 2D vector noise at position
	 * @param Position World position
	 * @return Two independent noise values
	 */
	virtual FVector2D GetVector2D(const FVector& Position) const;

	/**
	 * Generate 3D vector noise at position
	 * @param Position World position
	 * @return Three independent noise values
	 */
	virtual FVector GetVector(const FVector& Position) const;

	/**
	 * Generate 4D vector noise at position
	 * @param Position World position
	 * @return Four independent noise values
	 */
	virtual FVector4 GetVector4(const FVector& Position) const;

	//
	// Batch generation (can be overridden for optimization)
	//

	/**
	 * Generate scalar noise for multiple positions
	 * Default implementation calls GetDouble in a loop
	 */
	virtual void Generate(TArrayView<const FVector> Positions, TArrayView<double> OutResults) const;
	virtual void Generate(TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults) const;
	virtual void Generate(TArrayView<const FVector> Positions, TArrayView<FVector> OutResults) const;
	virtual void Generate(TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults) const;

protected:
	//
	// Internal helpers
	//

	/** Transform world position to noise space */
	FORCEINLINE FVector TransformPosition(const FVector& Position) const
	{
		return bApplyTransform ? Transform.InverseTransformPosition(Position) : Position;
	}

	/**
	 * Generate raw noise value (without remapping)
	 * Override this in derived classes for the core noise algorithm
	 */
	virtual double GenerateRaw(const FVector& Position) const { return 0.0; }

	/**
	 * Apply post-processing: invert, remap curve, contrast, scale
	 * Input and output in [0, 1] (before Scale)
	 */
	FORCEINLINE double ApplyRemap(double Value) const
	{
		if (bInvert) { Value = 1.0 - Value; }
		if (RemapLUT) { Value = RemapLUT->Eval(Value); }
		if (!FMath::IsNearlyEqual(Contrast, 1.0, SMALL_NUMBER))
		{
			Value = PCGExMath::Contrast::ApplyContrast(Value, Contrast, static_cast<int32>(ContrastCurve));
		}
		return Value * Scale;
	}

	/**
	 * Generate fractal noise with configured octaves
	 */
	double GenerateFractal(const FVector& Position) const;

	/** Precomputed fractal normalization factor */
	mutable double FractalBounding = 1.0;
	mutable bool bFractalBoundingComputed = false;

	void ComputeFractalBounding() const;
};
