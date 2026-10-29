// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExTopology.h"

#include "PCGComponent.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshRepairFunctions.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Paths/PCGExPath.h"
#include "UObject/GarbageCollection.h"

void FPCGExTopologyDetails::PostProcessMesh(const TObjectPtr<UDynamicMesh>& InDynamicMesh) const
{
	if (bWeldEdges)
	{
		UGeometryScriptLibrary_MeshRepairFunctions::WeldMeshEdges(InDynamicMesh, WeldEdgesOptions);
	}
	if (bComputeNormals)
	{
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(InDynamicMesh, NormalsOptions);
	}
	if (bFlipNormals)
	{
		UGeometryScriptLibrary_MeshNormalsFunctions::FlipNormals(InDynamicMesh);
	}
}

void FPCGExTopologyUVDetails::Prepare(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	TSet<int32> ExistingChannels;
	ExistingChannels.Reserve(8);
	ChannelIndices.Reserve(8);
	UVBuffers.Reserve(8);

	for (const FPCGExUVInputDetails& Channel : UVs)
	{
		if (!Channel.bEnabled || Channel.AttributeName.IsNone())
		{
			continue;
		}

		bool bIsAlreadySet = false;
		ExistingChannels.Add(Channel.Channel, &bIsAlreadySet);
		if (bIsAlreadySet)
		{
			continue;
		}

		TSharedPtr<PCGExData::TBuffer<FVector2D>> Buffer = InDataFacade->GetBroadcaster<FVector2D>(Channel.AttributeName, true);
		if (!Buffer)
		{
			PCGEX_LOG_INVALID_ATTR_C(InDataFacade->GetContext(), UV Channel, Channel.AttributeName)
			continue;
		}

		NumChannels = FMath::Max(NumChannels, Channel.Channel + 1);
		ChannelIndices.Add(Channel.Channel);
		UVBuffers.Add(Buffer);
	}
}

void FPCGExTopologyUVDetails::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	TSet<int32> ExistingChannels;
	ExistingChannels.Reserve(8);

	for (const FPCGExUVInputDetails& Channel : UVs)
	{
		if (!Channel.bEnabled || Channel.AttributeName.IsNone())
		{
			continue;
		}

		bool bIsAlreadySet = false;
		ExistingChannels.Add(Channel.Channel, &bIsAlreadySet);
		if (bIsAlreadySet)
		{
			continue;
		}

		FacadePreloader.Register<FVector2D>(InContext, Channel.AttributeName, PCGExData::EBufferPreloadType::BroadcastFromName);
	}
}

void FPCGExTopologyUVDetails::Write(const TArray<int32>& TriangleIDs, UE::Geometry::FDynamicMesh3& InMesh) const
{
	if (!NumChannels)
	{
		return;
	}

	const int32 VtxCount = InMesh.MaxVertexID();
	InMesh.Attributes()->SetNumUVLayers(NumChannels);

	TArray<int32> ElemIDs;
	ElemIDs.SetNum(VtxCount);

	for (int t = 0; t < ChannelIndices.Num(); ++t)
	{
		const int32 ChannelIndex = ChannelIndices[t];
		const TSharedPtr<PCGExData::TBuffer<FVector2D>> UVBuffer = UVBuffers[t];

		UE::Geometry::FDynamicMeshUVOverlay* UV = InMesh.Attributes()->GetUVLayer(ChannelIndex);

		for (int i = 0; i < VtxCount; i++)
		{
			ElemIDs[i] = UV->AppendElement(FVector2f(UVBuffer->Read(i)));
		}

		for (const int32 TriangleID : TriangleIDs)
		{
			const UE::Geometry::FIndex3i Triangle = InMesh.GetTriangle(TriangleID);
			UV->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]));
		}
	}
}

void FPCGExTopologyUVDetails::Write(const TArray<int32>& TriangleIDs, const TArray<int32>& VtxIDs, UE::Geometry::FDynamicMesh3& InMesh) const
{
	if (!NumChannels)
	{
		return;
	}

	const int32 VtxCount = InMesh.MaxVertexID();
	InMesh.Attributes()->SetNumUVLayers(NumChannels);

	TArray<int32> ElemIDs;
	ElemIDs.SetNum(VtxCount);


	for (int t = 0; t < ChannelIndices.Num(); ++t)
	{
		const int32 ChannelIndex = ChannelIndices[t];
		const TSharedPtr<PCGExData::TBuffer<FVector2D>> UVBuffer = UVBuffers[t];
		UE::Geometry::FDynamicMeshUVOverlay* UV = InMesh.Attributes()->GetUVLayer(ChannelIndex);

		for (int i = 0; i < VtxCount; i++)
		{
			const int32 PtIdx = VtxIDs[i];
			ElemIDs[i] = UV->AppendElement(FVector2f(PtIdx != -1 ? UVBuffer->Read(PtIdx) : FVector2D(0)));
		}

		for (const int32 TriangleID : TriangleIDs)
		{
			const UE::Geometry::FIndex3i Triangle = InMesh.GetTriangle(TriangleID);
			UV->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]));
		}
	}
}

void FPCGExTopologyUVDetails::Write(
	const TArray<int32>& TriangleIDs,
	const TArray<int32>& VertexIDs,
	const TArray<int32>& SourceDataIndices,
	const TArray<int32>& SourcePointIndices,
	const TArray<TSharedPtr<PCGExData::FFacade>>& Facades,
	UE::Geometry::FDynamicMesh3& InMesh) const
{
	if (UVs.IsEmpty())
	{
		return;
	}

	const int32 VtxCount = InMesh.MaxVertexID();
	const int32 NumFacades = Facades.Num();

	// Build UV buffers per facade per channel
	TSet<int32> ExistingChannels;
	TArray<int32> ActiveChannelIndices;
	TMap<int32, TArray<TSharedPtr<PCGExData::TBuffer<FVector2D>>>> PerFacadeBuffers; // ChannelIndex -> [FacadeIndex -> Buffer]

	for (const FPCGExUVInputDetails& Channel : UVs)
	{
		if (!Channel.bEnabled || Channel.AttributeName.IsNone())
		{
			continue;
		}

		bool bIsAlreadySet = false;
		ExistingChannels.Add(Channel.Channel, &bIsAlreadySet);
		if (bIsAlreadySet)
		{
			continue;
		}

		TArray<TSharedPtr<PCGExData::TBuffer<FVector2D>>>& FacadeBuffers = PerFacadeBuffers.FindOrAdd(Channel.Channel);
		FacadeBuffers.SetNum(NumFacades);

		bool bHasAnyBuffer = false;
		for (int32 FacadeIdx = 0; FacadeIdx < NumFacades; ++FacadeIdx)
		{
			if (const TSharedPtr<PCGExData::FFacade>& Facade = Facades[FacadeIdx])
			{
				FacadeBuffers[FacadeIdx] = Facade->GetBroadcaster<FVector2D>(Channel.AttributeName, true);
				if (FacadeBuffers[FacadeIdx])
				{
					bHasAnyBuffer = true;
				}
			}
		}

		if (bHasAnyBuffer)
		{
			ActiveChannelIndices.Add(Channel.Channel);
		}
	}

	if (ActiveChannelIndices.IsEmpty())
	{
		return;
	}

	// Find max channel index for layer count
	int32 MaxChannel = 0;
	for (const int32 Ch : ActiveChannelIndices)
	{
		MaxChannel = FMath::Max(MaxChannel, Ch + 1);
	}
	InMesh.Attributes()->SetNumUVLayers(MaxChannel);

	TArray<int32> ElemIDs;
	ElemIDs.SetNum(VtxCount);

	for (const int32 ChannelIndex : ActiveChannelIndices)
	{
		const TArray<TSharedPtr<PCGExData::TBuffer<FVector2D>>>& FacadeBuffers = PerFacadeBuffers[ChannelIndex];
		UE::Geometry::FDynamicMeshUVOverlay* UV = InMesh.Attributes()->GetUVLayer(ChannelIndex);

		for (int32 i = 0; i < VtxCount; ++i)
		{
			FVector2D UVValue = FVector2D::ZeroVector;

			const int32 SrcDataIdx = SourceDataIndices[i];
			const int32 SrcPtIdx = SourcePointIndices[i];

			if (SrcDataIdx >= 0 && SrcDataIdx < NumFacades && SrcPtIdx >= 0)
			{
				if (const TSharedPtr<PCGExData::TBuffer<FVector2D>>& Buffer = FacadeBuffers[SrcDataIdx])
				{
					UVValue = Buffer->Read(SrcPtIdx);
				}
			}

			ElemIDs[i] = UV->AppendElement(FVector2f(UVValue));
		}

		for (const int32 TriangleID : TriangleIDs)
		{
			const UE::Geometry::FIndex3i Triangle = InMesh.GetTriangle(TriangleID);
			UV->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]));
		}
	}
}

FTransform PCGExTopology::GetCoordinateSpaceTransform(EPCGCoordinateSpace CoordinateSpace, FPCGExContext* Context)
{
	if (CoordinateSpace == EPCGCoordinateSpace::World)
	{
		return FTransform::Identity;
	}

	FTransform LocalTransform = Context->ExecutionSource->GetExecutionState().GetTransform();

	if (CoordinateSpace == EPCGCoordinateSpace::OriginalComponent)
	{
		if (UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context->ExecutionSource.Get()))
		{
			// A partitioned component's original is a soft pointer on its partition actor, and callers run on workers.
			UPCGComponent* OriginalComponent = nullptr;
			{
				FGCScopeGuard GCGuard;
				OriginalComponent = SourceComponent->GetOriginalComponent();
			}
			check(OriginalComponent && OriginalComponent->GetOwner());
			LocalTransform = OriginalComponent->GetOwner()->GetActorTransform();
		}
	}

	// Strip rotation and scale - we only want translation offset
	LocalTransform.SetScale3D(FVector::OneVector);
	LocalTransform.SetRotation(FQuat::Identity);
	return LocalTransform;
}
