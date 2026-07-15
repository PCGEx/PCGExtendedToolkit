// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeAnisotropic.h"
#include "PCGExVersion.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(GlobalAnisotropic, {}, {})

#if WITH_EDITOR
void UPCGExProbeGlobalAnisotropicProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeGlobalAnisotropicProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeGlobalAnisotropic::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeGlobalAnisotropic::WantsOctree() const
{
	return true;
}

bool FPCGExProbeGlobalAnisotropic::Prepare(FPCGExContext* InContext)
{
	return FPCGExProbeOperation::Prepare(InContext);
}

FMatrix FPCGExProbeGlobalAnisotropic::BuildTransformMatrix(const FVector& Primary, const FVector& Secondary) const
{
	const FVector P = Primary.GetSafeNormal();
	const FVector S = (Secondary - P * FVector::DotProduct(Secondary, P)).GetSafeNormal();
	const FVector T = FVector::CrossProduct(P, S);

	// Build inverse scale matrix in local space
	// Points are transformed: local = M * world, then scaled
	FMatrix Rotation = FMatrix::Identity;
	Rotation.SetAxes(&P, &S, &T);

	FMatrix Scale = FMatrix::Identity;
	Scale.M[0][0] = 1.0 / Config.PrimaryScale;
	Scale.M[1][1] = 1.0 / Config.SecondaryScale;
	Scale.M[2][2] = 1.0 / Config.TertiaryScale;

	return Scale * Rotation.GetTransposed();
}

double FPCGExProbeGlobalAnisotropic::ComputeGlobalAnisotropicDistSq(const FVector& Delta, const FMatrix& Transform) const
{
	const FVector Transformed = Transform.TransformVector(Delta);
	return Transformed.SizeSquared();
}

void FPCGExProbeGlobalAnisotropic::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// Precompute global transform if not using per-point normals
	const FMatrix GlobalTransform = BuildTransformMatrix(Config.PrimaryAxis, Config.SecondaryAxis);

	// Determine max isotropic search radius (conservative estimate)
	const double MaxScale = FMath::Max3(Config.PrimaryScale, Config.SecondaryScale, Config.TertiaryScale);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		const FVector& Pos = Positions[i];
		double BaseRadius = FMath::Sqrt(GetSearchRadius(i));
		const double LocalSearchRadius = BaseRadius * MaxScale;
		const double LocalSearchRadiusSq = FMath::Square(LocalSearchRadius);

		// Get transform for this point
		FMatrix Transform = GlobalTransform;
		if (Config.bUsePerPointNormal)
		{
			// TODO: Get per-point normal from attribute
			// For now, use point's local transform if available
			// Transform = BuildTransformMatrix(PerPointNormal[i], Config.SecondaryAxis);
		}

		// Collect candidates with GlobalAnisotropic distance
		TArray<TPair<double, int32>> Candidates;

		Octree->FindElementsWithBoundsTest(
			FBox(Pos - FVector(LocalSearchRadius), Pos + FVector(LocalSearchRadius)),
			[&](const PCGExOctree::FItem& Other)
			{
				const int32 j = Other.Index;
				if (i == j || !AcceptConnectionsRef[j])
				{
					return;
				}

				const FVector Delta = Positions[j] - Pos;

				// Quick isotropic pre-filter
				if (Delta.SizeSquared() > LocalSearchRadiusSq)
				{
					return;
				}

				const double AnisoDist = ComputeGlobalAnisotropicDistSq(Delta, Transform);
				if (AnisoDist <= BaseRadius)
				{
					Candidates.Add({AnisoDist, j});
				}
			});

		// Sort and take K nearest
		Algo::Sort(Candidates, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});

		const int32 K = FMath::Min(Config.K, Candidates.Num());
		for (int32 k = 0; k < K; ++k)
		{
			OutEdges.Add(PCGEx::H64U(i, Candidates[k].Value));
		}
	}
}
