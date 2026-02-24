// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyDelta.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "GameFramework/Actor.h"

namespace PCGExActorDelta
{
	TArray<uint8> SerializeActorDelta(AActor* Actor)
	{
		TArray<uint8> Bytes;
		UClass* Class = Actor->GetClass();
		UObject* CDO = Class->GetDefaultObject();

		FMemoryWriter Writer(Bytes);
		FStructuredArchiveFromArchive Adapter(Writer);
		Class->SerializeTaggedProperties(
			Adapter.GetSlot(),
			reinterpret_cast<uint8*>(Actor),
			Class,
			reinterpret_cast<uint8*>(CDO),
			Actor);

		return Bytes;
	}

	void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes)
	{
		FMemoryReader Reader(DeltaBytes);
		FStructuredArchiveFromArchive Adapter(Reader);
		UClass* Class = Actor->GetClass();
		Class->SerializeTaggedProperties(
			Adapter.GetSlot(),
			reinterpret_cast<uint8*>(Actor),
			Class,
			reinterpret_cast<uint8*>(Class->GetDefaultObject()),
			Actor);
	}

	uint32 HashDelta(const TArray<uint8>& DeltaBytes)
	{
		if (DeltaBytes.IsEmpty()) { return 0; }
		return FCrc::MemCrc32(DeltaBytes.GetData(), DeltaBytes.Num());
	}
}
