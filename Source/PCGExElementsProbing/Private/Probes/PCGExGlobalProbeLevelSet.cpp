// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeLevelSet.h"
#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(LevelSet, {}, {})

#if WITH_EDITOR
void UPCGExProbeLevelSetProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeLevelSetProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeLevelSet::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeLevelSet::WantsOctree() const
{
	return true;
}

bool FPCGExProbeLevelSet::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	LevelBuffer = PrimaryDataFacade->GetBroadcaster<double>(Config.LevelAttribute, true, false);
	if (!LevelBuffer)
	{
		return false;
	}

	// Find min/max for normalization
	if (Config.bNormalizeLevels)
	{
		LevelMin = TNumericLimits<double>::Max();
		LevelMax = TNumericLimits<double>::Lowest();
		const int32 NumPoints = WorkingPositions->Num();
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const double Val = LevelBuffer->Read(i);
			LevelMin = FMath::Min(LevelMin, Val);
			LevelMax = FMath::Max(LevelMax, Val);
		}
	}

	return true;
}

void FPCGExProbeLevelSet::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	const double LevelRange = Config.bNormalizeLevels ? (LevelMax - LevelMin) : 1.0;
	const double NormFactor = LevelRange > SMALL_NUMBER ? 1.0 / LevelRange : 1.0;

	auto GetNormalizedLevel = [&](int32 Idx) -> double
	{
		const double Raw = LevelBuffer->Read(Idx);
		return Config.bNormalizeLevels ? (Raw - LevelMin) * NormFactor : Raw;
	};

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		const FVector& Pos = Positions[i];
		const double Level = GetNormalizedLevel(i);
		const double MaxDistSq = GetSearchRadius(i);
		const double MaxDist = FMath::Sqrt(MaxDistSq);

		// Collect candidates within level tolerance
		TArray<TPair<double, int32>> Candidates;

		Octree->FindElementsWithBoundsTest(
			FBox(Pos - FVector(MaxDist), Pos + FVector(MaxDist)),
			[&](const PCGExOctree::FItem& Other)
			{
				const int32 j = Other.Index;
				if (i == j || !AcceptConnectionsRef[j])
				{
					return;
				}

				const double DistSq = FVector::DistSquared(Pos, Positions[j]);
				if (DistSq > MaxDistSq)
				{
					return;
				}

				const double OtherLevel = GetNormalizedLevel(j);
				const double LevelDiff = FMath::Abs(Level - OtherLevel);

				if (LevelDiff <= Config.MaxLevelDifference)
				{
					// Sort by combined distance and level similarity
					const double Score = DistSq + FMath::Square(LevelDiff * 100.0);
					Candidates.Add({Score, j});
				}
			});

		// Sort and take best K
		Algo::Sort(Candidates, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});

		const int32 MaxConnections = FMath::Min(Config.MaxConnectionsPerPoint, Candidates.Num());
		for (int32 k = 0; k < MaxConnections; ++k)
		{
			OutEdges.Add(PCGEx::H64U(i, Candidates[k].Value));
		}
	}
}
