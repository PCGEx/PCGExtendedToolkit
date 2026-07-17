// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeAnisotropic.h"
#include "PCGExVersion.h"


#include "PCGExH.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExProbingCandidates.h"
#include "Math/PCGExMath.h"

PCGEX_CREATE_PROBE_FACTORY(Anisotropic, {}, {})

#if WITH_EDITOR
void UPCGExProbeAnisotropicProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeAnisotropicProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeAnisotropic::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}
	MinDot = PCGExMath::DegreesToDot(Config.MaxAngle);
	return true;
}

void FPCGExProbeAnisotropic::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	bool bIsAlreadyConnected;
	const double R = GetSearchRadius(Index);

	FVector D[16];
	int32 BestCandidate[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,};
	double BestDot[16];
	if (Config.bTransformDirection)
	{
		for (int d = 0; d < 16; d++)
		{
			D[d] = Directions[d];
			BestDot[d] = MinDot;
		}
	}
	else
	{
		const FTransform& WorkingTransform = *(WorkingTransforms->GetData() + Index);

		for (int d = 0; d < 16; d++)
		{
			D[d] = WorkingTransform.TransformVectorNoScale(Directions[d]);
			BestDot[d] = MinDot;
		}
	}

	const int32 NumCandidates = Candidates.Num();
	for (int i = 0; i < NumCandidates; i++)
	{
		const PCGExProbing::FCandidate& C = Candidates[i];

		if (C.Distance > R)
		{
			continue;
		}

		if (Coincidence && Coincidence->Contains(C.GH))
		{
			continue;
		}

		int32 BestIndex = -1;

		for (int d = 0; d < 16; d++)
		{
			const double TempDot = FVector::DotProduct(D[d], C.Direction);
			if (TempDot > BestDot[d])
			{
				BestIndex = d;
				BestDot[d] = TempDot;
			}
		}

		if (BestIndex != -1)
		{
			if (Coincidence)
			{
				Coincidence->Add(C.GH, &bIsAlreadyConnected);
				if (bIsAlreadyConnected)
				{
					continue;
				}
			}

			BestCandidate[BestIndex] = i;
		}
	}

	for (int d = 0; d < 16; d++)
	{
		if (BestCandidate[d] == -1)
		{
			continue;
		}
		const PCGExProbing::FCandidate& C = Candidates[BestCandidate[d]];
		OutEdges->Add(PCGEx::H64U(Index, C.PointIndex));
	}
}

#if WITH_EDITOR
FString UPCGExProbeAnisotropicProviderSettings::GetDisplayName() const
{
	return TEXT("");
	/*
	return GetDefaultNodeName().ToString()
		+ TEXT(" @ ")
		+ FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
		*/
}
#endif
