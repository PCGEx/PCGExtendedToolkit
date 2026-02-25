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

		// FObjectWriter/FObjectReader are UObject-aware memory archives that handle
		// TObjectPtr, FSoftObjectPtr, etc. We subclass to filter via ShouldSkipProperty
		// so only user-editable properties are included in the delta.

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

		/** Quick check: does the object have any instance-editable property that differs from defaults? */
		static bool HasInstanceEditableDelta(UObject* Object, UObject* Defaults)
		{
			for (FProperty* Property = Object->GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				if (!IsInstanceEditableProperty(Property)) { continue; }
				if (!Property->Identical_InContainer(Object, Defaults)) { return true; }
			}
			return false;
		}

		/** Serialize only the properties that differ from Defaults into OutBytes.
		 *  Skips entirely if nothing differs — avoids the ~13-byte terminator overhead
		 *  that SerializeTaggedProperties writes even when no properties are emitted. */
		static void SerializeObjectDelta(
			UObject* Object,
			UObject* Defaults,
			TArray<uint8>& OutBytes)
		{
			if (!HasInstanceEditableDelta(Object, Defaults)) { return; }

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

		/** Deserialize delta bytes onto Object; properties not present in the delta are untouched.
		 *  Uses CDO as the diff baseline so tagged properties resolve correctly. */
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

		// Actor-level: diff instance against its CDO
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

			// Components from CreateDefaultSubobject/Blueprint SCS have an archetype that
			// lives on the actor CDO — these give a meaningful per-actor baseline to diff
			// against. Engine-managed components (scene root, etc.) have the raw class CDO
			// as archetype instead; skip those as they have no user-defined baseline.
			if (Archetype == Component->GetClass()->GetDefaultObject()) { continue; }

			// Class mismatch = archetype from a different version/refactor; skip safely
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

		// Unpack wire format written by SerializeActorDelta.
		// Bounds-check every read to handle corrupted/truncated data gracefully.
		FMemoryReader Reader(DeltaBytes);
		const int64 TotalSize = DeltaBytes.Num();

		// Actor-level delta
		uint32 ActorSize = 0;
		Reader << ActorSize;
		if (ActorSize > 0)
		{
			if (Reader.Tell() + ActorSize > TotalSize) { return; }

			TArray<uint8> ActorBytes;
			ActorBytes.SetNumUninitialized(ActorSize);
			Reader.Serialize(ActorBytes.GetData(), ActorSize);
			Internal::DeserializeObjectDelta(Actor, ActorBytes);
		}

		// Component deltas — matched by subobject name
		if (Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize) { return; }

		uint32 CompCount = 0;
		Reader << CompCount;

		for (uint32 i = 0; i < CompCount; ++i)
		{
			if (Reader.Tell() >= TotalSize) { return; }

			FName CompName;
			Reader << CompName;

			if (Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize) { return; }

			uint32 CompSize = 0;
			Reader << CompSize;

			if (CompSize == 0) { continue; }
			if (Reader.Tell() + CompSize > TotalSize) { return; }

			TArray<uint8> CompBytes;
			CompBytes.SetNumUninitialized(CompSize);
			Reader.Serialize(CompBytes.GetData(), CompSize);

			// Skip if target actor doesn't have a component with this name
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
