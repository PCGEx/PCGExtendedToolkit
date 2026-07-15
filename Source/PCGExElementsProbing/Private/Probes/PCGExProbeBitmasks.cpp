// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExProbeBitmasks.h"
#include "PCGExVersion.h"


#include "PCGExH.h"
#include "Containers/PCGExManagedObjects.h"

#include "Core/PCGExProbingCandidates.h"
#include "Data/Bitmasks/PCGExBitmaskData.h"

PCGEX_CREATE_PROBE_FACTORY(
	Bitmasks,
	{ NewFactory->BitmaskData = PCGExBitmask::FBitmaskData::Make(Config.Collections, Config.Compositions, Config.Angle); },
	{ NewOperation->BitmaskData = BitmaskData; }
	)

#if WITH_EDITOR
void UPCGExProbeBitmasksProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeBitmasksProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

namespace PCGExProbeBitmasks
{
	FScopedContainer::FScopedContainer(const PCGExMT::FScope& InScope)
		: PCGExMT::FScopedContainer(InScope)
	{
	}

	void FScopedContainer::Init(const TSharedPtr<PCGExBitmask::FBitmaskData>& BitmaskData, const bool bCopyDirs)
	{
		const int32 NumDirs = BitmaskData->Directions.Num();
		BestDotsBuffer.SetNumUninitialized(NumDirs);
		BestDistsBuffer.SetNumUninitialized(NumDirs);
		BestIdxBuffer.SetNumUninitialized(NumDirs);

		if (bCopyDirs)
		{
			WorkingDirs = BitmaskData->Directions;
		}
		else
		{
			WorkingDirs.SetNumUninitialized(NumDirs);
		}
	}

	void FScopedContainer::Reset()
	{
		for (int i = 0; i < BestDotsBuffer.Num(); i++)
		{
			BestDotsBuffer[i] = -1;
			BestDistsBuffer[i] = TNumericLimits<double>::Max();
			BestIdxBuffer[i] = -1;
		}
	}
}

TSharedPtr<PCGExMT::FScopedContainer> FPCGExProbeBitmasks::GetScopedContainer(const PCGExMT::FScope& InScope) const
{
	TSharedPtr<PCGExProbeBitmasks::FScopedContainer> ScopedContainer = MakeShared<PCGExProbeBitmasks::FScopedContainer>(InScope);
	ScopedContainer->Init(BitmaskData, Config.bTransformDirection); // dir count
	return ScopedContainer;
}

bool FPCGExProbeBitmasks::RequiresChainProcessing() const
{
	return false;
}

bool FPCGExProbeBitmasks::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	/*
	bUseBestDot = Config.Favor == EPCGExProbeBitmasksPriorization::Dot;
	MinDot = PCGExMath::DegreesToDot(Config.MaxAngle);
	DirectionMultiplier = Config.bInvertDirection ? -1 : 1;

	Direction = Config.GetValueSettingDirection();
	if (!Direction->Init(PrimaryDataFacade)) { return false; }
	*/

	return true;
}

#define PCGEX_GET_DIRECTION (Direction->Read(Index) * DirectionMultiplier).GetSafeNormal()

void FPCGExProbeBitmasks::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
	const int32 DirCount = BitmaskData->Directions.Num();
	if (DirCount == 0)
	{
		return;
	}

	PCGExProbeBitmasks::FScopedContainer* LocalContainer = static_cast<PCGExProbeBitmasks::FScopedContainer*>(Container);
	LocalContainer->Reset();

	TArray<double>& BestDotsBuffer = LocalContainer->BestDotsBuffer; // caller-provided scratch buffers
	TArray<double>& BestDistsBuffer = LocalContainer->BestDistsBuffer;
	TArray<int32>& BestIdxBuffer = LocalContainer->BestIdxBuffer;
	TArray<FVector>& WorkingDirs = LocalContainer->WorkingDirs;

	const TArray<double>& DotThresholds = BitmaskData->Dots;

	// precompute world dirs
	if (Config.bTransformDirection)
	{
		const FTransform& WorkingTransform = *(WorkingTransforms->GetData() + Index);
		const TArray<FVector>& Directions = BitmaskData->Directions;
		for (int32 d = 0; d < DirCount; ++d)
		{
			WorkingDirs[d] = WorkingTransform.TransformVectorNoScale(Directions[d]);
		}
	}

	const double R = GetSearchRadius(Index);
	const int32 MaxIndex = Candidates.Num() - 1;
	PCGExProbing::FCandidate* CandData = Candidates.GetData();

	for (int i = 0; i <= MaxIndex; ++i)
	{
		const int32 LocalIndex = bUseBestDot ? (MaxIndex - i) : i;
		const PCGExProbing::FCandidate& C = CandData[LocalIndex];

		if (bUseBestDot)
		{
			if (C.Distance > R)
			{
				continue;
			}
		}
		else
		{
			if (C.Distance > R)
			{
				break;
			}
		}

		if (Coincidence && Coincidence->Contains(C.GH))
		{
			continue;
		}

		const double CandDist = C.Distance;
		const FVector& CandDir = C.Direction;

		// inner loop
		for (int32 d = 0; d < DirCount; ++d)
		{
			const double dot = FVector::DotProduct(WorkingDirs[d], CandDir);
			if (dot < DotThresholds[d])
			{
				continue;
			}

			if (dot >= BestDotsBuffer[d] && CandDist < BestDistsBuffer[d])
			{
				BestDotsBuffer[d] = dot;
				BestDistsBuffer[d] = CandDist;
				BestIdxBuffer[d] = LocalIndex;
			}
		}
	}

	for (int32 d = 0; d < DirCount; ++d)
	{
		const int32 CI = BestIdxBuffer[d];
		if (CI < 0)
		{
			continue;
		}

		const PCGExProbing::FCandidate& C = CandData[CI];

		if (Coincidence)
		{
			bool bAlready;
			Coincidence->Add(C.GH, &bAlready);
			if (bAlready)
			{
				continue;
			}
		}

		OutEdges->Add(PCGEx::H64U(Index, C.PointIndex));
	}
}

#undef PCGEX_GET_DIRECTION

#if WITH_EDITOR
FString UPCGExProbeBitmasksProviderSettings::GetDisplayName() const
{
	return TEXT("");
	/*
	return GetDefaultNodeName().ToString()
		+ TEXT(" @ ")
		+ FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
		*/
}
#endif
