// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExNoiseGenerator.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Async/ParallelFor.h"
#include "Core/PCGExNoise3DOperation.h"

#define LOCTEXT_NAMESPACE "PCGExNoise3D"
#define PCGEX_NAMESPACE Noise3D

namespace PCGExNoise3D
{
	bool FNoiseGenerator::Init(FPCGExContext* InContext, const FName SourceLabel, bool bThrowError)
	{
		TArray<TObjectPtr<const UPCGExNoise3DFactoryData>> Factories;
		if (!PCGExFactories::GetInputFactories(InContext, SourceLabel, Factories, {PCGExFactories::EType::Noise3D}, bThrowError))
		{
			return false;
		}

		const int32 Num = Factories.Num();
		Operations.Reserve(Num);
		OperationsPtr.Reserve(Num);
		Weights.Reserve(Num);
		BlendModes.Reserve(Num);
		BlendFactors.Reserve(Num);
		TotalWeight = 0.0;

		for (const TObjectPtr<const UPCGExNoise3DFactoryData>& Factory : Factories)
		{
			TSharedPtr<FPCGExNoise3DOperation> Operation = Factory->CreateOperation(InContext);
			if (!Operation) { continue; }

			const double Weight = FMath::Max(SMALL_NUMBER, Factory->ConfigBase.WeightFactor);
			TotalWeight += Weight;

			Operations.Add(Operation);
			OperationsPtr.Add(Operation.Get());
			Weights.Add(Weight);
			BlendModes.Add(Operation->BlendMode);

			// Precompute blend factor: this operation's weight relative to accumulated total
			BlendFactors.Add(Weight / TotalWeight);
		}

		return Operations.Num() > 0;
	}

	bool FNoiseGenerator::Init(FPCGExContext* InContext, const bool bThrowError)
	{
		return Init(InContext, Labels::SourceNoise3DLabel, bThrowError);
	}

	//
	// Single-value blend implementation
	//

	double FNoiseGenerator::BlendSingle(const EPCGExNoiseBlendMode BlendMode, const double A, const double B, const double BlendFactor) const
	{
		switch (BlendMode)
		{
		case EPCGExNoiseBlendMode::Blend:
			return A + (B - A) * BlendFactor;

		case EPCGExNoiseBlendMode::Add:
			return FMath::Lerp(A, FMath::Clamp(A + B, 0.0, 1.0), BlendFactor);

		case EPCGExNoiseBlendMode::Subtract:
			return FMath::Lerp(A, FMath::Clamp(A - B, 0.0, 1.0), BlendFactor);

		case EPCGExNoiseBlendMode::Multiply:
			return FMath::Lerp(A, A * B, BlendFactor);

		case EPCGExNoiseBlendMode::Min:
			return FMath::Lerp(A, FMath::Min(A, B), BlendFactor);

		case EPCGExNoiseBlendMode::Max:
			return FMath::Lerp(A, FMath::Max(A, B), BlendFactor);

		case EPCGExNoiseBlendMode::Screen:
			return FMath::Lerp(A, ScreenBlend(A, B), BlendFactor);

		case EPCGExNoiseBlendMode::Overlay:
			return FMath::Lerp(A, OverlayBlend(A, B), BlendFactor);

		case EPCGExNoiseBlendMode::SoftLight:
			return FMath::Lerp(A, SoftLightBlend(A, B), BlendFactor);

		case EPCGExNoiseBlendMode::First:
			return A > SMALL_NUMBER ? A : B;

		default:
			return A;
		}
	}

	FVector2D FNoiseGenerator::BlendSingle(const EPCGExNoiseBlendMode BlendMode, const FVector2D& A, const FVector2D& B, const double BlendFactor) const
	{
		return FVector2D(
			BlendSingle(BlendMode, A.X, B.X, BlendFactor),
			BlendSingle(BlendMode, A.Y, B.Y, BlendFactor)
		);
	}

	FVector FNoiseGenerator::BlendSingle(const EPCGExNoiseBlendMode BlendMode, const FVector& A, const FVector& B, const double BlendFactor) const
	{
		return FVector(
			BlendSingle(BlendMode, A.X, B.X, BlendFactor),
			BlendSingle(BlendMode, A.Y, B.Y, BlendFactor),
			BlendSingle(BlendMode, A.Z, B.Z, BlendFactor)
		);
	}

	FVector4 FNoiseGenerator::BlendSingle(const EPCGExNoiseBlendMode BlendMode, const FVector4& A, const FVector4& B, const double BlendFactor) const
	{
		return FVector4(
			BlendSingle(BlendMode, A.X, B.X, BlendFactor),
			BlendSingle(BlendMode, A.Y, B.Y, BlendFactor),
			BlendSingle(BlendMode, A.Z, B.Z, BlendFactor),
			BlendSingle(BlendMode, A.W, B.W, BlendFactor)
		);
	}

	//
	// Batch blend - switch OUTSIDE the inner loop for better branch prediction
	//

	void FNoiseGenerator::BlendBatch(const EPCGExNoiseBlendMode BlendMode, double* RESTRICT Out, const double* RESTRICT In, const int32 Count, const double BlendFactor) const
	{
		switch (BlendMode)
		{
		case EPCGExNoiseBlendMode::Blend:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = Out[i] + (In[i] - Out[i]) * BlendFactor;
			}
			break;

		case EPCGExNoiseBlendMode::Add:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], FMath::Clamp(Out[i] + In[i], 0.0, 1.0), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Subtract:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], FMath::Clamp(Out[i] - In[i], 0.0, 1.0), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Multiply:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], Out[i] * In[i], BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Min:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], FMath::Min(Out[i], In[i]), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Max:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], FMath::Max(Out[i], In[i]), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Screen:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], ScreenBlend(Out[i], In[i]), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Overlay:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], OverlayBlend(Out[i], In[i]), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::SoftLight:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], SoftLightBlend(Out[i], In[i]), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::First:
			for (int32 i = 0; i < Count; ++i)
			{
				if (Out[i] <= SMALL_NUMBER) { Out[i] = In[i]; }
			}
			break;
		}
	}

	void FNoiseGenerator::BlendBatch(const EPCGExNoiseBlendMode BlendMode, FVector2D* RESTRICT Out, const FVector2D* RESTRICT In, const int32 Count, const double BlendFactor) const
	{
		switch (BlendMode)
		{
		case EPCGExNoiseBlendMode::Blend:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = Out[i] + (In[i] - Out[i]) * BlendFactor;
			}
			break;

		case EPCGExNoiseBlendMode::Add:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X + In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y + In[i].Y, 0.0, 1.0), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Subtract:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X - In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y - In[i].Y, 0.0, 1.0), BlendFactor);
			}
			break;

		default:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = BlendSingle(BlendMode, Out[i], In[i], BlendFactor);
			}
			break;
		}
	}

	void FNoiseGenerator::BlendBatch(const EPCGExNoiseBlendMode BlendMode, FVector* RESTRICT Out, const FVector* RESTRICT In, const int32 Count, const double BlendFactor) const
	{
		switch (BlendMode)
		{
		case EPCGExNoiseBlendMode::Blend:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = Out[i] + (In[i] - Out[i]) * BlendFactor;
			}
			break;

		case EPCGExNoiseBlendMode::Add:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X + In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y + In[i].Y, 0.0, 1.0), BlendFactor);
				Out[i].Z = FMath::Lerp(Out[i].Z, FMath::Clamp(Out[i].Z + In[i].Z, 0.0, 1.0), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Subtract:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X - In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y - In[i].Y, 0.0, 1.0), BlendFactor);
				Out[i].Z = FMath::Lerp(Out[i].Z, FMath::Clamp(Out[i].Z - In[i].Z, 0.0, 1.0), BlendFactor);
			}
			break;

		default:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = BlendSingle(BlendMode, Out[i], In[i], BlendFactor);
			}
			break;
		}
	}

	void FNoiseGenerator::BlendBatch(const EPCGExNoiseBlendMode BlendMode, FVector4* RESTRICT Out, const FVector4* RESTRICT In, const int32 Count, const double BlendFactor) const
	{
		switch (BlendMode)
		{
		case EPCGExNoiseBlendMode::Blend:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = Out[i] + (In[i] - Out[i]) * BlendFactor;
			}
			break;

		case EPCGExNoiseBlendMode::Add:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X + In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y + In[i].Y, 0.0, 1.0), BlendFactor);
				Out[i].Z = FMath::Lerp(Out[i].Z, FMath::Clamp(Out[i].Z + In[i].Z, 0.0, 1.0), BlendFactor);
				Out[i].W = FMath::Lerp(Out[i].W, FMath::Clamp(Out[i].W + In[i].W, 0.0, 1.0), BlendFactor);
			}
			break;

		case EPCGExNoiseBlendMode::Subtract:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i].X = FMath::Lerp(Out[i].X, FMath::Clamp(Out[i].X - In[i].X, 0.0, 1.0), BlendFactor);
				Out[i].Y = FMath::Lerp(Out[i].Y, FMath::Clamp(Out[i].Y - In[i].Y, 0.0, 1.0), BlendFactor);
				Out[i].Z = FMath::Lerp(Out[i].Z, FMath::Clamp(Out[i].Z - In[i].Z, 0.0, 1.0), BlendFactor);
				Out[i].W = FMath::Lerp(Out[i].W, FMath::Clamp(Out[i].W - In[i].W, 0.0, 1.0), BlendFactor);
			}
			break;

		default:
			for (int32 i = 0; i < Count; ++i)
			{
				Out[i] = BlendSingle(BlendMode, Out[i], In[i], BlendFactor);
			}
			break;
		}
	}

	//
	// Single-point generation
	//

	double FNoiseGenerator::GetDouble(const FVector& Position) const
	{
		if (Operations.IsEmpty()) { return 0.0; }

		double Result = OperationsPtr[0]->GetDouble(Position);

		for (int32 i = 1; i < Operations.Num(); ++i)
		{
			const double Value = OperationsPtr[i]->GetDouble(Position);
			Result = BlendSingle(BlendModes[i], Result, Value, BlendFactors[i]);
		}

		return Result;
	}

	FVector2D FNoiseGenerator::GetVector2D(const FVector& Position) const
	{
		if (Operations.IsEmpty()) { return FVector2D::ZeroVector; }

		FVector2D Result = OperationsPtr[0]->GetVector2D(Position);

		for (int32 i = 1; i < Operations.Num(); ++i)
		{
			const FVector2D Value = OperationsPtr[i]->GetVector2D(Position);
			Result = BlendSingle(BlendModes[i], Result, Value, BlendFactors[i]);
		}

		return Result;
	}

	FVector FNoiseGenerator::GetVector(const FVector& Position) const
	{
		if (Operations.IsEmpty()) { return FVector::ZeroVector; }

		FVector Result = OperationsPtr[0]->GetVector(Position);

		for (int32 i = 1; i < Operations.Num(); ++i)
		{
			const FVector Value = OperationsPtr[i]->GetVector(Position);
			Result = BlendSingle(BlendModes[i], Result, Value, BlendFactors[i]);
		}

		return Result;
	}

	FVector4 FNoiseGenerator::GetVector4(const FVector& Position) const
	{
		if (Operations.IsEmpty()) { return FVector4::Zero(); }

		FVector4 Result = OperationsPtr[0]->GetVector4(Position);

		for (int32 i = 1; i < Operations.Num(); ++i)
		{
			const FVector4 Value = OperationsPtr[i]->GetVector4(Position);
			Result = BlendSingle(BlendModes[i], Result, Value, BlendFactors[i]);
		}

		return Result;
	}

	//
	// Batch generation
	//

	void FNoiseGenerator::Generate(const TArrayView<const FVector> Positions, TArrayView<double> OutResults) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNoiseGenerator::GenerateDouble);

		check(Positions.Num() == OutResults.Num());

		if (Operations.IsEmpty())
		{
			FMemory::Memzero(OutResults.GetData(), OutResults.Num() * sizeof(double));
			return;
		}

		const int32 Count = Positions.Num();

		// Fast path for single operation
		if (Operations.Num() == 1)
		{
			OperationsPtr[0]->Generate(Positions, OutResults);
			return;
		}

		// Multiple operations - need temp storage
		TArray<double> TempBuffer;
		TempBuffer.SetNumUninitialized(Count);

		// First operation
		OperationsPtr[0]->Generate(Positions, OutResults);

		// Blend subsequent operations - switch is outside inner loop
		for (int32 OpIdx = 1; OpIdx < Operations.Num(); ++OpIdx)
		{
			OperationsPtr[OpIdx]->Generate(Positions, TempBuffer);
			BlendBatch(BlendModes[OpIdx], OutResults.GetData(), TempBuffer.GetData(), Count, BlendFactors[OpIdx]);
		}
	}

	void FNoiseGenerator::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNoiseGenerator::GenerateVector2);

		check(Positions.Num() == OutResults.Num());

		if (Operations.IsEmpty())
		{
			FMemory::Memzero(OutResults.GetData(), OutResults.Num() * sizeof(FVector2D));
			return;
		}

		const int32 Count = Positions.Num();

		if (Operations.Num() == 1)
		{
			OperationsPtr[0]->Generate(Positions, OutResults);
			return;
		}

		TArray<FVector2D> TempBuffer;
		TempBuffer.SetNumUninitialized(Count);

		OperationsPtr[0]->Generate(Positions, OutResults);

		for (int32 OpIdx = 1; OpIdx < Operations.Num(); ++OpIdx)
		{
			OperationsPtr[OpIdx]->Generate(Positions, TempBuffer);
			BlendBatch(BlendModes[OpIdx], OutResults.GetData(), TempBuffer.GetData(), Count, BlendFactors[OpIdx]);
		}
	}

	void FNoiseGenerator::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector> OutResults) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNoiseGenerator::GenerateVector);

		check(Positions.Num() == OutResults.Num());

		if (Operations.IsEmpty())
		{
			FMemory::Memzero(OutResults.GetData(), OutResults.Num() * sizeof(FVector));
			return;
		}

		const int32 Count = Positions.Num();

		if (Operations.Num() == 1)
		{
			OperationsPtr[0]->Generate(Positions, OutResults);
			return;
		}

		TArray<FVector> TempBuffer;
		TempBuffer.SetNumUninitialized(Count);

		OperationsPtr[0]->Generate(Positions, OutResults);

		for (int32 OpIdx = 1; OpIdx < Operations.Num(); ++OpIdx)
		{
			OperationsPtr[OpIdx]->Generate(Positions, TempBuffer);
			BlendBatch(BlendModes[OpIdx], OutResults.GetData(), TempBuffer.GetData(), Count, BlendFactors[OpIdx]);
		}
	}

	void FNoiseGenerator::Generate(const TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNoiseGenerator::GenerateVector4);

		check(Positions.Num() == OutResults.Num());

		if (Operations.IsEmpty())
		{
			FMemory::Memzero(OutResults.GetData(), OutResults.Num() * sizeof(FVector4));
			return;
		}

		const int32 Count = Positions.Num();

		if (Operations.Num() == 1)
		{
			OperationsPtr[0]->Generate(Positions, OutResults);
			return;
		}

		TArray<FVector4> TempBuffer;
		TempBuffer.SetNumUninitialized(Count);

		OperationsPtr[0]->Generate(Positions, OutResults);

		for (int32 OpIdx = 1; OpIdx < Operations.Num(); ++OpIdx)
		{
			OperationsPtr[OpIdx]->Generate(Positions, TempBuffer);
			BlendBatch(BlendModes[OpIdx], OutResults.GetData(), TempBuffer.GetData(), Count, BlendFactors[OpIdx]);
		}
	}

	//
	// Parallel batch generation
	//

	void FNoiseGenerator::GenerateParallel(const TArrayView<const FVector> Positions, TArrayView<double> OutResults, const int32 MinBatchSize) const
	{
		check(Positions.Num() == OutResults.Num());

		const int32 Count = Positions.Num();
		if (Count < MinBatchSize * 2 || Operations.IsEmpty())
		{
			Generate(Positions, OutResults);
			return;
		}

		ParallelFor(Count, [&](const int32 Index)
		{
			OutResults[Index] = GetDouble(Positions[Index]);
		}, Count < MinBatchSize);
	}

	void FNoiseGenerator::GenerateParallel(const TArrayView<const FVector> Positions, TArrayView<FVector2D> OutResults, const int32 MinBatchSize) const
	{
		check(Positions.Num() == OutResults.Num());

		const int32 Count = Positions.Num();
		if (Count < MinBatchSize * 2 || Operations.IsEmpty())
		{
			Generate(Positions, OutResults);
			return;
		}

		ParallelFor(Count, [&](const int32 Index)
		{
			OutResults[Index] = GetVector2D(Positions[Index]);
		}, Count < MinBatchSize);
	}

	void FNoiseGenerator::GenerateParallel(const TArrayView<const FVector> Positions, TArrayView<FVector> OutResults, const int32 MinBatchSize) const
	{
		check(Positions.Num() == OutResults.Num());

		const int32 Count = Positions.Num();
		if (Count < MinBatchSize * 2 || Operations.IsEmpty())
		{
			Generate(Positions, OutResults);
			return;
		}

		ParallelFor(Count, [&](const int32 Index)
		{
			OutResults[Index] = GetVector(Positions[Index]);
		}, Count < MinBatchSize);
	}

	void FNoiseGenerator::GenerateParallel(const TArrayView<const FVector> Positions, TArrayView<FVector4> OutResults, const int32 MinBatchSize) const
	{
		check(Positions.Num() == OutResults.Num());

		const int32 Count = Positions.Num();
		if (Count < MinBatchSize * 2 || Operations.IsEmpty())
		{
			Generate(Positions, OutResults);
			return;
		}

		ParallelFor(Count, [&](const int32 Index)
		{
			OutResults[Index] = GetVector4(Positions[Index]);
		}, Count < MinBatchSize);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
