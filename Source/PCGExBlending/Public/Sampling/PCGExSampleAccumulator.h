// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExPointElements.h"
#include "Math/PCGExMathAxis.h"
#include "Types/PCGExTypes.h"

namespace PCGExSampling
{
	struct FSampleEntry
	{
		PCGExData::FElement Target; // IO is the facade position in the targets handler
		double Dist = 0;            // linear
		double Weight = 0;          // resolved by the node before ResolveWeightedPoints
	};

	/**
	 * Per-source geometry fold shared by the target samplers; one instance per parallel scope.
	 * Reset -> collect Entries -> resolve Entry.Weight -> ResolveWeightedPoints -> Add per entry -> Finalize.
	 * Header-only on purpose: Add runs once per sample.
	 */
	class FSampleAccumulator
	{
		const EPCGExAxis SignAxis;
		const EPCGExAxis AngleAxis;
		const EPCGExAxisAlign LookAtAlign;

		double DistanceSum = 0;

	public:
		TArray<FSampleEntry, TInlineAllocator<16>> Entries;
		TArray<PCGExData::FWeightedPoint> WeightedPoints; // what IUnionBlender::Blend consumes; reused across points

		FTransform WeightedTransform = FTransform::Identity;
		FVector WeightedUp = FVector::UpVector;
		FVector WeightedSignAxis = FVector::ZeroVector;
		FVector WeightedAngleAxis = FVector::ZeroVector;
		FVector CWDistance = FVector::ZeroVector;
		FVector LookAt = FVector::ForwardVector;
		FTransform LookAtTransform = FTransform::Identity;
		double Distance = 0; // mean of the Add()'d distances; nodes with another definition overwrite it after Finalize
		double TotalWeight = 0;
		int32 Count = 0;

		FSampleAccumulator(const EPCGExAxis InSignAxis, const EPCGExAxis InAngleAxis, const EPCGExAxisAlign InLookAtAlign)
			: SignAxis(InSignAxis)
			  , AngleAxis(InAngleAxis)
			  , LookAtAlign(InLookAtAlign)
		{
		}

		FORCEINLINE void Reset(const FVector& UpSeed)
		{
			Entries.Reset();
			WeightedPoints.Reset();
			WeightedTransform = FTransform::Identity;
			WeightedTransform.SetScale3D(FVector::ZeroVector);
			WeightedUp = UpSeed;
			WeightedSignAxis = FVector::ZeroVector;
			WeightedAngleAxis = FVector::ZeroVector;
			DistanceSum = 0;
			Distance = 0;
			TotalWeight = 0;
			Count = 0;
		}

		/** Zero-total fallback: all-zero weights still blend, sharing a total of 1 over the split mass. Load-bearing for attribute-weighted sampling. */
		FORCEINLINE void ApplyZeroTotalFallback()
		{
			double Sum = 0;
			double SplitSum = 0;
			for (const PCGExData::FWeightedPoint& P : WeightedPoints)
			{
				Sum += P.Weight;
				SplitSum += P.Split;
			}
			if (Sum == 0 && SplitSum > 0)
			{
				// Uniform over the split mass, not the count: split points (A/B of one sample) still total 1 per sample.
				const double Uniform = 1.0 / SplitSum;
				for (PCGExData::FWeightedPoint& P : WeightedPoints)
				{
					P.Weight = Uniform;
				}
			}
		}

		/** Fills WeightedPoints from Entries (same order) and applies the zero-total fallback to both. */
		FORCEINLINE void ResolveWeightedPoints()
		{
			const int32 Num = Entries.Num();
			WeightedPoints.Reset(Num);
			for (const FSampleEntry& Entry : Entries)
			{
				WeightedPoints.Emplace(Entry.Target.Index, Entry.Weight, Entry.Target.IO);
			}

			ApplyZeroTotalFallback();
			for (int32 i = 0; i < Num; i++)
			{
				Entries[i].Weight = WeightedPoints[i].Weight;
			}
		}

		FORCEINLINE void Add(const FTransform& Target, const double GeometryWeight, const double Dist)
		{
			const FQuat Rotation = Target.GetRotation();
			WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::WeightedAdd(WeightedTransform, Target, GeometryWeight);
			WeightedSignAxis += PCGExMath::GetDirection(Rotation, SignAxis) * GeometryWeight;
			WeightedAngleAxis += PCGExMath::GetDirection(Rotation, AngleAxis) * GeometryWeight;
			TotalWeight += GeometryWeight;
			DistanceSum += Dist;
			Count++;
		}

		/** Target-driven up vector; the caller decides whether the up mode wants it. */
		FORCEINLINE void AddUp(const FVector& Up, const double GeometryWeight)
		{
			WeightedUp = PCGExTypeOps::FTypeOps<FVector>::WeightedAdd(WeightedUp, Up, GeometryWeight);
		}

		FORCEINLINE void Finalize(const FTransform& Fallback, const FVector& Origin)
		{
			if (TotalWeight != 0) // Dodge NaN
			{
				WeightedUp = PCGExTypeOps::FTypeOps<FVector>::NormalizeWeight(WeightedUp, TotalWeight);
				WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::NormalizeWeight(WeightedTransform, TotalWeight);
			}
			else
			{
				WeightedTransform = Fallback;
			}

			WeightedUp.Normalize();
			Distance = Count > 0 ? DistanceSum / static_cast<double>(Count) : 0;

			CWDistance = Origin - WeightedTransform.GetLocation();
			LookAt = CWDistance.GetSafeNormal();
			LookAtTransform = PCGExMath::MakeLookAtTransform(LookAt, WeightedUp, LookAtAlign);
		}
	};
}
