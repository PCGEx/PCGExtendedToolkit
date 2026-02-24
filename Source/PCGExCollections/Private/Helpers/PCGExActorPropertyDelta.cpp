// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyDelta.h"

#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace PCGExActorDelta
{
	namespace Internal
	{
		/**
		 * Only serialize properties that are user-editable on instances (EditAnywhere / EditInstanceOnly).
		 * This excludes engine bookkeeping (ActorGuid, tick state, net role, etc.) that always differs
		 * between instances and their CDO but doesn't represent user intent.
		 */
		static bool IsInstanceEditableProperty(const FProperty* InProperty)
		{
			return InProperty->HasAnyPropertyFlags(CPF_Edit)
				&& !InProperty->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		}

		class FDeltaWriter : public FObjectWriter
		{
		public:
			using FObjectWriter::FObjectWriter;

			virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
			{
				if (!IsInstanceEditableProperty(InProperty)) { return true; }
				return FObjectWriter::ShouldSkipProperty(InProperty);
			}
		};

		class FDeltaReader : public FObjectReader
		{
		public:
			using FObjectReader::FObjectReader;

			virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
			{
				if (!IsInstanceEditableProperty(InProperty)) { return true; }
				return FObjectReader::ShouldSkipProperty(InProperty);
			}
		};

		static void SerializeObjectDelta(
			UObject* Object,
			UObject* Defaults,
			TArray<uint8>& OutBytes)
		{
			UClass* Class = Object->GetClass();
			FDeltaWriter Writer(OutBytes);
			FStructuredArchiveFromArchive Adapter(Writer);
			Class->SerializeTaggedProperties(
				Adapter.GetSlot(),
				reinterpret_cast<uint8*>(Object),
				Class,
				reinterpret_cast<uint8*>(Defaults),
				Object);
		}

		static void DeserializeObjectDelta(
			UObject* Object,
			const TArray<uint8>& InBytes)
		{
			UClass* Class = Object->GetClass();
			FDeltaReader Reader(InBytes);
			FStructuredArchiveFromArchive Adapter(Reader);
			Class->SerializeTaggedProperties(
				Adapter.GetSlot(),
				reinterpret_cast<uint8*>(Object),
				Class,
				reinterpret_cast<uint8*>(Class->GetDefaultObject()),
				Object);
		}
	}

	TArray<uint8> SerializeActorDelta(AActor* Actor)
	{
		if (!Actor) { return {}; }

		// Serialize actor-level properties
		UClass* ActorClass = Actor->GetClass();
		UObject* ActorCDO = ActorClass->GetDefaultObject();

		TArray<uint8> ActorBytes;
		Internal::SerializeObjectDelta(Actor, ActorCDO, ActorBytes);

		// Collect component deltas
		struct FComponentDelta
		{
			FName Name;
			TArray<uint8> Bytes;
		};
		TArray<FComponentDelta> ComponentDeltas;

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			UObject* Archetype = Component->GetArchetype();
			if (!Archetype || Archetype == Component) { continue; }

			// Skip components whose archetype is the raw class CDO — these have no
			// actor-specific baseline to diff against (engine-managed or dynamically added
			// without a matching template on the actor CDO). Components from
			// CreateDefaultSubobject or Blueprint SCS have an archetype that lives on the
			// actor CDO, giving a meaningful per-actor baseline.
			if (Archetype == Component->GetClass()->GetDefaultObject()) { continue; }

			if (Component->GetClass() != Archetype->GetClass()) { continue; }

			TArray<uint8> CompBytes;
			Internal::SerializeObjectDelta(Component, Archetype, CompBytes);

			if (!CompBytes.IsEmpty())
			{
				ComponentDeltas.Add({ Component->GetFName(), MoveTemp(CompBytes) });
			}
		}

		// If nothing changed at all, return empty
		if (ActorBytes.IsEmpty() && ComponentDeltas.IsEmpty())
		{
			return {};
		}

		// Pack into wire format:
		//   [uint32 ActorDeltaSize][ActorDelta...]
		//   [uint32 ComponentCount]
		//   For each: [FName][uint32 CompDeltaSize][CompDelta...]
		TArray<uint8> Result;
		FMemoryWriter Writer(Result);

		uint32 ActorSize = ActorBytes.Num();
		Writer << ActorSize;
		if (ActorSize > 0)
		{
			Writer.Serialize(ActorBytes.GetData(), ActorSize);
		}

		uint32 CompCount = ComponentDeltas.Num();
		Writer << CompCount;

		for (FComponentDelta& CD : ComponentDeltas)
		{
			Writer << CD.Name;
			uint32 CompSize = CD.Bytes.Num();
			Writer << CompSize;
			Writer.Serialize(CD.Bytes.GetData(), CompSize);
		}

		return Result;
	}

	void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes)
	{
		if (!Actor || DeltaBytes.IsEmpty()) { return; }

		FMemoryReader Reader(DeltaBytes);

		// Read actor-level delta
		uint32 ActorSize = 0;
		Reader << ActorSize;
		if (ActorSize > 0)
		{
			TArray<uint8> ActorBytes;
			ActorBytes.SetNumUninitialized(ActorSize);
			Reader.Serialize(ActorBytes.GetData(), ActorSize);
			Internal::DeserializeObjectDelta(Actor, ActorBytes);
		}

		// Read component deltas
		uint32 CompCount = 0;
		Reader << CompCount;

		for (uint32 i = 0; i < CompCount; ++i)
		{
			FName CompName;
			Reader << CompName;

			uint32 CompSize = 0;
			Reader << CompSize;

			TArray<uint8> CompBytes;
			CompBytes.SetNumUninitialized(CompSize);
			Reader.Serialize(CompBytes.GetData(), CompSize);

			// Find matching component by name — skip if missing/renamed
			if (UActorComponent* Component = FindObjectFast<UActorComponent>(Actor, CompName))
			{
				Internal::DeserializeObjectDelta(Component, CompBytes);
			}
		}
	}

	uint32 HashDelta(const TArray<uint8>& DeltaBytes)
	{
		if (DeltaBytes.IsEmpty()) { return 0; }
		return FCrc::MemCrc32(DeltaBytes.GetData(), DeltaBytes.Num());
	}
}
