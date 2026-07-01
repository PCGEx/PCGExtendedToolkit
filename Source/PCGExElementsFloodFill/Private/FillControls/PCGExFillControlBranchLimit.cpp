// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlBranchLimit.h"


#include "HAL/PlatformAtomics.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

bool FPCGExFillControlBranchLimit::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	// Mode & bSourceIsVtx were cached on the operation in CreateOperation.
	const UPCGExFillControlsFactoryBranchLimit* TypedFactory = Cast<UPCGExFillControlsFactoryBranchLimit>(Factory);

	MaxBranchesValue = TypedFactory->Config.MaxBranches.GetValueSetting();
	if (!MaxBranchesValue->Init(GetSourceFacade()))
	{
		return false;
	}

	// Hoist a constant / data-domain budget so the hot path skips the per-call virtual Read.
	if (MaxBranchesValue->IsConstant())
	{
		bConstantBudget = true;
		ConstantBudget = MaxBranchesValue->Read(0);
	}

	// Allocate state by enforcement strategy: Vtx+Reroute needs none (probe-time); Vtx+Prune
	// tracks per-node child counts; Seed shares one fork budget per diffusion.
	if (bSourceIsVtx)
	{
		if (Mode == EPCGExFloodFillBranchMode::Prune)
		{
			ChildCounts.Init(0, Cluster->Nodes->Num());
		}
	}
	else
	{
		ParentHasChild.Init(0, Cluster->Nodes->Num());
		DiffusionForks.Init(0, Handler->GetNumDiffusions());
	}

	return true;
}

int32 FPCGExFillControlBranchLimit::ReadBudget(const int32 Index)
{
	const int32 Raw = bConstantBudget ? ConstantBudget : MaxBranchesValue->Read(Index);
	// Clamp so 1+budget can't overflow or hit the MAX_int32 'unlimited' sentinel.
	return FMath::Clamp(Raw, 0, MAX_int32 - 2);
}

#pragma region Capture-time enforcement

bool FPCGExFillControlBranchLimit::ChecksCapture() const
{
	return !UsesProbeFanout();
}

bool FPCGExFillControlBranchLimit::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	const int32 ParentNode = Candidate.Link.Node;
	if (ParentNode < 0)
	{
		// Seed/root has no parent to budget against.
		return true;
	}

	if (bSourceIsVtx)
	{
		// Per-vtx: a node may gain up to (1 + budget) children, i.e. branch 'budget' times.
		return FPlatformAtomics::AtomicRead(&ChildCounts[ParentNode]) <= ReadBudget(Cluster->GetNodePointIndex(ParentNode));
	}

	// Seed (global): first child is free; any beyond it is a fork drawing from the shared budget.
	if (FPlatformAtomics::AtomicRead(&ParentHasChild[ParentNode]) == 0)
	{
		return true;
	}
	return DiffusionForks[Diffusion->Index] < ReadBudget(GetSettingsIndex(Diffusion));
}

bool FPCGExFillControlBranchLimit::WantsCaptureNotify() const
{
	return !UsesProbeFanout();
}

void FPCGExFillControlBranchLimit::OnCaptured(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	const int32 ParentNode = Candidate.Link.Node;
	if (ParentNode < 0)
	{
		return;
	}

	if (bSourceIsVtx)
	{
		FPlatformAtomics::InterlockedIncrement(&ChildCounts[ParentNode]);
		return;
	}

	// First child free; subsequent children spend a fork. Atomic test-and-set: whoever flips 0->1 is the
	// first child (free); everyone that finds it already 1 spends a fork -- correct under overlapping diffusions.
	if (FPlatformAtomics::InterlockedCompareExchange(&ParentHasChild[ParentNode], 1, 0) != 0) { DiffusionForks[Diffusion->Index]++; }
}

#pragma endregion

#pragma region Probe-time enforcement

bool FPCGExFillControlBranchLimit::LimitsProbeFanout() const
{
	return UsesProbeFanout();
}

int32 FPCGExFillControlBranchLimit::GetProbeFanoutLimit(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From)
{
	// A node may spread to up to (1 + budget) children; the probe keeps the best by score (see FDiffusion::Probe).
	return 1 + ReadBudget(From.Node->PointIndex);
}

#pragma endregion

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryBranchLimit::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlBranchLimit)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	// Cache mode & source here so the capability predicates (called during BuildFrom, before PrepareForDiffusions) have one source of truth.
	NewOperation->Mode = Config.Mode;
	NewOperation->bSourceIsVtx = Config.Source == EPCGExFloodFillSettingSource::Vtx;
	return NewOperation;
}

void UPCGExFillControlsFactoryBranchLimit::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	// The preloader targets the cluster's vtx facade, so only Vtx-source attributes preload here.
	if (Config.Source == EPCGExFloodFillSettingSource::Vtx)
	{
		Config.MaxBranches.RegisterBufferDependencies(InContext, FacadePreloader);
	}
}

UPCGExFactoryData* UPCGExFillControlsBranchLimitProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryBranchLimit* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryBranchLimit>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExFillControlsBranchLimitProviderSettings::GetDisplayName() const
{
	FString DName = GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC"));
	DName += Config.Mode == EPCGExFloodFillBranchMode::Reroute ? TEXT(" - Reroute @ ") : TEXT(" - Prune @ ");
	DName += Config.MaxBranches.Input == EPCGExInputValueType::Constant
		? FString::FromInt(Config.MaxBranches.Constant)
		: TEXT("attr");
	return DName;
}
#endif
