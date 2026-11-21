// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeTheta.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(Theta, {}, {})

bool FPCGExProbeTheta::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeTheta::WantsOctree() const
{
	return true;
}

bool FPCGExProbeTheta::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	// Precompute cone bisector directions
	const FVector Axis = Config.ConeAxis.GetSafeNormal();
	FVector Tangent, Bitangent;
	Axis.FindBestAxisVectors(Tangent, Bitangent);

	ConeBisectors.SetNum(Config.NumCones);
	ConeHalfAngle = PI / Config.NumCones;

	for (int32 i = 0; i < Config.NumCones; ++i)
	{
		const float Angle = (2.0f * PI * i) / Config.NumCones;
		ConeBisectors[i] = FMath::Cos(Angle) * Tangent + FMath::Sin(Angle) * Bitangent;
	}

	return true;
}

void FPCGExProbeTheta::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	const float CosConeHalf = FMath::Cos(ConeHalfAngle);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		const FVector& Pos = Positions[i];
		const double MaxDistSq = GetSearchRadius(i);
		const double MaxDist = FMath::Sqrt(MaxDistSq);

		// Track best candidate per cone
		TArray<int32> BestPerCone;
		TArray<double> BestDistPerCone;
		BestPerCone.Init(INDEX_NONE, Config.NumCones);
		BestDistPerCone.Init(TNumericLimits<double>::Max(), Config.NumCones);

		Octree->FindElementsWithBoundsTest(
			FBox(Pos - FVector(MaxDist), Pos + FVector(MaxDist)),
			[&](const PCGExOctree::FItem& Other)
			{
				const int32 j = Other.Index;
				if (i == j || !AcceptConnectionsRef[j])
				{
					return;
				}

				const FVector Delta = Positions[j] - Pos;
				const double DistSq = Delta.SizeSquared();
				if (DistSq > MaxDistSq || DistSq < SMALL_NUMBER)
				{
					return;
				}

				const FVector Dir = Delta.GetUnsafeNormal();

				// Find which cone this point falls into
				for (int32 c = 0; c < Config.NumCones; ++c)
				{
					if (FVector::DotProduct(Dir, ConeBisectors[c]) >= CosConeHalf)
					{
						double EffectiveDist;
						if (Config.bUseYaoVariant)
						{
							// Yao: actual Euclidean distance
							EffectiveDist = DistSq;
						}
						else
						{
							// Theta: projected distance onto cone bisector
							const double Projection = FVector::DotProduct(Delta, ConeBisectors[c]);
							EffectiveDist = Projection * Projection;
						}

						if (EffectiveDist < BestDistPerCone[c])
						{
							BestDistPerCone[c] = EffectiveDist;
							BestPerCone[c] = j;
						}
						break; // Point can only be in one cone
					}
				}
			});

		// Add edges to best in each cone
		for (int32 c = 0; c < Config.NumCones; ++c)
		{
			if (BestPerCone[c] != INDEX_NONE)
			{
				OutEdges.Add(PCGEx::H64U(i, BestPerCone[c]));
			}
		}
	}
}
