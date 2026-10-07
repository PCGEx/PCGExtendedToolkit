// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExSocketFitDetails.h"
#include "Engine/StaticMesh.h"
#include "Sampling/PCGExSamplingCommon.h"

namespace PCGExMT
{
	struct FScope;
	class FTaskManager;
}

struct FPCGExSocketOutputDetails;

namespace PCGExData
{
	class FPointIOCollection;
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

struct FPCGContext;
struct FPCGMeshInstanceList;
class UPCGBasePointData;
class UPCGParamData;


namespace PCGExStaging
{
	namespace Labels
	{
		const FName OutputSocketLabel = TEXT("Sockets");
	}

	struct PCGEXFOUNDATIONS_API FSocketInfos
	{
		FSocketInfos() = default;
		FSoftObjectPath Path;
		FName Category = NAME_None;
		TArray<FPCGExSocket> Sockets;
		int32 Count = 0;
	};

#define PCGEX_FOREACH_FIELD_SAMPLESOCKETS(MACRO)\
MACRO(SocketName, FName, NAME_None) \
MACRO(SocketTag, FName, NAME_None) \
MACRO(Category, FName, NAME_None) \
MACRO(AssetPath, FSoftObjectPath, FSoftObjectPath{})

	class PCGEXFOUNDATIONS_API FSocketHelper : public TSharedFromThis<FSocketHelper>
	{
	protected:
		FRWLock SocketLock;
		const FPCGExSocketOutputDetails* Details = nullptr;

		TMap<uint64, int32> InfosKeys;
		TArray<FSocketInfos> SocketInfosList;
		TArray<int32> Mapping;
		TArray<int32> StartIndices;

		PCGEX_FOREACH_FIELD_SAMPLESOCKETS(PCGEX_OUTPUT_DECL)

	public:
		explicit FSocketHelper(const FPCGExSocketOutputDetails* InDetails, const int32 InNumPoints);

		TSharedPtr<PCGExData::FFacade> InputDataFacade;
		TSharedPtr<PCGExData::FFacade> SocketFacade;

		void Add(const int32 Index, const TObjectPtr<UStaticMesh>& Mesh);

		void Compile(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExData::FFacade>& InDataFacade, const TSharedPtr<PCGExData::FPointIOCollection>& InCollection);

	protected:
		FSocketInfos& NewSocketInfos(const uint64 EntryHash, int32& OutIndex);
		void FilterSocketInfos(const int32 Index);
		void CompileRange(const PCGExMT::FScope& Scope);
	};
}
