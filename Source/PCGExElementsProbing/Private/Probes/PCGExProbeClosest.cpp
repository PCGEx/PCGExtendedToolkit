// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeClosest.h"


#include "PCGExH.h"
#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"

#include "Core/PCGExProbingCandidates.h"
#include "Details/PCGExSettingsDetails.h"

PCGEX_CREATE_PROBE_FACTORY(Closest, {}, {})

#if WITH_EDITOR
void FPCGExProbeConfigClosest::ApplyDeprecation()
{
	FPCGExProbeConfigBase::ApplyDeprecation();
	MaxConnections.Update(MaxConnectionsInput_DEPRECATED, MaxConnectionsAttribute_DEPRECATED, MaxConnectionsConstant_DEPRECATED);
}

void FPCGExProbeConfigClosest::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	FPCGExProbeConfigBase::RenamePins(InSettings, InOutNode);
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxConnectionsAttribute")), FName(TEXT("MaxConnections")), FName(TEXT("Attribute")), FName(TEXT("Max Connections (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxConnectionsConstant")), FName(TEXT("MaxConnections")), FName(TEXT("Constant")), FName(TEXT("Max Connections")));
}
#endif

bool FPCGExProbeClosest::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	MaxConnections = Config.MaxConnections.GetValueSetting();
	if (!MaxConnections->Init(PrimaryDataFacade))
	{
		return false;
	}

	CWCoincidenceTolerance = FVector(PCGEx::SafeScalarTolerance(Config.CoincidencePreventionTolerance));

	return true;
}

void FPCGExProbeClosest::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	bool bIsAlreadyConnected;
	const int32 MaxIterations = FMath::Min(MaxConnections->Read(Index), Candidates.Num());
	const double R = GetSearchRadius(Index);

	if (MaxIterations <= 0)
	{
		return;
	}

	TSet<uint64> LocalCoincidence;
	int32 Additions = 0;

	for (PCGExProbing::FCandidate& C : Candidates)
	{
		if (C.Distance > R)
		{
			return;
		} // Candidates are sorted, stop there.

		if (Coincidence)
		{
			Coincidence->Add(C.GH, &bIsAlreadyConnected);
			if (bIsAlreadyConnected)
			{
				continue;
			}
		}

		if (Config.bPreventCoincidence)
		{
			LocalCoincidence.Add(PCGEx::SH3(C.Direction, CWCoincidenceTolerance), &bIsAlreadyConnected);
			if (bIsAlreadyConnected)
			{
				continue;
			}
		}

		OutEdges->Add(PCGEx::H64U(Index, C.PointIndex));

		Additions++;
		if (Additions >= MaxIterations)
		{
			return;
		}
	}
}

#if WITH_EDITOR
void UPCGExProbeClosestProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeClosestProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExProbeClosestProviderSettings::GetDisplayName() const
{
	return TEXT("");
	/*
	return GetDefaultNodeName().ToString()
		+ TEXT(" @ ")
		+ FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
		*/
}
#endif
