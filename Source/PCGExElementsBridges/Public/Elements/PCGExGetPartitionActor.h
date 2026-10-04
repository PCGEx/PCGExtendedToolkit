// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPin.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExPartitionDetails.h"
#include "Helpers/PCGExPartitionActorHelpers.h"

#include "PCGExGetPartitionActor.generated.h"

class UWorld;

namespace PCGExMT
{
	class FMainThreadPoll;
}

namespace PCGExGetPartitionActor
{
	inline const FName ReferencePinLabel = TEXT("Reference");
}

UENUM()
enum class EPCGExPartitionDescriptorSource : uint8
{
	ExecutingComponent = 0 UMETA(DisplayName = "Executing Component", ToolTip = "Look partitions up on the executing component's own grid: its graph's 2D setting, and its actor's data layers and HLOD layer."),
	Input              = 1 UMETA(DisplayName = "Input", ToolTip = "Look partitions up on the grid of each actor, partition actor or PCG component referenced on the Reference pin."),
};

UENUM()
enum class EPCGExPartitionActorOutput : uint8
{
	AttributeSet = 0 UMETA(DisplayName = "Attribute Set", ToolTip = "One attribute set, one row per partition actor."),
	Points       = 1 UMETA(DisplayName = "Points", ToolTip = "One point data, one point per partition actor, at its location and sized like its cell."),
};

/**
 * Get Partition Actor.
 * Outputs a reference to the PCG partition actor of each requested grid cell, for the partitions that are loaded.
 * Cells are picked relative to the executing component's own cell, like Partition Identifier. The engine keeps one
 * partition actor per cell and grid descriptor (size, 2D, runtime, plus the owner's data layers and HLOD layer), so
 * the descriptor comes from a PCG component: the executing one, or the ones referenced on the Reference pin.
 * References are resolved on the game thread during preparation; the optional wait pauses the node meanwhile.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Misc", meta = (Keywords = "pcgex partition actor grid cell higen world streaming reference", PCGExNodeLibraryDoc = "utilities/get-partition-actor"))
class UPCGExGetPartitionActorSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExGetPartitionActorElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetPartitionActor, "Get Partition Actor", "Outputs a reference to the PCG partition actor of each requested grid cell, for the partitions that are loaded. Can wait for a partition to stream in.");

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }
	virtual FLinearColor GetNodeTitleColor() const override;
#endif

protected:
#if WITH_EDITOR
	/** Descriptor Source decides whether the required Reference pin exists: culling is compiled, so the change is structural. */
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Whose grid the partitions are looked up on. Input adds the Reference pin, so not overridable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExPartitionDescriptorSource DescriptorSource = EPCGExPartitionDescriptorSource::ExecutingComponent;

	/** 'FSoftObjectPath' attribute read on the Reference pin. Any other component reference resolves to its owner. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "DescriptorSource == EPCGExPartitionDescriptorSource::Input", EditConditionHides))
	FName ActorReferenceAttribute = FName("ActorReference");

	/** Partitions to output, relative to the executing component's own cell; one actor each. Auto 2D follows the
	 *  descriptor source, so force 2D or 3D here to reach the other kind of grid. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGExPartitionQuery> Partitions = {FPCGExPartitionQuery{}};

	/** Which kind of partition actor to look for. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPartitionActorKind Kind = EPCGExPartitionActorKind::Serialized;

	/** Shape of the output. Drives the output pin type, so not overridable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable))
	EPCGExPartitionActorOutput OutputType = EPCGExPartitionActorOutput::AttributeSet;

	/** Pause until every reference is loaded and every partition actor is registered, or until the timeout. In 5.8+ a
	 *  serialized one is given up on as soon as World Partition has streamed its cell in without it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Wait", meta = (PCG_Overridable))
	bool bWaitForMissingActors = false;

	/** Seconds of real time after which the wait gives up and whatever is loaded is output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Wait", meta = (PCG_Overridable, DisplayName = " └─ Timeout", EditCondition = "bWaitForMissingActors", EditConditionHides, ClampMin = 0.001, UIMax = 30))
	double WaitTimeout = 1;

	/** Suppress the warning when some of the requested partition actors are loaded but not all of them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietMissingPartitionWarning = false;

	/** Suppress the warnings about the Reference pin: no reference, or one that is not loaded or carries no PCG component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietInvalidReferenceWarning = false;

	/** Suppress the warning when Wait For Missing Actors times out. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietTimeoutWarning = false;
};

struct FPCGExGetPartitionActorContext final : FPCGExContext
{
	/** One partition looked up on one descriptor source. */
	struct FRequest
	{
		/** Complete before the first lookup, never touched after: the engine caches its hash in it. */
		FPCGGridDescriptor Descriptor;
		PCGExPartitionGrid::FCell Cell;

		/** Not registered yet, and not known to be absent. */
		bool bPending = true;

		/** Set once the partition actor is found. Paths, not pointers: staging may run off the game thread. */
		FSoftObjectPath Actor;
		FVector Location = FVector::ZeroVector;
	};

	/** One reference, with the requests it yields once it resolves. The executing component is a slot with no reference. */
	struct FSlot
	{
		FSoftObjectPath Reference;
		bool bResolved = false;
		/** Resolved, but to nothing that carries a PCG component. */
		bool bNoSource = false;
		TArray<FRequest> Requests;
	};

	TArray<FSlot> Slots;

	FVector Anchor = FVector::ZeroVector;
	TWeakObjectPtr<UWorld> World;

	/** Something is still missing after Boot and the wait is enabled. */
	bool bNeedsWait = false;

	TSharedPtr<PCGExMT::FMainThreadPoll> Wait;
};

class FPCGExGetPartitionActorElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetPartitionActor)
	// References and partition actors are resolved on the game thread; the wait polls there too, from the subsystem tick.
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	/** Partition actors register and unregister with no dependency-CRC change; a cached result would be stale. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

	/** Always fresh objects; a content CRC keeps downstream cached when the same actors come out. */
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override { return true; }

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
