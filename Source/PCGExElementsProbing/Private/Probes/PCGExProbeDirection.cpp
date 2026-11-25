// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeDirection.h"

#include "PCGExH.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExProbingCandidates.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMath.h"

PCGEX_CREATE_PROBE_FACTORY(Direction, {}, {})

#if WITH_EDITOR
void FPCGExProbeConfigDirection::ApplyDeprecation()
{
	FPCGExProbeConfigBase::ApplyDeprecation();
	Direction.Update(DirectionInput_DEPRECATED, DirectionAttribute_DEPRECATED, DirectionConstant_DEPRECATED);
}

void FPCGExProbeConfigDirection::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	FPCGExProbeConfigBase::RenamePins(InSettings, InOutNode);
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("DirectionAttribute")), FName(TEXT("Direction")), FName(TEXT("Attribute")), FName(TEXT("Direction (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("DirectionConstant")), FName(TEXT("Direction")), FName(TEXT("Constant")), FName(TEXT("Direction")));
}
#endif

bool FPCGExProbeDirection::RequiresChainProcessing() const
{
	return Config.bDoChainedProcessing;
}

bool FPCGExProbeDirection::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	bUseBestDot = Config.Favor == EPCGExProbeDirectionPriorization::Dot;
	MinDot = PCGExMath::DegreesToDot(Config.MaxAngle);
	DirectionMultiplier = Config.bInvertDirection ? -1 : 1;

	if (!InitSettingValue(Direction, Config.Direction.GetValueSetting(), PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

#define PCGEX_GET_DIRECTION (Direction->Read(Index) * DirectionMultiplier).GetSafeNormal()

void FPCGExProbeDirection::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	bool bIsAlreadyConnected;
	const double R = GetSearchRadius(Index);
	PCGExProbing::FBestCandidate Best;

	const FTransform& WorkingTransform = *(WorkingTransforms->GetData() + Index);
	const FVector Dir = Config.bTransformDirection ? WorkingTransform.TransformVectorNoScale(PCGEX_GET_DIRECTION) : PCGEX_GET_DIRECTION;

	const int32 NumCandidates = Candidates.Num();
	for (int32 i = 0; i < NumCandidates; i++)
	{
		const PCGExProbing::FCandidate& C = Candidates[i];

		// Candidates are sorted nearest-first.
		if (C.Distance > R)
		{
			break;
		}

		if (Coincidence && Coincidence->Contains(C.GH))
		{
			continue;
		}

		double Dot = 0;
		if (Config.bUseComponentWiseAngle)
		{
			if (!PCGExMath::IsDirectionWithinTolerance(Dir, C.Direction, Config.MaxAngles))
			{
				continue;
			}
			Dot = FVector::DotProduct(Dir, C.Direction);
			if (Config.bUnsignedCheck)
			{
				Dot = FMath::Abs(Dot);
			}
		}
		else
		{
			Dot = FVector::DotProduct(Dir, C.Direction);
			if (Config.bUnsignedCheck)
			{
				Dot = FMath::Abs(Dot);
			}
			if (Dot < MinDot)
			{
				continue;
			}
		}

		Best.ConsiderDirectional(i, C, Dot, bUseBestDot);
	}

	if (Best.BestIndex != -1)
	{
		const PCGExProbing::FCandidate& C = Candidates[Best.BestIndex];

		if (Coincidence)
		{
			Coincidence->Add(C.GH, &bIsAlreadyConnected);
			if (bIsAlreadyConnected)
			{
				return;
			}
		}

		OutEdges->Add(PCGEx::H64U(Index, C.PointIndex));
	}
}

void FPCGExProbeDirection::PrepareBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
	InBestCandidate.BestIndex = -1;
	InBestCandidate.BestPrimaryValue = -1;
	InBestCandidate.BestSecondaryValue = TNumericLimits<double>::Max();
}

void FPCGExProbeDirection::ProcessCandidateChained(const int32 Index, const int32 CandidateIndex, PCGExProbing::FCandidate& Candidate, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
	const double R = GetSearchRadius(Index);

	const FTransform& WorkingTransform = *(WorkingTransforms->GetData() + Index);
	const FVector Dir = Config.bTransformDirection ? WorkingTransform.TransformVectorNoScale(PCGEX_GET_DIRECTION) : PCGEX_GET_DIRECTION;

	if (Candidate.Distance > R)
	{
		return;
	}

	double Dot = 0;
	if (Config.bUseComponentWiseAngle)
	{
		if (!PCGExMath::IsDirectionWithinTolerance(Dir, Candidate.Direction, Config.MaxAngles))
		{
			return;
		}
		Dot = FVector::DotProduct(Dir, Candidate.Direction);
		if (Config.bUnsignedCheck)
		{
			Dot = FMath::Abs(Dot);
		}
	}
	else
	{
		Dot = FVector::DotProduct(Dir, Candidate.Direction);
		if (Config.bUnsignedCheck)
		{
			Dot = FMath::Abs(Dot);
		}
		if (Dot < MinDot)
		{
			return;
		}
	}

	InBestCandidate.ConsiderDirectional(CandidateIndex, Candidate, Dot, bUseBestDot);
}

#undef PCGEX_GET_DIRECTION

void FPCGExProbeDirection::ProcessBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	if (InBestCandidate.BestIndex == -1)
	{
		return;
	}

	const PCGExProbing::FCandidate& C = Candidates[InBestCandidate.BestIndex];

	bool bIsAlreadyConnected;
	if (Coincidence)
	{
		Coincidence->Add(C.GH, &bIsAlreadyConnected);
		if (bIsAlreadyConnected)
		{
			return;
		}
	}

	OutEdges->Add(PCGEx::H64U(Index, C.PointIndex));
}
