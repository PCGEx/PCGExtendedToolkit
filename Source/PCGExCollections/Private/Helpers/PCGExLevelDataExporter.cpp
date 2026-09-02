// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExLevelDataExporter.h"

#include "PCGExAssemblyRoot.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#pragma region FPCGExLevelExportSource

FPCGExLevelExportSource FPCGExLevelExportSource::FromWorld(UWorld* InWorld)
{
	FPCGExLevelExportSource Source;
	Source.World = InWorld;
	if (InWorld && InWorld->PersistentLevel)
	{
		Source.Actors.Reserve(InWorld->PersistentLevel->Actors.Num());
		for (AActor* Actor : InWorld->PersistentLevel->Actors)
		{
			if (Actor)
			{
				Source.Actors.Add(Actor);
			}
		}
	}
	return Source;
}

FPCGExLevelExportSource FPCGExLevelExportSource::FromActorSubtree(AActor* InRoot)
{
	FPCGExLevelExportSource Source;
	if (!InRoot)
	{
		return Source;
	}

	Source.World = InRoot->GetWorld();
	Source.Root = InRoot;

	const IPCGExAssemblyRoot* AssemblyRoot = Cast<IPCGExAssemblyRoot>(InRoot);
	Source.Frame = AssemblyRoot ? AssemblyRoot->GetAssemblyFrame() : InRoot->GetActorTransform();

	// Depth-first over attach children. A pruned node takes its whole subtree with it; a node the
	// root keeps is content even when it is a bare grouping pivot -- the exporter's own content
	// filter decides what becomes a point.
	TArray<AActor*> Stack;
	Stack.Add(InRoot);

	TArray<AActor*> Children;
	while (!Stack.IsEmpty())
	{
		const AActor* Node = Stack.Pop(EAllowShrinking::No);

		Children.Reset();
		Node->GetAttachedActors(Children, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/false);

		for (AActor* Child : Children)
		{
			if (!Child || (AssemblyRoot && !AssemblyRoot->IsAssemblyContent(Child)))
			{
				continue;
			}
			Source.Actors.Add(Child);
			Stack.Add(Child);
		}
	}

	return Source;
}

#pragma endregion
