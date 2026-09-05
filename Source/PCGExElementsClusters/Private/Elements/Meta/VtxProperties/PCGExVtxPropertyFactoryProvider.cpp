// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertyFactoryProvider.h"
#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExWriteVtxProperties"
#define PCGEX_NAMESPACE PCGExWriteVtxProperties

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoVtxProperty, UPCGExVtxPropertyFactoryData)

bool FPCGExSimpleEdgeOutputSettings::Validate(const FPCGContext* InContext) const
{
	if (bWriteDirection)
	{
		PCGEX_VALIDATE_NAME_C(InContext, DirectionAttribute);
	}
	if (bWriteLength)
	{
		PCGEX_VALIDATE_NAME_C(InContext, LengthAttribute);
	}
	return true;
}

void FPCGExSimpleEdgeOutputSettings::Init(const TSharedRef<PCGExData::FFacade>& InFacade)
{
	if (bWriteDirection)
	{
		DirWriter = InFacade->GetWritable<FVector>(DirectionAttribute, PCGExData::EBufferInit::New);
	}
	if (bWriteLength)
	{
		LengthWriter = InFacade->GetWritable<double>(LengthAttribute, PCGExData::EBufferInit::New);
	}
}

void FPCGExSimpleEdgeOutputSettings::Set(const int32 EntryIndex, const double InLength, const FVector& InDir) const
{
	if (DirWriter)
	{
		DirWriter->SetValue(EntryIndex, bInvertDirection ? InDir * -1 : InDir);
	}
	if (LengthWriter)
	{
		LengthWriter->SetValue(EntryIndex, InLength);
	}
}

void FPCGExSimpleEdgeOutputSettings::Set(const int32 EntryIndex, const PCGExClusters::FAdjacencyData& Data) const
{
	if (DirWriter)
	{
		DirWriter->SetValue(EntryIndex, bInvertDirection ? Data.Direction * -1 : Data.Direction);
	}
	if (LengthWriter)
	{
		LengthWriter->SetValue(EntryIndex, Data.Length);
	}
}

bool FPCGExEdgeOutputWithIndexSettings::Validate(const FPCGContext* InContext) const
{
	if (!FPCGExSimpleEdgeOutputSettings::Validate(InContext))
	{
		return false;
	}
	if (bWriteEdgeIndex)
	{
		PCGEX_VALIDATE_NAME_C(InContext, EdgeIndexAttribute);
	}
	if (bWriteVtxIndex)
	{
		PCGEX_VALIDATE_NAME_C(InContext, VtxIndexAttribute);
	}
	if (bWriteNeighborCount)
	{
		PCGEX_VALIDATE_NAME_C(InContext, NeighborCountAttribute);
	}
	return true;
}

void FPCGExEdgeOutputWithIndexSettings::Init(const TSharedRef<PCGExData::FFacade>& InFacade)
{
	FPCGExSimpleEdgeOutputSettings::Init(InFacade);
	if (bWriteEdgeIndex)
	{
		EIdxWriter = InFacade->GetWritable<int32>(EdgeIndexAttribute, PCGExData::EBufferInit::New);
	}
	if (bWriteVtxIndex)
	{
		VIdxWriter = InFacade->GetWritable<int32>(VtxIndexAttribute, PCGExData::EBufferInit::New);
	}
	if (bWriteNeighborCount)
	{
		NCountWriter = InFacade->GetWritable<int32>(NeighborCountAttribute, PCGExData::EBufferInit::New);
	}
}

void FPCGExEdgeOutputWithIndexSettings::Set(const int32 EntryIndex, const double InLength, const FVector& InDir, const int32 EIndex, const int32 VIndex, const int32 NeighborCount) const
{
	FPCGExSimpleEdgeOutputSettings::Set(EntryIndex, InLength, InDir);
	if (EIdxWriter)
	{
		EIdxWriter->SetValue(EntryIndex, EIndex);
	}
	if (VIdxWriter)
	{
		VIdxWriter->SetValue(EntryIndex, VIndex);
	}
	if (NCountWriter)
	{
		NCountWriter->SetValue(EntryIndex, NeighborCount);
	}
}

void FPCGExEdgeOutputWithIndexSettings::Set(const int32 EntryIndex, const PCGExClusters::FAdjacencyData& Data) const
{
	FPCGExSimpleEdgeOutputSettings::Set(EntryIndex, Data);
	if (EIdxWriter)
	{
		EIdxWriter->SetValue(EntryIndex, Data.EdgeIndex);
	}
	if (VIdxWriter)
	{
		VIdxWriter->SetValue(EntryIndex, Data.NodePointIndex);
	}
}

void FPCGExEdgeOutputWithIndexSettings::Set(const int32 EntryIndex, const PCGExClusters::FAdjacencyData& Data, const int32 NeighborCount)
{
	Set(EntryIndex, Data);
	if (NCountWriter)
	{
		NCountWriter->SetValue(EntryIndex, NeighborCount);
	}
}

bool FPCGExVtxPropertyOperation::WantsBFP() const
{
	return false;
}

bool FPCGExVtxPropertyOperation::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	PrimaryDataFacade = InVtxDataFacade;
	SecondaryDataFacade = InEdgeDataFacade;
	Cluster = InCluster.Get();
	bIsValidOperation = true;
	return bIsValidOperation;
}

bool FPCGExVtxPropertyOperation::IsOperationValid()
{
	return bIsValidOperation;
}

void FPCGExVtxPropertyOperation::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
}

#if WITH_EDITOR
FString UPCGExVtxPropertyProviderSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertyFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertyOperation)
	return NewOperation;
}

TArray<FPCGPinProperties> UPCGExVtxPropertyProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

UPCGExFactoryData* UPCGExVtxPropertyProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return Super::CreateFactory(InContext, InFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
