// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeTensor.h"

#include "PCGExH.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExProbingCandidates.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Math/PCGExMath.h"

#if WITH_EDITOR
void FPCGExProbeConfigTensor::ApplyDeprecation()
{
	FPCGExProbeConfigBase::ApplyDeprecation();
	TensorHandlerDetails.ApplyDeprecation();
}

void FPCGExProbeConfigTensor::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	FPCGExProbeConfigBase::RenamePins(InSettings, InOutNode);
	TensorHandlerDetails.RenamePins(InSettings, InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExProbeTensorProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FACTORIES(PCGExTensor::SourceTensorsLabel, "Tensors", Required, FPCGExDataTypeInfoTensor::AsId())
	return PinProperties;
}

PCGEX_CREATE_PROBE_FACTORY(Tensor, {}, { NewOperation->TensorFactories = &TensorFactories; })

PCGExFactories::EPreparationResult UPCGExProbeFactoryTensor::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	if (!PCGExFactories::GetInputFactories(InContext, PCGExTensor::SourceTensorsLabel, TensorFactories, {FPCGExDataTypeInfoTensor::AsId()}))
	{
		return PCGExFactories::EPreparationResult::Fail;
	}

	if (TensorFactories.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing tensors."))
		return PCGExFactories::EPreparationResult::Fail;
	}
	return Result;
}

bool FPCGExProbeTensor::RequiresChainProcessing() const
{
	return Config.bDoChainedProcessing;
}

TSharedPtr<PCGExMT::FScopedContainer> FPCGExProbeTensor::GetScopedContainer(const PCGExMT::FScope& InScope) const
{
	if (!Config.bDoChainedProcessing)
	{
		return nullptr;
	}
	return MakeShared<PCGExProbeTensor::FScopedContainer>(InScope);
}

bool FPCGExProbeTensor::Prepare(FPCGExContext* InContext)
{
	if (!TensorFactories)
	{
		return false;
	}

	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	bUseBestDot = (Config.Favor == EPCGExProbeDirectionPriorization::Dot);
	MinDot = PCGExMath::DegreesToDot(Config.MaxAngle);
	Mirror = Config.bInvertTensorDirection ? -1 : 1;

	TensorsHandler = MakeShared<PCGExTensor::FTensorsHandler>(Config.TensorHandlerDetails);
	if (!TensorsHandler->Init(Context, *TensorFactories, PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

void FPCGExProbeTensor::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	bool bIsAlreadyConnected;
	const double R = GetSearchRadius(Index);
	PCGExProbing::FBestCandidate Best;

	bool bSuccess = false;
	const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(Index, *(WorkingTransforms->GetData() + Index), bSuccess);

	if (!bSuccess)
	{
		return;
	}

	const FVector Dir = Sample.DirectionAndSize.GetSafeNormal() * Mirror;
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
		}
		else
		{
			Dot = FVector::DotProduct(Dir, C.Direction);
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

void FPCGExProbeTensor::PrepareBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
	InBestCandidate.BestIndex = -1;
	InBestCandidate.BestPrimaryValue = -1;
	InBestCandidate.BestSecondaryValue = TNumericLimits<double>::Max();

	// New seed: its sample is taken by the first in-radius candidate.
	Container->Reset();
}

void FPCGExProbeTensor::ProcessCandidateChained(const int32 Index, const int32 CandidateIndex, PCGExProbing::FCandidate& Candidate, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
	if (Candidate.Distance > GetSearchRadius(Index))
	{
		return;
	}

	// The sample only depends on the seed: one per seed, not one per candidate.
	PCGExProbeTensor::FScopedContainer* SeedSample = static_cast<PCGExProbeTensor::FScopedContainer*>(Container);
	if (!SeedSample->bSampled)
	{
		SeedSample->bSampled = true;
		SeedSample->bSuccess = false;
		const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(Index, *(WorkingTransforms->GetData() + Index), SeedSample->bSuccess);
		SeedSample->Direction = Sample.DirectionAndSize.GetSafeNormal() * Mirror;
	}

	if (!SeedSample->bSuccess)
	{
		return;
	}

	const FVector& Dir = SeedSample->Direction;

	double Dot = 0;
	if (Config.bUseComponentWiseAngle)
	{
		if (!PCGExMath::IsDirectionWithinTolerance(Dir, Candidate.Direction, Config.MaxAngles))
		{
			return;
		}
		Dot = FVector::DotProduct(Dir, Candidate.Direction);
	}
	else
	{
		Dot = FVector::DotProduct(Dir, Candidate.Direction);
		if (Dot < MinDot)
		{
			return;
		}
	}

	InBestCandidate.ConsiderDirectional(CandidateIndex, Candidate, Dot, bUseBestDot);
}

void FPCGExProbeTensor::ProcessBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
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
