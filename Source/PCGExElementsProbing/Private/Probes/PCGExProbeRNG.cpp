// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeRNG.h"
#include "PCGExVersion.h"

#include "PCGExH.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExProbingCandidates.h"

PCGEX_CREATE_PROBE_FACTORY(RNG, {}, {})

#if WITH_EDITOR
void UPCGExProbeRNGProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeRNGProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeRNG::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}
	HalfBeta = Config.Beta * 0.5;
	HalfBetaSq = HalfBeta * HalfBeta;
	return true;
}

void FPCGExProbeRNG::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	const double R = GetSearchRadius(Index);
	const TArray<FVector>& Positions = *WorkingPositions;
	const FVector& Pi = Positions[Index];

	if (FMath::IsNearlyEqual(Config.Beta, 2.0))
	{
		// Optimized RNG path (β=2): C1=Pj, C2=Pi, radius=d_ij.
		// C2 check reduces to d_ik < d_ij, which is guaranteed by the inner break --
		// so only one DistSquared per inner iteration is needed.
		for (int32 c = 0; c < Candidates.Num(); ++c)
		{
			const PCGExProbing::FCandidate& Cj = Candidates[c];
			if (Cj.Distance > R)
			{
				break;
			}

			const double DijSq = Cj.Distance * Cj.Distance;
			const FVector& Pj = Positions[Cj.PointIndex];

			bool bBlocked = false;
			for (int32 k = 0; k < c; ++k)
			{
				const PCGExProbing::FCandidate& Ck = Candidates[k];
				if (Ck.Distance >= Cj.Distance)
				{
					break;
				}

				if (FVector::DistSquared(Pj, Positions[Ck.PointIndex]) < DijSq)
				{
					bBlocked = true;
					break;
				}
			}

			if (!bBlocked)
			{
				OutEdges->Add(PCGEx::H64U(Index, Cj.PointIndex));
			}
		}
	}
	else
	{
		// General β-skeleton path.
		// β-skeleton sphere centers: C1 = Pi + HalfBeta*(Pj-Pi), C2 = Pj + HalfBeta*(Pi-Pj)
		// Edge (i,j) blocked if any k lies inside both spheres.
		// For any β ∈ [1,2], a blocking k must satisfy d_ik < d_ij (triangle inequality on C2),
		// so we only examine candidates with Ck.Distance < Dij.
		for (int32 c = 0; c < Candidates.Num(); ++c)
		{
			const PCGExProbing::FCandidate& Cj = Candidates[c];
			if (Cj.Distance > R)
			{
				break;
			}

			const double Dij = Cj.Distance;
			const double RadiusSq = HalfBetaSq * Dij * Dij;
			const FVector& Pj = Positions[Cj.PointIndex];
			const FVector C1 = Pi + HalfBeta * (Pj - Pi);
			const FVector C2 = Pj + HalfBeta * (Pi - Pj);

			bool bBlocked = false;
			for (int32 k = 0; k < c; ++k)
			{
				const PCGExProbing::FCandidate& Ck = Candidates[k];
				if (Ck.Distance >= Dij)
				{
					break;
				}

				const FVector& Pk = Positions[Ck.PointIndex];
				if (FVector::DistSquared(C1, Pk) < RadiusSq && FVector::DistSquared(C2, Pk) < RadiusSq)
				{
					bBlocked = true;
					break;
				}
			}

			if (!bBlocked)
			{
				OutEdges->Add(PCGEx::H64U(Index, Cj.PointIndex));
			}
		}
	}
}

#if WITH_EDITOR
FString UPCGExProbeRNGProviderSettings::GetDisplayName() const
{
	const double B = Config.Beta;
	if (FMath::IsNearlyEqual(B, 2.0))
	{
		return TEXT("RNG");
	}
	if (FMath::IsNearlyEqual(B, 1.0))
	{
		return TEXT("Gabriel");
	}
	return FString::Printf(TEXT("β = %.2f"), B);
}
#endif
