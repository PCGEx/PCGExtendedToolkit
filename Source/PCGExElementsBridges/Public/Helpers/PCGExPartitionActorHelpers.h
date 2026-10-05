// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Grid/PCGGridDescriptor.h"
#include "UObject/WeakObjectPtr.h"

#include "PCGExPartitionActorHelpers.generated.h"

class APCGPartitionActor;
class UWorld;
struct FPCGExPartitionQuery;

namespace PCGExPartitionGrid
{
	struct FCell;
}

UENUM()
enum class EPCGExPartitionActorKind : uint8
{
	Serialized       = 0 UMETA(DisplayName = "Serialized", ToolTip = "Partition actors saved with the level and streamed by World Partition."),
	RuntimeGenerated = 1 UMETA(DisplayName = "Runtime Generated", ToolTip = "Transient partition actors pooled by the runtime generation scheduler. They never carry data layers or an HLOD layer."),
	FromSource       = 2 UMETA(DisplayName = "From Source", ToolTip = "Runtime generated when the descriptor source generates at runtime, serialized otherwise."),
};

namespace PCGExPartitionActors
{
	/** Written next to each partition actor reference: PCGExPartitionGrid::FormatId of the cell it was resolved for. */
	inline const FName PartitionIdAttributeName = TEXT("PartitionId");

	/**
	 * What a lookup copies its grid descriptor from. The engine keys partition actors on grid size, 2D and runtime, plus
	 * the owner's data layers and HLOD layer (a runtime hash in game worlds); only a PCG component or a partition actor
	 * can supply those, a bare source cannot.
	 */
	struct PCGEXELEMENTSBRIDGES_API FDescriptorSource
	{
		enum class EType : uint8
		{
			Bare,
			Component,
			PartitionActor,
		};

		EType Type = EType::Bare;

		/** UObject-typed so the header needs no engine include; cast at the use site. */
		TWeakObjectPtr<const UObject> Object;

		bool operator==(const FDescriptorSource& Other) const { return Type == Other.Type && Object == Other.Object; }

		/**
		 * The key of one partition on this source's grid. False when the source object is gone. The query's 2D mode wins
		 * over the source's; bInFallback2D only serves a bare source left on Auto.
		 * The result freezes on its first lookup (the engine caches its hash and asserts in every setter afterwards).
		 */
		bool MakeDescriptor(const uint32 InGridSize, const FPCGExPartitionQuery& InQuery, const EPCGExPartitionActorKind InKind, const bool bInFallback2D, FPCGGridDescriptor& OutDescriptor) const;
	};

	/**
	 * Appends the sources InObject stands for: a partition actor, a PCG component (its partition actor when it is a local
	 * component), every PCG component of any other actor, or the owner of any other component. Game thread.
	 */
	PCGEXELEMENTSBRIDGES_API void AppendSources(const UObject* InObject, TArray<FDescriptorSource>& OutSources);

	/** The partition actor registered for InCoord, or null: not streamed in, or no such actor. Game thread. */
	PCGEXELEMENTSBRIDGES_API APCGPartitionActor* Find(const UWorld* InWorld, const FPCGGridDescriptor& InDescriptor, const FIntVector& InCoord);

	/**
	 * The cell InPartitionActor stands for right now. False while it stands for none: a runtime generated actor that holds
	 * no graph instance is pooled, whatever cell it last had. Game thread.
	 */
	PCGEXELEMENTSBRIDGES_API bool TryGetAssignedCell(const APCGPartitionActor* InPartitionActor, PCGExPartitionGrid::FCell& OutCell);

	/**
	 * Whether InPartitionActor stands, right now, for the partition InPartitionId names (PCGExPartitionGrid::FormatId with
	 * no prefix). Runtime generated partition actors are pooled: the same object comes back standing for another cell,
	 * so a reference alone does not say which partition it was taken for. Game thread.
	 */
	PCGEXELEMENTSBRIDGES_API bool StandsFor(const APCGPartitionActor* InPartitionActor, const FString& InPartitionId);

	/**
	 * True when World Partition has nothing left to stream in over InCell, so a serialized partition actor not registered
	 * by now is not coming. Says nothing about runtime generated ones: the scheduler assigns those once streaming is complete.
	 * Always true outside game worlds; never true in a game world before 5.8, whose query cannot see data layer cells.
	 */
	PCGEXELEMENTSBRIDGES_API bool IsCellStreamedIn(const UWorld* InWorld, const PCGExPartitionGrid::FCell& InCell);
}
