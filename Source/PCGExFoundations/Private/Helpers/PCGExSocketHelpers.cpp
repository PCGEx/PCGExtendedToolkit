// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExSocketHelpers.h"


#include "PCGExCoreSettingsCache.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSocket.h"
#include "Details/PCGExSocketOutputDetails.h"
#include "Engine/StaticMeshSocket.h"
#include "Helpers/PCGExRandomHelpers.h"

namespace PCGExStaging
{
	FSocketHelper::FSocketHelper(const FPCGExSocketOutputDetails* InDetails, const int32 InNumPoints)
		: Details(InDetails)
	{
		Mapping.Init(-1, InNumPoints);
		StartIndices.Init(-1, InNumPoints);
	}

	void FSocketHelper::Add(const int32 Index, const TObjectPtr<UStaticMesh>& Mesh)
	{
		const uint64 EntryHash = GetTypeHash(Mesh);

		const int32* IdxPtr = nullptr;

		{
			FReadScopeLock ReadLock(SocketLock);
			IdxPtr = InfosKeys.Find(EntryHash);
		}

		int32 Idx = -1;

		if (!IdxPtr)
		{
			FWriteScopeLock WriteLock(SocketLock);

			IdxPtr = InfosKeys.Find(EntryHash);

			if (IdxPtr)
			{
				Idx = *IdxPtr;
			}
			else
			{
				FSocketInfos& NewInfos = NewSocketInfos(EntryHash, Idx);

				NewInfos.Path = Mesh.GetPath();
				NewInfos.Category = NAME_None;
				NewInfos.Sockets.Reserve(Mesh->Sockets.Num());

				for (const TObjectPtr<UStaticMeshSocket>& MeshSocket : Mesh->Sockets)
				{
					FPCGExSocket& NewSocket = NewInfos.Sockets.Emplace_GetRef(
						MeshSocket->SocketName,
						MeshSocket->RelativeLocation,
						MeshSocket->RelativeRotation,
						MeshSocket->RelativeScale,
						MeshSocket->Tag);
					NewSocket.bManaged = true;
				}

				FilterSocketInfos(Idx);
			}
		}
		else
		{
			Idx = *IdxPtr;
		}

		FPlatformAtomics::InterlockedIncrement(&SocketInfosList[Idx].Count);
		Mapping[Index] = Idx;
	}

	void FSocketHelper::Compile(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExData::FFacade>& InDataFacade, const TSharedPtr<PCGExData::FPointIOCollection>& InCollection)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSocketHelper::Compile);

		int32 NumOutPoints = 0;

		InputDataFacade = InDataFacade;

		for (const TPair<uint64, int32>& InInfos : InfosKeys)
		{
			const FSocketInfos& Infos = SocketInfosList[InInfos.Value];
			NumOutPoints += Infos.Count * Infos.Sockets.Num();
		}

		const int32 NumPoints = InDataFacade->GetNum(PCGExData::EIOSide::In);

		TSharedPtr<PCGExData::FPointIO> SocketIO = InCollection->Emplace_GetRef(InDataFacade->GetIn());
		SocketIO->IOIndex = InDataFacade->Source->IOIndex;

		PCGEX_INIT_IO_VOID(SocketIO, PCGExData::EIOInit::New)
		SocketFacade = MakeShared<PCGExData::FFacade>(SocketIO.ToSharedRef());

		UPCGBasePointData* OutPoints = SocketIO->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, NumOutPoints, EPCGPointNativeProperties::MetadataEntry | EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed);

#define PCGEX_OUTPUT_INIT_LOCAL(_NAME, _TYPE, _DEFAULT_VALUE) if(Details->bWrite##_NAME){ _NAME##Writer = SocketFacade->GetWritable<_TYPE>(Details->_NAME##AttributeName, _DEFAULT_VALUE, true, PCGExData::EBufferInit::Inherit); }
		PCGEX_FOREACH_FIELD_SAMPLESOCKETS(PCGEX_OUTPUT_INIT_LOCAL)
#undef PCGEX_OUTPUT_INIT_LOCAL

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FSocketHelper::Compile::LoopPreparation);

			UPCGMetadata* Metadata = SocketFacade->GetOut()->MutableMetadata();
			Details->CarryOverDetails.Prune(Metadata);

			TConstPCGValueRange<int64> ReadMetadataEntry = InputDataFacade->GetIn()->GetConstMetadataEntryValueRange();
			TPCGValueRange<int64> OutMetadataEntries = SocketFacade->GetOut()->GetMetadataEntryValueRange();

			TArray<TTuple<int64, int64>> DelayedEntries;
			DelayedEntries.SetNum(OutMetadataEntries.Num());

			int32 WriteIndex = 0;
			for (int i = 0; i < NumPoints; i++)
			{
				int32 Idx = Mapping[i];
				if (Idx == -1)
				{
					continue;
				}

				StartIndices[i] = WriteIndex;

				const int32 NumSockets = SocketInfosList[Idx].Sockets.Num();
				const int64& InMetadataKey = ReadMetadataEntry[i];

				for (int j = 0; j < NumSockets; j++)
				{
					OutMetadataEntries[WriteIndex] = Metadata->AddEntryPlaceholder();
					DelayedEntries[WriteIndex] = MakeTuple(OutMetadataEntries[WriteIndex], InMetadataKey);
					WriteIndex++;
				}
			}

			Metadata->AddDelayedEntries(DelayedEntries);
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, CreateSocketPoints)
		CreateSocketPoints->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE, WeakManager = TWeakPtr<PCGExMT::FTaskManager>(TaskManager)]()
		{
			PCGEX_ASYNC_THIS
			if (This->SocketNameWriter || This->SocketTagWriter || This->CategoryWriter || This->AssetPathWriter)
			{
				if (TSharedPtr<PCGExMT::FTaskManager> PinnedManager = WeakManager.Pin())
				{
					This->SocketFacade->WriteFastest(PinnedManager);
				}
			}
		};

		CreateSocketPoints->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->CompileRange(Scope);
		};

		CreateSocketPoints->StartSubLoops(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize() * 4);
	}

	FSocketInfos& FSocketHelper::NewSocketInfos(const uint64 EntryHash, int32& OutIndex)
	{
		OutIndex = SocketInfosList.Num();
		InfosKeys.Add(EntryHash, OutIndex);
		return SocketInfosList.Emplace_GetRef();
	}

	void FSocketHelper::FilterSocketInfos(const int32 Index)
	{
		FSocketInfos& SocketInfos = SocketInfosList[Index];
		TArray<FPCGExSocket> ValidSockets;

		for (int i = 0; i < SocketInfos.Sockets.Num(); i++)
		{
			if (const FPCGExSocket& Socket = SocketInfos.Sockets[i];
				Details->SocketNameFilters.Test(Socket.SocketName.ToString()) && Details->SocketTagFilters.Test(Socket.Tag))
			{
				ValidSockets.Add(Socket);
			}
		}

		SocketInfos.Sockets = ValidSockets;
	}

	void FSocketHelper::CompileRange(const PCGExMT::FScope& Scope)
	{
		const UPCGBasePointData* SourceData = InputDataFacade->Source->GetOutIn();

		TConstPCGValueRange<FTransform> ReadTransform = SourceData->GetConstTransformValueRange();
		TPCGValueRange<FTransform> OutTransform = SocketFacade->GetOut()->GetTransformValueRange();

		TPCGValueRange<int32> OutSeed = SocketFacade->GetOut()->GetSeedValueRange();


		PCGEX_SCOPE_LOOP(i)
		{
			int32 Index = StartIndices[i];
			if (Index == -1)
			{
				continue;
			}

			const FTransform& InTransform = ReadTransform[i];
			const FSocketInfos& SocketInfos = SocketInfosList[Mapping[i]];

			// Cache stable per-socketinfos values once
			const FName& Category = SocketInfos.Category;
			const FSoftObjectPath& Path = SocketInfos.Path;

			for (const FPCGExSocket& Socket : SocketInfos.Sockets)
			{
				FTransform WorldTransform = Socket.RelativeTransform * InTransform;
				const FVector WorldSc = WorldTransform.GetScale3D();
				FVector OutScale = Socket.RelativeTransform.GetScale3D();

				for (const int32 C : Details->TrScaComponents)
				{
					OutScale[C] = WorldSc[C];
				}
				WorldTransform.SetScale3D(OutScale);

				OutTransform[Index] = WorldTransform;
				OutSeed[Index] = PCGExRandomHelpers::ComputeSpatialSeed(WorldTransform.GetLocation());

				PCGEX_OUTPUT_VALUE(SocketName, Index, Socket.SocketName)
				PCGEX_OUTPUT_VALUE(SocketTag, Index, FName(Socket.Tag))
				PCGEX_OUTPUT_VALUE(Category, Index, Category)
				PCGEX_OUTPUT_VALUE(AssetPath, Index, Path)

				Index++;
			}
		}
	}
}
