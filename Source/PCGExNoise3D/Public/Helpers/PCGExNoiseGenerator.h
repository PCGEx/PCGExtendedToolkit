// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DCommon.h"

class FPCGExNoise3DOperation;
struct FPCGExContext;

namespace PCGExNoise3D
{
	/**
	 * Noise generator that combines multiple noise operations
	 * Thread-safe after initialization
	 */
	class PCGEXNOISE3D_API FNoiseGenerator : public TSharedFromThis<FNoiseGenerator>
	{
	protected:
		TArray<TSharedPtr<FPCGExNoise3DOperation>> Operations;
		TArray<const FPCGExNoise3DOperation*> OperationsPtr;
		TArray<double> Weights;
		TArray<EPCGExNoiseBlendMode> BlendModes;

		/** Precomputed blend factors: BlendFactors[i] = Weights[i] / sum(Weights[0..i]) */
		TArray<double> BlendFactors;

		double TotalWeight = 0.0;

		/** How GenerateInPlace combines initial value with noise */
		EPCGExNoiseInPlaceMode InPlaceMode = EPCGExNoiseInPlaceMode::Composite;

		/** Blend mode used for BlendResult in-place mode */
		EPCGExNoiseBlendMode InPlaceBlendMode = EPCGExNoiseBlendMode::Blend;

	public:
		FNoiseGenerator() = default;

		/**
		 * Initialize from factories in context
		 * @param InContext PCG context containing factory inputs
		 * @param bThrowError Whether to log errors if no factories found
		 * @return true if at least one operation was created
		 */
		bool Init(FPCGExContext* InContext, bool bThrowError = true);
		bool Init(FPCGExContext* InContext, const FName SourceLabel, bool bThrowError = true);

		/**
		 * Get number of operations
		 */
		int32 Num() const
		{
			return Operations.Num();
		}

		/**
		 * Check if generator has any operations
		 */
		bool IsValid() const
		{
			return Operations.Num() > 0;
		}

		/**
		 * Get total weight of all operations
		 */
		double GetTotalWeight() const
		{
			return TotalWeight;
		}

		/**
		 * Set how GenerateInPlace combines initial value with noise
		 * @param Mode The in-place combination mode
		 * @param BlendMode Blend mode used when Mode is BlendResult (ignored otherwise)
		 */
		void SetInPlaceMode(EPCGExNoiseInPlaceMode Mode, EPCGExNoiseBlendMode BlendMode = EPCGExNoiseBlendMode::Blend)
		{
			InPlaceMode = Mode;
			InPlaceBlendMode = BlendMode;
		}

		EPCGExNoiseInPlaceMode GetInPlaceMode() const
		{
			return InPlaceMode;
		}

		EPCGExNoiseBlendMode GetInPlaceBlendMode() const
		{
			return InPlaceBlendMode;
		}

		//
		// Single-point generation
		//

		double GetDouble(const FVector& Position) const;
		FVector2D GetVector2D(const FVector& Position) const;
		FVector GetVector(const FVector& Position) const;
		FVector4 GetVector4(const FVector& Position) const;

		//
		// Batch generation (optimized for parallel processing)
		//

		void Generate(TArrayView<const FVector> Positions, TArrayView<double> OutResults) const;
		void Generate(TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults) const;
		void Generate(TArrayView<const FVector> Positions, TArrayView<FVector> OutResults) const;
		void Generate(TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults) const;

		//
		// Parallel batch generation (chunked; preserves the batch fast path)
		//

		void GenerateParallel(TArrayView<const FVector> Positions, TArrayView<double> OutResults, int32 MinBatchSize = 256) const;
		void GenerateParallel(TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults, int32 MinBatchSize = 256) const;
		void GenerateParallel(TArrayView<const FVector> Positions, TArrayView<FVector> OutResults, int32 MinBatchSize = 256) const;
		void GenerateParallel(TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults, int32 MinBatchSize = 256) const;

	private:
		/** Batch core. Scratch must be OutResults-sized when more than one operation is stacked (unused otherwise). */
		template <typename ValueType>
		void GenerateImpl(TArrayView<const FVector> Positions, TArrayView<ValueType> OutResults, TArrayView<ValueType> Scratch) const;

		template <typename ValueType>
		void GenerateTyped(TArrayView<const FVector> Positions, TArrayView<ValueType> OutResults) const;

		template <typename ValueType>
		void GenerateParallelImpl(TArrayView<const FVector> Positions, TArrayView<ValueType> OutResults, int32 MinBatchSize) const;
		//
		// Single-value blend (used for single-point generation)
		//

		double BlendSingle(EPCGExNoiseBlendMode BlendMode, double A, double B, double BlendFactor) const;
		FVector2D BlendSingle(EPCGExNoiseBlendMode BlendMode, const FVector2D& A, const FVector2D& B, double BlendFactor) const;
		FVector BlendSingle(EPCGExNoiseBlendMode BlendMode, const FVector& A, const FVector& B, double BlendFactor) const;
		FVector4 BlendSingle(EPCGExNoiseBlendMode BlendMode, const FVector4& A, const FVector4& B, double BlendFactor) const;

		//
		// Batch blend helpers (switch outside loop)
		//

		void BlendBatch(EPCGExNoiseBlendMode BlendMode, double* RESTRICT OutResults, const double* RESTRICT InValues, int32 Count, double BlendFactor) const;
		void BlendBatch(EPCGExNoiseBlendMode BlendMode, FVector2D* RESTRICT OutResults, const FVector2D* RESTRICT InValues, int32 Count, double BlendFactor) const;
		void BlendBatch(EPCGExNoiseBlendMode BlendMode, FVector* RESTRICT OutResults, const FVector* RESTRICT InValues, int32 Count, double BlendFactor) const;
		void BlendBatch(EPCGExNoiseBlendMode BlendMode, FVector4* RESTRICT OutResults, const FVector4* RESTRICT InValues, int32 Count, double BlendFactor) const;

		//
		// Photoshop-style blend helpers
		//

		static FORCEINLINE double ScreenBlend(double A, double B)
		{
			return 1.0 - (1.0 - A) * (1.0 - B);
		}

		static FORCEINLINE double OverlayBlend(double A, double B)
		{
			return A < 0.5 ? 2.0 * A * B : 1.0 - 2.0 * (1.0 - A) * (1.0 - B);
		}

		static FORCEINLINE double SoftLightBlend(double A, double B)
		{
			return B < 0.5 ? A - (1.0 - 2.0 * B) * A * (1.0 - A) : A + (2.0 * B - 1.0) * (((A - 0.5) * 2.0) - A * (1.0 - A));
		}
	};
}
