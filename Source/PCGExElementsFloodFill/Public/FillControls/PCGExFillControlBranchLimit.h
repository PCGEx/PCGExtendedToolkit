// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "UObject/Object.h"

#include "PCGExFillControlBranchLimit.generated.h"

UENUM()
enum class EPCGExFloodFillBranchMode : uint8
{
	Prune   = 0 UMETA(DisplayName = "Prune", ToolTip="Hard-cap branching at capture time; excess branches are dropped (fewer, longer lanes, but some nodes may go uncaptured)."),
	Reroute = 1 UMETA(DisplayName = "Reroute", ToolTip="Cap branching at probe time, leaving excess neighbors for other nodes to claim -- preserves coverage at a slight cost."),
};

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigBranchLimit : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigBranchLimit()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false; // The diffusion step is implied by Mode.
	}

	/** How the branch limit is enforced. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExFloodFillBranchMode Mode = EPCGExFloodFillBranchMode::Prune;

	/** Max times the fill may branch (children beyond the first); 0 = single lane. Scope follows Source: per-vtx, or global to the seed's whole diffusion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Max Branches"))
	FPCGExInputShorthandSelectorInteger32Abs MaxBranches = FPCGExInputShorthandSelectorInteger32Abs(FName("MaxBranches"), 0);
};

/**
 * Limits how many times the fill may branch during diffusion. Prune drops excess
 * branches at capture time (may leave nodes uncaptured); Reroute caps fan-out at probe
 * time but leaves excess neighbors for other nodes (preserves coverage). Budget scope
 * follows Source: Vtx per-node, Seed global to the diffusion. Always per-diffusion --
 * a node skipped by one seed stays reachable by others.
 */
class FPCGExFillControlBranchLimit : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryBranchLimit;

public:
	virtual bool PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler) override;

	// Capture-time enforcement (all but Vtx+Reroute). Vtx: per-node child counts. Seed: shared per-diffusion fork budget.
	virtual bool ChecksCapture() const override;
	virtual bool IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate) override;
	virtual bool WantsCaptureNotify() const override;
	virtual void OnCaptured(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate) override;

	// Probe-time enforcement (Vtx+Reroute only): cap fan-out, keep the best children by score, leave the rest for other nodes.
	virtual bool LimitsProbeFanout() const override;
	virtual int32 GetProbeFanoutLimit(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From) override;

	virtual bool ChecksProbe() const override
	{
		return false;
	}

	virtual bool ChecksCandidate() const override
	{
		return false;
	}

protected:
	// Vtx+Reroute limits fan-out at probe time; every other combination enforces at capture time.
	FORCEINLINE bool UsesProbeFanout() const { return Mode == EPCGExFloodFillBranchMode::Reroute && bSourceIsVtx; }

	// Per-parent branch budget, clamped to [0, MAX_int32-2] so 1+budget never overflows or hits the 'unlimited' sentinel. Cached when constant.
	int32 ReadBudget(int32 Index);

	TSharedPtr<PCGExDetails::TSettingValue<int32>> MaxBranchesValue;
	bool bConstantBudget = false;
	int32 ConstantBudget = 0;

	// Captured children per node (Prune + Vtx). Overlapping diffusions may write the same parent
	// concurrently (Cluster : Diffuse has no claiming), so accessed atomically.
	TArray<int32> ChildCounts;

	// Whether a node already has a child (Prune + Seed), as 0/1 for an atomic test-and-set. Fork tally: DiffusionForks.
	TArray<int32> ParentHasChild;

	// Forks spent per diffusion (Seed, global budget). Indexed by diffusion; one writer per slot.
	TArray<int32> DiffusionForks;

	EPCGExFloodFillBranchMode Mode = EPCGExFloodFillBranchMode::Prune;
	bool bSourceIsVtx = false;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFillControlsFactoryBranchLimit : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigBranchLimit Config;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-branch-limit"))
class UPCGExFillControlsBranchLimitProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsBranchLimit, "Fill Control : Branch Limit", "Limit how many times the fill may branch, suppressing heavy branching.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Control Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFillControlConfigBranchLimit Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
