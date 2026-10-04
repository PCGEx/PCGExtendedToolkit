// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExPartitionActorHelpers.h"

#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "Grid/PCGPartitionActor.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

#include "PCGExVersion.h"
#include "Details/PCGExPartitionDetails.h"

namespace PCGExPartitionActorHelpers
{
	// UPCGComponent::GetGridDescriptor asserts on a local component asked for another grid size: its partition actor is the source.
	void AppendComponent(const UPCGComponent* InComponent, TArray<PCGExPartitionActors::FDescriptorSource>& OutSources)
	{
		const AActor* Owner = InComponent ? InComponent->GetOwner() : nullptr;
		if (!Owner) { return; }

		if (const APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Owner))
		{
			OutSources.AddUnique(PCGExPartitionActors::FDescriptorSource{PCGExPartitionActors::FDescriptorSource::EType::PartitionActor, PartitionActor});
		}
		else
		{
			OutSources.AddUnique(PCGExPartitionActors::FDescriptorSource{PCGExPartitionActors::FDescriptorSource::EType::Component, InComponent});
		}
	}
}

namespace PCGExPartitionActors
{
	bool FDescriptorSource::MakeDescriptor(const uint32 InGridSize, const FPCGExPartitionQuery& InQuery, const EPCGExPartitionActorKind InKind, const bool bInFallback2D, FPCGGridDescriptor& OutDescriptor) const
	{
		FPCGGridDescriptor Descriptor;
		bool bSource2D = bInFallback2D;
		bool bSourceRuntime = false;

		switch (Type)
		{
		case EType::Bare:
			break;
		case EType::Component:
			{
				const UPCGComponent* Component = Cast<UPCGComponent>(Object.Get());
				if (!Component || !Component->GetOwner()) { return false; }

				Descriptor = Component->GetGridDescriptor(InGridSize);
				bSource2D = Descriptor.Is2DGrid();
				bSourceRuntime = Descriptor.IsRuntime();
			}
			break;
		case EType::PartitionActor:
			{
				const APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Object.Get());
				if (!PartitionActor) { return false; }

				Descriptor = PartitionActor->GetGridDescriptor();
				bSource2D = Descriptor.Is2DGrid();
				bSourceRuntime = Descriptor.IsRuntime();
			}
			break;
		default:
			ensureMsgf(false, TEXT("Unresolvable FDescriptorSource::EType (%d)"), static_cast<int32>(Type));
			return false;
		}

		bool bRuntime = false;
		switch (InKind)
		{
		case EPCGExPartitionActorKind::Serialized:
			break;
		case EPCGExPartitionActorKind::RuntimeGenerated:
			bRuntime = true;
			break;
		case EPCGExPartitionActorKind::FromSource:
			bRuntime = bSourceRuntime;
			break;
		default:
			ensureMsgf(false, TEXT("Unresolvable EPCGExPartitionActorKind (%d)"), static_cast<int32>(InKind));
			return false;
		}

		// Runtime partition actors never carry layers, so their key is bare whatever the source.
		if (bRuntime) { Descriptor = FPCGGridDescriptor(); }

		OutDescriptor = Descriptor.SetGridSize(InGridSize).SetIs2DGrid(PCGExPartitionGrid::ResolveGrid2D(InQuery, bSource2D)).SetIsRuntime(bRuntime);
		return true;
	}

	void AppendSources(const UObject* InObject, TArray<FDescriptorSource>& OutSources)
	{
		if (const APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(InObject))
		{
			OutSources.AddUnique(FDescriptorSource{FDescriptorSource::EType::PartitionActor, PartitionActor});
		}
		else if (const UPCGComponent* Component = Cast<UPCGComponent>(InObject))
		{
			PCGExPartitionActorHelpers::AppendComponent(Component, OutSources);
		}
		else if (const AActor* Actor = Cast<AActor>(InObject))
		{
			Actor->ForEachComponent<UPCGComponent>(/*bIncludeFromChildActors=*/false, [&OutSources](const UPCGComponent* Component)
			{
				PCGExPartitionActorHelpers::AppendComponent(Component, OutSources);
			});
		}
		else if (const UActorComponent* ActorComponent = Cast<UActorComponent>(InObject))
		{
			AppendSources(ActorComponent->GetOwner(), OutSources);
		}
	}

	APCGPartitionActor* Find(const UWorld* InWorld, const FPCGGridDescriptor& InDescriptor, const FIntVector& InCoord)
	{
		const UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(const_cast<UWorld*>(InWorld));
		APCGPartitionActor* PartitionActor = Subsystem ? Subsystem->GetRegisteredPCGPartitionActor(InDescriptor, InCoord) : nullptr;
		return IsValid(PartitionActor) ? PartitionActor : nullptr;
	}

	bool TryGetAssignedCell(const APCGPartitionActor* InPartitionActor, PCGExPartitionGrid::FCell& OutCell)
	{
		if (!InPartitionActor) { return false; }

		// The pool holds actors with no graph instance, still carrying the grid size and location of their last cell.
		if (InPartitionActor->IsRuntimeGenerated() && !InPartitionActor->HasLocalPCGComponents()) { return false; }

		// APCGPartitionActor::GetGridCoord asserts on a zero grid size.
		if (InPartitionActor->GetPCGGridSize() == 0u) { return false; }

		OutCell = PCGExPartitionGrid::FCell(InPartitionActor->GetPCGGridSize(), InPartitionActor->GetGridCoord(), InPartitionActor->IsUsing2DGrid());
		return true;
	}

	bool StandsFor(const APCGPartitionActor* InPartitionActor, const FString& InPartitionId)
	{
		PCGExPartitionGrid::FCell Cell;
		return TryGetAssignedCell(InPartitionActor, Cell) && PCGExPartitionGrid::FormatId(Cell) == InPartitionId;
	}

	bool IsCellStreamedIn(const UWorld* InWorld, const PCGExPartitionGrid::FCell& InCell)
	{
		// Nothing streams outside a game world: what is not registered there stays missing.
		if (!InWorld || !InWorld->IsGameWorld()) { return true; }

#if PCGEX_ENGINE_VERSION >= 508
		const UWorldPartitionSubsystem* WorldPartition = UWorld::GetSubsystem<UWorldPartitionSubsystem>(InWorld);
		if (!WorldPartition) { return true; }

		// Engine mirror (FPCGRuntimeGenScheduler::TickQueueSourcesForGeneration), plus data layer cells: a partition
		// actor inherits its owner's data layers, and the default query only sees cells that have none.
		// TODO: query the partition actor's own location only; over the whole cell, one unloaded far-side cell keeps this false.
		TArray<FWorldPartitionStreamingQuerySource> QuerySources;
		FWorldPartitionStreamingQuerySource& QuerySource = QuerySources.Emplace_GetRef(InCell.CellCenter());
		QuerySource.bSpatialQuery = true;
		QuerySource.bUseGridLoadingRange = false;
		QuerySource.Radius = static_cast<float>(InCell.GridSize) * 0.5f;
		QuerySource.bDataLayersOnly = false;
		QuerySource.bIncludeAnyDataLayer = true;

		return WorldPartition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, QuerySources, /*bExactState=*/false);
#else
		return false;
#endif
	}
}
