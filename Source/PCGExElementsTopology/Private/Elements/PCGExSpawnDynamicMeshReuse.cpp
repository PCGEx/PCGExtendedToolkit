// Copyright 2026 Eidos Interactive Corp and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// See PCGExSpawnDynamicMeshReuse.h.

#include "Elements/PCGExSpawnDynamicMeshReuse.h"

#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGElement.h"
#include "Components/PCGExDynamicMeshComponent.h"
#include "Core/PCGExContext.h"
#include "Data/PCGDynamicMeshData.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"

namespace PCGExSpawnDynamicMesh
{
	namespace Private
	{
		static TAutoConsoleVariable<bool> CVarAllowComponentReuse(
			TEXT("pcgex.SpawnDynamicMesh.AllowComponentReuse"),
			true,
			TEXT("Controls whether the Spawn Dynamic Mesh node reuses the component it created on a previous generation when nothing it depends on has changed."));

		/**
		* Tag prefixes that carry a per-execution value, and are therefore left out of the identity a component is
		* reused by. PCGEx stamps its clusters with a running counter, whose value depends on how many clusters the
		* execution happened to build before this one.
		*/
		static const TCHAR* VolatileTagPrefixes[] = {TEXT("PCGEx/Cluster:")};

		/** True for a tag whose value changes from one generation to the next even though nothing else did. */
		bool IsVolatileTag(const FString& InTag)
		{
			for (const TCHAR* Prefix : VolatileTagPrefixes)
			{
				if (InTag.StartsWith(Prefix))
				{
					return true;
				}
			}

			return false;
		}

		/**
		* Hashes the geometry that actually reaches the component: the vertex and triangle slots in index order.
		* Walking the id ranges rather than the live elements keeps meshes with the same geometry but a different
		* free-list history apart, which matches how the mesh is serialized into the package.
		*/
		void AddMeshToCrc(const UDynamicMesh* InDynamicMesh, FArchiveCrc32& Ar)
		{
			if (!IsValid(InDynamicMesh))
			{
				return;
			}

			InDynamicMesh->ProcessMesh([&Ar](const UE::Geometry::FDynamicMesh3& Mesh)
			{
				int32 MaxVertexID = Mesh.MaxVertexID();
				int32 MaxTriangleID = Mesh.MaxTriangleID();
				int32 VertexCount = Mesh.VertexCount();
				int32 TriangleCount = Mesh.TriangleCount();

				Ar << MaxVertexID;
				Ar << MaxTriangleID;
				Ar << VertexCount;
				Ar << TriangleCount;

				for (int32 VertexID = 0; VertexID < MaxVertexID; ++VertexID)
				{
					if (!Mesh.IsVertex(VertexID))
					{
						continue;
					}

					Ar << VertexID;

					// Hashed exactly: the graph reproduces each mesh bit for bit, so no tolerance is needed and a
					// tolerance would only risk two distinct meshes claiming the same component.
					FVector3d Position = Mesh.GetVertex(VertexID);
					Ar << Position;
				}

				for (int32 TriangleID = 0; TriangleID < MaxTriangleID; ++TriangleID)
				{
					if (!Mesh.IsTriangle(TriangleID))
					{
						continue;
					}

					Ar << TriangleID;

					UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
					Ar << Triangle.A;
					Ar << Triangle.B;
					Ar << Triangle.C;
				}
			});
		}

		/** Hashes the materials by path, in slot order, since the element assigns them by index. */
		void AddMaterialsToCrc(const TArray<TObjectPtr<UMaterialInterface>>& InMaterials, FArchiveCrc32& Ar)
		{
			int32 NumMaterials = InMaterials.Num();
			Ar << NumMaterials;

			for (const TObjectPtr<UMaterialInterface>& Material : InMaterials)
			{
				FString MaterialPath = IsValid(Material) ? Material->GetPathName() : FString{};
				Ar << MaterialPath;
			}
		}

		/**
		* The identity of one input: its geometry, its materials, its stable tags and the node's settings.
		*
		* Deliberately not the data Crc from GetDependenciesCrc: UPCGDynamicMeshData::AddToCrc serializes the metadata
		* and the FDynamicMesh3 wholesale and does not reproduce between two runs of the same graph, so keying reuse on
		* it almost never matches.
		*/
		FPCGCrc ComputeGeometryCrc(
			const UPCGDynamicMeshData* InDynamicMeshData,
			const TSet<FString>& InTags,
			const UPCGSettings* InSettings)
		{
			FArchiveCrc32 Ar;

			AddMeshToCrc(InDynamicMeshData->GetDynamicMesh(), Ar);
			AddMaterialsToCrc(InDynamicMeshData->GetMaterials(), Ar);

			// Tags end up on the component, so they are part of what makes two inputs interchangeable. Sorted, because
			// a TSet iterates in an order that depends on how it was filled, and without the volatile ones, which would
			// change an input's identity every run for no reason so that no component could ever be reused.
			TArray<FString> SortedTags;
			SortedTags.Reserve(InTags.Num());
			for (const FString& Tag : InTags)
			{
				if (!IsVolatileTag(Tag))
				{
					SortedTags.Add(Tag);
				}
			}
			SortedTags.Sort();

			for (FString& Tag : SortedTags)
			{
				Ar << Tag;
			}

			// The node's own settings, so that two Spawn Dynamic Mesh nodes fed the same mesh keep their own components.
			if (InSettings != nullptr)
			{
				uint32 SettingsCrcValue = InSettings->GetSettingsCrc().GetValue();
				Ar << SettingsCrcValue;
			}

			return FPCGCrc(Ar.GetCrc());
		}

		/** Finds the resource this input should reuse, which is the one built from the same input on a previous generation. */
		UPCGExManagedDynamicMeshComponent* FindResourceToReuse(
			UPCGComponent* InSourceComponent,
			const AActor* InTargetActor,
			const FPCGCrc& InInputCrc)
		{
			UPCGExManagedDynamicMeshComponent* ResourceToReuse = nullptr;

			InSourceComponent->ForEachManagedResource([&ResourceToReuse, &InInputCrc, InTargetActor](UPCGManagedResource* InResource)
			{
				if (ResourceToReuse != nullptr)
				{
					return;
				}

				UPCGExManagedDynamicMeshComponent* CandidateResource = Cast<UPCGExManagedDynamicMeshComponent>(InResource);
				if (CandidateResource == nullptr || !CandidateResource->SupportsComponentReset() || !CandidateResource->CanBeUsed())
				{
					return;
				}

				// Compared through GetValue because FPCGCrc::operator== ensures on an invalid operand, and a resource
				// loaded from a package that predates this change carries none.
				if (!CandidateResource->GetCrc().IsValid() || CandidateResource->GetCrc().GetValue() != InInputCrc.GetValue())
				{
					return;
				}

				// Two inputs carrying the same mesh share a Crc, so a resource an earlier input already took has to be
				// passed over. A surviving resource is marked unused by the cleanup that precedes the generation and
				// MarkAsReused clears that, which makes the flag the record of what has been claimed.
				if (!CandidateResource->IsMarkedUnused())
				{
					return;
				}

				// The component has to still exist and still belong to the actor this input is spawning onto: the
				// target actor is not necessarily covered by the Crc.
				const UPCGExDynamicMeshComponent* GeneratedMeshComponent = Cast<UPCGExDynamicMeshComponent>(CandidateResource->GeneratedComponent.Get());
				if (!IsValid(GeneratedMeshComponent) || GeneratedMeshComponent->GetOwner() != InTargetActor)
				{
					return;
				}

				ResourceToReuse = CandidateResource;
			});

			return ResourceToReuse;
		}
	}

	UClass* GetManagedComponentClass()
	{
		// Falling back to the default resource when reuse is off leaves the node behaving exactly as it did before.
		return Private::CVarAllowComponentReuse.GetValueOnAnyThread() ? UPCGExManagedDynamicMeshComponent::StaticClass() : nullptr;
	}

	FPCGCrc ComputeInputCrc(
		const IPCGElement* InElement,
		FPCGExContext* InContext,
		const UPCGSettings* InSettings,
		const FPCGTaggedData& InInput)
	{
		if (InElement == nullptr || InContext == nullptr || !Private::CVarAllowComponentReuse.GetValueOnAnyThread())
		{
			return FPCGCrc{};
		}

		const UPCGDynamicMeshData* DynamicMeshData = Cast<UPCGDynamicMeshData>(InInput.Data);
		if (DynamicMeshData == nullptr)
		{
			return FPCGCrc{};
		}

		return Private::ComputeGeometryCrc(DynamicMeshData, InInput.Tags, InSettings);
	}

	UPCGExDynamicMeshComponent* TryReuseComponent(
		UPCGComponent* InSourceComponent,
		const AActor* InTargetActor,
		const FPCGCrc& InInputCrc)
	{
		if (!IsValid(InSourceComponent) || !IsValid(InTargetActor) || !InInputCrc.IsValid())
		{
			return nullptr;
		}

		if (!Private::CVarAllowComponentReuse.GetValueOnAnyThread())
		{
			return nullptr;
		}

		UPCGExManagedDynamicMeshComponent* ResourceToReuse = Private::FindResourceToReuse(InSourceComponent, InTargetActor, InInputCrc);
		if (ResourceToReuse == nullptr)
		{
			return nullptr;
		}

		// MarkAsReused, not MarkAsUsed: the Crc guarantees the component already holds exactly what this generation
		// would have built, so clearing it and rebuilding it would only churn the package again.
		ResourceToReuse->MarkAsReused();

		return Cast<UPCGExDynamicMeshComponent>(ResourceToReuse->GeneratedComponent.Get());
	}

	void EnableReuse(UPCGManagedComponent* InResource, const FPCGCrc& InInputCrc)
	{
		UPCGExManagedDynamicMeshComponent* Resource = Cast<UPCGExManagedDynamicMeshComponent>(InResource);
		if (Resource == nullptr)
		{
			return;
		}

		// An invalid Crc simply means this resource could never be matched, so there is no point keeping it alive.
		Resource->bSupportsReset = InInputCrc.IsValid();
		Resource->SetCrc(InInputCrc);
	}
}

void UPCGExManagedDynamicMeshComponent::ResetComponent()
{
	// Only reached if the resource is handed to a generation that did not recognize it, which this element never does:
	// it either matches the Crc and calls MarkAsReused, or builds a new component. Implemented all the same so that the
	// resource honours the contract of the reset it opts into, and an empty mesh is what "no longer generated" means
	// for this component.
	UPCGExDynamicMeshComponent* MeshComponent = Cast<UPCGExDynamicMeshComponent>(GeneratedComponent.Get());
	if (!IsValid(MeshComponent))
	{
		return;
	}

	MeshComponent->EmptyOverrideMaterials();
	MeshComponent->SetMesh(UE::Geometry::FDynamicMesh3{});
}
