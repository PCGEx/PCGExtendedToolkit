// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyDelta.h"

#include "ComponentReregisterContext.h"
#include "PCGExLog.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/SerializedPropertyScope.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "Types/PCGExTypeOpsString.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace PCGExActorDelta
{
	// Wire format magic and version.
	// The delta bytes are persisted to disk in the collection's .uasset and must survive
	// editor restarts. Older formats without this header (or with older versions) are
	// silently skipped by ApplyPropertyDelta -- the user must rebuild the collection to
	// regenerate deltas in the current format.
	//
	// Format (v6): per-property, with a trailing subobject section at every object level.
	//   - FNames and object references are serialized as strings (FSoftObjectPath for objects) so
	//     the bytes are session-portable. Both operator<<(UObject*&) AND operator<<(FObjectPtr&)
	//     route through that path: FObjectProperty::SerializeItem for TObjectPtr<T>-backed storage
	//     goes through the FObjectPtr virtual, and the base FObjectWriter would write a raw handle
	//     that is a stale address after editor restart.
	//   - Outer-ownership detection (Inst->GetOuter() == Object) for subobjects instead of
	//     CPF_InstancedReference: UHT only sets that flag for the Instanced keyword /
	//     DefaultToInstanced, so CreateDefaultSubobject / NewObject(this, ...) subobjects would be
	//     missed. Runtime outer-ownership is the ground truth.
	//   - Relaxed filter for subobject contents (non-Edit UPROPERTY fields captured)
	//     since subobject state is typically internal, not user-facing.
	//   - Runtime component recreation: class path is stored alongside each component
	//     delta so apply can NewObject + AddInstanceComponent + SetupAttachment if
	//     the spawned target doesn't have a subobject by that name.
	//   - Dynamic components (archetype == class CDO) are captured/applied instead of being skipped.
	//   - operator<<(UObject*&) also intercepts Instanced refs nested in structs and containers
	//     (gated to non-class-member positions only).
	//   - Cycle guard via TSet<const UObject*> threaded through the recursion.
	//   - Optional trailing sections, written only when non-empty: tagged sections at the end of each object
	//     delta (instanced containers, created subobjects), delta-extension payloads after the last component
	//     record. A reader stops where it knows no more, so these needed no version bump.
	static constexpr uint32 DeltaWireMagic = 0x50434745u; // 'PCGE'
	static constexpr uint32 DeltaWireVersion = 6u;

	static constexpr uint32 ExtensionSectionMagic = 0x4558544Eu; // 'EXTN'
	static constexpr uint32 ExtensionSectionVersion = 1u;

	// Past this, a created subobject's record is reported once: a large capture is usually derived data.
	static constexpr int32 CreatedSubobjectReportSize = 64 * 1024;

	// Tags of the optional sections closing an object delta.
	static constexpr uint32 InstancedContainersTag = 0x494E5354u; // 'INST'
	static constexpr uint32 CreatedSubobjectsTag = 0x43525444u;   // 'CRTD'

#pragma region ExtensionRegistry

	namespace ExtensionRegistry
	{
		/** Either Class (direct registration) or PendingPath (soft registration resolving lazily) names the target. */
		struct FEntry
		{
			FSoftClassPath PendingPath;
			TWeakObjectPtr<UClass> Class;
			FDeltaExtension Extension;
		};

		// Game thread only: registration from module startup, lookups from capture and apply. No locking.
		// Entries are keyed by a monotonic id so removals don't invalidate the buckets, which are keyed by the resolved
		// class: a lookup walks the target's class hierarchy with one bucket probe per super.
		TMap<uint64, FEntry>& GetEntries()
		{
			static TMap<uint64, FEntry> Instance;
			return Instance;
		}

		TMap<UClass*, TArray<uint64>>& GetBuckets()
		{
			static TMap<UClass*, TArray<uint64>> Instance;
			return Instance;
		}

		/** Registrations whose soft path has not resolved yet. */
		TArray<uint64>& GetPendingIds()
		{
			static TArray<uint64> Instance;
			return Instance;
		}

		/** Payload Id -> registration, for the extensions that Capture or Apply. */
		TMap<FName, uint64>& GetIdIndex()
		{
			static TMap<FName, uint64> Instance;
			return Instance;
		}

		uint64 AllocId()
		{
			static uint64 Counter = 0;
			return ++Counter;
		}

		/** Resolves the entry's class and files it in its bucket. False while the soft path does not resolve. */
		bool TryBucket(const uint64 Id)
		{
			FEntry* E = GetEntries().Find(Id);
			if (!E)
			{
				return false;
			}

			UClass* Target = E->Class.Get();
			if (!Target && !E->PendingPath.IsNull())
			{
				Target = E->PendingPath.ResolveClass();
				E->Class = Target;
			}

			if (!Target)
			{
				return false;
			}

			if (E->Extension.PostApply && !Target->IsChildOf<UActorComponent>())
			{
				UE_LOG(LogPCGEx, Warning,
				       TEXT("[PCGExActorDelta] Extension '%s' on %s: PostApply runs on components only, it is dropped."),
				       *E->Extension.Id.ToString(), *Target->GetName());
				E->Extension.PostApply = nullptr;
			}

			GetBuckets().FindOrAdd(Target).AddUnique(Id);
			return true;
		}

		/** Cheap when nothing is pending (a hash lookup per entry otherwise), so it runs on every capture and apply
		 *  instead of listening for module loads. */
		void TryResolvePending()
		{
			TArray<uint64>& Pending = GetPendingIds();
			for (int32 i = Pending.Num() - 1; i >= 0; --i)
			{
				if (!GetEntries().Contains(Pending[i]) || TryBucket(Pending[i]))
				{
					Pending.RemoveAtSwap(i, EAllowShrinking::No);
				}
			}
		}

		void Unregister(const uint64 Id)
		{
			if (Id == 0)
			{
				return;
			}

			FEntry Removed;
			if (!GetEntries().RemoveAndCopyValue(Id, Removed))
			{
				return;
			}

			if (const uint64* Indexed = GetIdIndex().Find(Removed.Extension.Id); Indexed && *Indexed == Id)
			{
				GetIdIndex().Remove(Removed.Extension.Id);
			}

			if (UClass* C = Removed.Class.Get())
			{
				if (TArray<uint64>* Bucket = GetBuckets().Find(C))
				{
					Bucket->Remove(Id);
					if (Bucket->IsEmpty())
					{
						GetBuckets().Remove(C);
					}
				}
			}
			else
			{
				// The class was collected: its bucket key is a dangling address, so sweep every bucket for the id.
				for (auto It = GetBuckets().CreateIterator(); It; ++It)
				{
					It.Value().Remove(Id);
					if (It.Value().IsEmpty())
					{
						It.RemoveCurrent();
					}
				}
			}

			GetPendingIds().Remove(Id);
		}

		uint64 Register(UClass* Class, const FSoftClassPath& Path, FDeltaExtension&& Extension)
		{
			if (!Class && Path.IsNull())
			{
				return 0;
			}

			const bool bHasPayload = Extension.Capture || Extension.Apply;
			if (!bHasPayload && !Extension.PostApply && Extension.ClaimedProperties.IsEmpty())
			{
				return 0;
			}

			if (bHasPayload)
			{
				if (Extension.Id.IsNone())
				{
					UE_LOG(LogPCGEx, Warning, TEXT("[PCGExActorDelta] Rejected a delta extension that Captures or Applies without an Id."));
					return 0;
				}
				if (GetIdIndex().Contains(Extension.Id))
				{
					UE_LOG(LogPCGEx, Warning, TEXT("[PCGExActorDelta] Rejected delta extension '%s': that Id is already registered."), *Extension.Id.ToString());
					return 0;
				}
			}

			const uint64 Id = AllocId();
			if (bHasPayload)
			{
				GetIdIndex().Add(Extension.Id, Id);
			}

			FEntry Entry;
			Entry.PendingPath = Path;
			Entry.Class = Class;
			Entry.Extension = MoveTemp(Extension);
			GetEntries().Add(Id, MoveTemp(Entry));

			if (!TryBucket(Id))
			{
				GetPendingIds().AddUnique(Id);
			}
			return Id;
		}

		/** Live registrations matching Class or one of its supers, most-derived first. */
		void GatherMatching(UClass* Class, TArray<uint64, TInlineAllocator<8>>& OutIds)
		{
			TMap<UClass*, TArray<uint64>>& Buckets = GetBuckets();
			if (Buckets.IsEmpty())
			{
				return;
			}

			const TMap<uint64, FEntry>& Entries = GetEntries();
			for (UClass* Cls = Class; Cls; Cls = Cls->GetSuperClass())
			{
				const TArray<uint64>* Bucket = Buckets.Find(Cls);
				if (!Bucket)
				{
					continue;
				}

				for (const uint64 Id : *Bucket)
				{
					const FEntry* E = Entries.Find(Id);
					if (!E)
					{
						continue;
					}

					// A bucket key is a raw UClass*: a collected class's address can be reused by a new one. The entry's
					// weak pointer is the liveness check.
					if (E->Class.Get() != Cls)
					{
						UE_LOG(LogPCGEx, Warning,
						       TEXT("[PCGExActorDelta] Skipping a delta extension whose target class was garbage-collected; the registration is now stale."));
						continue;
					}

					OutIds.Add(Id);
				}
			}
		}

		const FDeltaExtension* Find(const uint64 Id)
		{
			const FEntry* E = GetEntries().Find(Id);
			return E ? &E->Extension : nullptr;
		}

		/** The live, resolved registration of a payload Id, and its class. */
		const FDeltaExtension* FindById(const FName PayloadId, UClass*& OutClass)
		{
			const uint64* Id = GetIdIndex().Find(PayloadId);
			const FEntry* E = Id ? GetEntries().Find(*Id) : nullptr;
			OutClass = E ? E->Class.Get() : nullptr;
			return OutClass ? &E->Extension : nullptr;
		}

		/** Properties the extensions matching Class keep out of the generic delta. */
		void GatherClaims(UClass* Class, TArray<FName, TInlineAllocator<4>>& OutClaims)
		{
			TArray<uint64, TInlineAllocator<8>> Ids;
			GatherMatching(Class, Ids);
			for (const uint64 Id : Ids)
			{
				OutClaims.Append(Find(Id)->ClaimedProperties);
			}
		}

		void RunPostApply(AActor* Actor)
		{
			TryResolvePending();
			if (GetBuckets().IsEmpty())
			{
				return;
			}

			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			TArray<uint64, TInlineAllocator<8>> Ids;
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}

				Ids.Reset();
				GatherMatching(Component->GetClass(), Ids);
				if (Ids.IsEmpty())
				{
					continue;
				}

				UObject* Archetype = Component->GetArchetype();
				for (const uint64 Id : Ids)
				{
					// Looked up again: an earlier fixup may have unregistered this one.
					if (const FDeltaExtension* Extension = Find(Id); Extension && Extension->PostApply)
					{
						Extension->PostApply(Component, Archetype);
					}
				}
			}
		}
	}

#pragma endregion

#pragma region Internal

	namespace Internal
	{
		/** Only user-editable instance properties at the TOP level of an actor/component.
		 *  Excludes engine bookkeeping (ActorGuid, tick state, net role, ...) that always
		 *  differs from the CDO but doesn't represent user intent. */
		static bool IsInstanceEditableProperty(const FProperty* InProperty)
		{
			return InProperty->HasAnyPropertyFlags(CPF_Edit)
				&& !InProperty->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		}

		/** Relative-transform props on the root component are driven by the PCG point, never
		 *  by the source actor. Including them in the delta would overwrite the spawn
		 *  transform with the source actor's (often near-origin) values. Non-root scene
		 *  components legitimately use these props to describe their attachment offset, so
		 *  the filter only fires when the caller flags the object as "this is the root". */
		static bool IsTransformProperty(const FProperty* InProperty)
		{
			const FName PropName = InProperty->GetFName();
			static const FName NAME_RelativeLocation(TEXT("RelativeLocation"));
			static const FName NAME_RelativeRotation(TEXT("RelativeRotation"));
			static const FName NAME_RelativeScale3D(TEXT("RelativeScale3D"));
			return PropName == NAME_RelativeLocation
				|| PropName == NAME_RelativeRotation
				|| PropName == NAME_RelativeScale3D;
		}

		/**
		 * Top-level property filter, branching on whether we're serializing an actor /
		 * component (bIsSubobject=false) or a subobject (true).
		 *
		 *  - Top level: user-editable properties only. Engine bookkeeping would drown the
		 *    delta in noise. Root-component relative transform fields are additionally
		 *    excluded when bSkipTransformProps is set, because they fight the spawn
		 *    transform; non-root components keep them so child-component offsets survive.
		 *  - Subobject: any non-transient, non-deprecated UPROPERTY field. Subobject state is
		 *    typically internal (no details-panel exposure), so requiring CPF_Edit would drop
		 *    exactly the state we came to capture -- UVoxelSplineMetadata::GuidToValues and
		 *    similar plain-UPROPERTY TMap/TArray backing stores.
		 */
		static bool ShouldIncludeProperty(const FProperty* Property, bool bIsSubobject, bool bSkipTransformProps)
		{
			if (bIsSubobject)
			{
				return !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated);
			}
			if (!IsInstanceEditableProperty(Property))
			{
				return false;
			}
			if (bSkipTransformProps && IsTransformProperty(Property))
			{
				return false;
			}
			return true;
		}

		// Forward declaration so the archive's operator<<(UObject*&) can call back for
		// nested-Instanced recursion.
		static void SerializeObjectDelta(
			UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes,
			TSet<const UObject*>& VisitedSources, bool bIsSubobject, bool bSkipTransformProps,
			TSet<FSoftObjectPath>* CollateralSink);

		static void DeserializeObjectDelta(UObject* Object, const TArray<uint8>& InBytes);

		void ApplyCreatedRecord(UObject* Object, const FString& PropName, const FString& ClassPathStr, const TArray<uint8>& SubBytes);

		/** A nested-Instanced ref position: an Instanced FObjectProperty owned by a
		 *  struct (or container element), reached via SerializeItem recursing past the
		 *  top-level property layer. The operator<<(UObject*&) override inlines a
		 *  recursive object-delta in this case; everything else falls through to the
		 *  soft-path write/read. Returns that property, or null. */
		const FObjectPropertyBase* AsNestedInstancedRef(const FProperty* CurProp)
		{
			// FSetProperty::SerializeItem loads into an empty set without making its element current (its save and
			// slow load paths do), so the set itself is current then.
			if (const FSetProperty* SetProp = CastField<FSetProperty>(CurProp))
			{
				CurProp = SetProp->GetElementProperty();
			}

			const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(CurProp);
			return P
				&& P->HasAnyPropertyFlags(CPF_InstancedReference)
				&& P->GetOwner<UClass>() == nullptr
					? P
					: nullptr;
		}

		/** A top-level container of instanced objects. Its records go to the 'INST' section, written and read with the
		 *  container current on the archive: a container only makes its elements current when it is current itself
		 *  (FSerializedPropertyScope's OnlyIfOldProperty), and the nested records key off the element. Struct members need
		 *  no section, tagged serialization makes each of them current. */
		bool IsInstancedContainer(const FProperty* Property)
		{
			return Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference) && !Property->IsA<FStructProperty>();
		}

		/** One log line per distinct message per session, for what every spawn of an entry would repeat. Game thread only. */
		void ReportOnce(const FString& Message)
		{
			static TSet<FString> Reported;
			bool bAlreadyReported = false;
			Reported.Add(Message, &bAlreadyReported);
			if (!bAlreadyReported)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[PCGExActorDelta] %s"), *Message);
			}
		}

		/** What a delta may record and recreate as an instance-owned subobject. Actors and components have paths of their
		 *  own; body setups are collision their component rebuilds, and can be large. */
		bool IsRecreatableSubobjectClass(const UClass* Class)
		{
			return Class
				&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& !Class->IsChildOf<AActor>()
				&& !Class->IsChildOf<UActorComponent>()
				&& !Class->IsChildOf<UBodySetup>();
		}

		/** A recreated object of a non-native class needs its class loaded first: list it with the delta's collaterals. */
		void AddClassCollateral(const UClass* Class, TSet<FSoftObjectPath>* CollateralSink)
		{
			if (!CollateralSink || Class->HasAnyClassFlags(CLASS_Native))
			{
				return;
			}

			FSoftObjectPath ClassPath(Class);
			if (ClassPath.IsAsset())
			{
				CollateralSink->Add(MoveTemp(ClassPath));
			}
		}

		/** Moves an object out of Owner once Owner stops referencing it, so it is not saved as an orphan. */
		void RetireOwnedObject(const UObject* Owner, UObject* Object)
		{
			if (Object && Object->GetOuter() == Owner)
			{
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
		}

		/** Current when Owner owns it as exactly Class; otherwise a new Class object under Owner, Current retired. Flags
		 *  follow the owner, as the editor's actor factories do. */
		UObject* ProvideOwnedObject(UObject* Owner, UObject* Current, UClass* Class)
		{
			if (Current && Current->GetOuter() == Owner && Current->GetClass() == Class)
			{
				return Current;
			}

			RetireOwnedObject(Owner, Current);
			const EObjectFlags Flags = Owner->GetFlags() & (RF_Transactional | RF_Transient | RF_NonPIEDuplicateTransient);
			return NewObject<UObject>(Owner, Class, NAME_None, Flags);
		}

		/**
		 * Memory archive subclasses layered over FObjectWriter/FObjectReader.
		 *
		 * FName override:
		 *   Serialize as string. The default FArchive FName path uses session-local name-
		 *   table indices, which aren't stable across editor restarts. Writing as string
		 *   makes the stream session-portable.
		 *
		 * operator<<(UObject*&) override (writer/reader):
		 *   The base FObjectWriter writes UObject*& as the raw 8-byte pointer value --
		 *   meaningless once persisted. We override to handle two cases:
		 *
		 *   1. Nested-Instanced refs (see AsNestedInstancedRef). Inlines a recursive
		 *      object-delta so the nested subobject's contents round-trip. Top-level
		 *      Instanced refs are routed through the trailing subobject section in
		 *      SerializeObjectDelta instead, so they never reach this override.
		 *
		 *      Payload:
		 *        [uint8  Present]         0 = null, 1 = subobject present
		 *        If Present:
		 *          [FString ClassPath]
		 *          [uint32  SubSize]
		 *          [SubSize bytes]        recursive object delta vs. the subobject's class CDO
		 *
		 *   2. All other UObject*& positions (asset references, struct-inner non-Instanced
		 *      refs, etc.). Serialized as FSoftObjectPath strings so the reference
		 *      survives across sessions.
		 *
		 *      Payload: [FString PathStr]   empty string = null
		 */
		class FDeltaWriter : public FObjectWriter
		{
		public:
			using FObjectWriter::FObjectWriter;

			/** Cycle guard shared across the capture tree. Nested operator<<(UObject*&)
			 *  recursion propagates this into the SerializeObjectDelta call so the same
			 *  source object never serializes twice. */
			TSet<const UObject*>* VisitedSources = nullptr;

			/** Optional sink for FSoftObjectPath strings emitted via the soft-path fallback
			 *  in operator<<(UObject*&). Populated for any non-null external reference the
			 *  delta captures (asset refs, foreign-outer object refs). The spawn element
			 *  reads these to async-batch-load referenced assets before applying the delta,
			 *  so FSoftObjectPath::ResolveObject inside the reader succeeds without sync
			 *  loads. nullptr = don't collect. */
			TSet<FSoftObjectPath>* CollateralSink = nullptr;

			virtual FArchive& operator<<(FName& Name) override
			{
				FString AsString = Name.ToString();
				*this << AsString;
				return *this;
			}

			virtual FArchive& operator<<(UObject*& Res) override;

			/** TObjectPtr<T>-backed properties serialize through this virtual in UE 5.6/5.7,
			 *  bypassing operator<<(UObject*&). Route both through the same soft-path code. */
			virtual FArchive& operator<<(FObjectPtr& Value) override
			{
				UObject* Raw = Value.Get();
				*this << Raw;
				return *this;
			}
		};

		class FDeltaReader : public FObjectReader
		{
		public:
			using FObjectReader::FObjectReader;

			/** The object being read into: nested-Instanced objects are applied onto, or created under, it. */
			UObject* Owner = nullptr;

			/** Reading an instanced container record from the property section, where only writers predating the 'INST'
			 *  section put one: its elements are paths to the source actor's own subobjects. */
			bool bLegacyInstancedContainer = false;

			virtual FArchive& operator<<(FName& Name) override
			{
				FString AsString;
				*this << AsString;
				Name = PCGExTypeOps::MakeNameChecked(AsString);
				return *this;
			}

			virtual FArchive& operator<<(UObject*& Res) override;

			/** TObjectPtr<T>-backed properties serialize through this virtual in UE 5.6/5.7,
			 *  bypassing operator<<(UObject*&). Only a nested-Instanced position reads the current
			 *  value, an owned subobject to apply onto. Anywhere else Raw starts null: on a freshly
			 *  archetype-cloned target, calling Get() on every TObjectPtr triggers lazy resolution /
			 *  sync-loads and turns each spawn into a multi-second pause. We accept the trade-off that
			 *  source-was-null intent isn't preserved (target stays at archetype baseline instead of
			 *  being nulled out) -- archetype baseline is almost always safer than nulling a
			 *  slot the component may require non-null. */
			virtual FArchive& operator<<(FObjectPtr& Value) override
			{
				UObject* Raw = AsNestedInstancedRef(GetSerializedProperty()) ? Value.Get() : nullptr;
				*this << Raw;
				if (Raw)
				{
					Value = Raw;
				}
				return *this;
			}
		};

		/** A direct FObjectProperty field on Object whose value is outer-owned by Object --
		 *  i.e. a subobject the SerializeObjectDelta code should recurse into rather than
		 *  writing as a pointer. */
		struct FSubobjectBinding
		{
			FString PropName;
			UObject* Instance;
			UObject* Defaults; // The parallel default subobject, or the class CDO of a created one.
			bool bCreated;     // Defaults hold no same-class subobject: recorded with its class, recreated on apply.
		};

		/**
		 * Collect outer-owned subobjects reachable from Object via direct FObjectProperty
		 * fields.
		 *
		 * Outer-ownership (Inst->GetOuter() == Object) is the robust discriminator: both
		 * CreateDefaultSubobject and NewObject(this, ...) outer the child to the owner,
		 * while external references (asset pointers, other actors) have a different outer.
		 * We intentionally do NOT rely on CPF_InstancedReference -- UHT only sets it for
		 * UPROPERTY(Instanced) / DefaultToInstanced, which many plugins don't bother with
		 * even when their subobjects are genuinely per-instance-owned.
		 *
		 * When Defaults hold a parallel subobject of the same class, the recursive diff runs
		 * against it. Otherwise (the instance created it, or changed its class) the subobject
		 * is diffed against its class CDO and recorded with its class, so apply can recreate
		 * it. Transient instances and classes IsRecreatableSubobjectClass refuses are skipped.
		 *
		 * UActorComponent instances are explicitly skipped at the actor level: they're
		 * outered to the actor (both CDSO and SCS-created), so without this guard
		 * SerializeActorDelta would double-serialize each component -- once via its outer
		 * component loop, once by recursing into it as a subobject here.
		 */
		static void CollectSubobjects(UObject* Object, UObject* Defaults, TConstArrayView<FName> Claims, TArray<FSubobjectBinding>& Out)
		{
			UClass* Class = Object->GetClass();
			for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
				if (!ObjProp)
				{
					continue;
				}
				if (ObjProp->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
				{
					continue;
				}
				if (Claims.Contains(ObjProp->GetFName()))
				{
					continue;
				}

				UObject* Inst = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Object));
				if (!Inst || Inst->GetOuter() != Object)
				{
					continue;
				}

				// Components are handled by SerializeActorDelta's component loop. Recursing
				// into them here at the actor level would double-serialize their state.
				if (Inst->IsA<UActorComponent>())
				{
					continue;
				}

				UObject* Def = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Defaults));
				if (Def && Def->GetClass() == Inst->GetClass())
				{
					Out.Add({ObjProp->GetName(), Inst, Def, false});
					continue;
				}

				if (Inst->HasAnyFlags(RF_Transient) || !IsRecreatableSubobjectClass(Inst->GetClass()))
				{
					continue;
				}

				Out.Add({ObjProp->GetName(), Inst, Inst->GetClass()->GetDefaultObject(), true});
			}
		}

		/** [uint32 Count], then [FString PropName][uint32 ValueSize][ValueSize bytes] per property. bScoped serializes each
		 *  value with its property current on the archive (see IsInstancedContainer). */
		void WritePropertyRecords(
			FArchive& Writer, UObject* Object, UObject* Defaults, TConstArrayView<FProperty*> Properties, const bool bScoped,
			TSet<const UObject*>& VisitedSources, TSet<FSoftObjectPath>* CollateralSink)
		{
			uint32 Count = Properties.Num();
			Writer << Count;

			for (FProperty* Property : Properties)
			{
				FString PropName = Property->GetName();
				Writer << PropName;

				// Write the value into its own buffer first so we can length-prefix it.
				TArray<uint8> ValueBytes;
				{
					FDeltaWriter ValueWriter(ValueBytes);
					// Inherit the outer writer's archive state so custom-versioned structs
					// (FSpline uses FFortniteMainBranchObjectVersion etc.) round-trip with
					// consistent versions, share the visited set so nested
					// operator<<(UObject*&) recursion can be cycle-safe, and share the
					// collateral sink so soft paths emitted by per-property writes land
					// in the same outer collection.
					ValueWriter.SetCustomVersions(Writer.GetCustomVersions());
					ValueWriter.VisitedSources = &VisitedSources;
					ValueWriter.CollateralSink = CollateralSink;
					FSerializedPropertyScope SerializedProperty(ValueWriter, bScoped ? Property : nullptr);
					Property->SerializeItem(
						FStructuredArchiveFromArchive(ValueWriter).GetSlot(),
						Property->ContainerPtrToValuePtr<void>(Object),
						Property->ContainerPtrToValuePtr<void>(Defaults));
				}

				uint32 ValueSize = ValueBytes.Num();
				Writer << ValueSize;
				if (ValueSize > 0)
				{
					Writer.Serialize(ValueBytes.GetData(), ValueSize);
				}
			}
		}

		void WriteSection(FArchive& Writer, uint32 Tag, TArray<uint8>& SectionBytes)
		{
			uint32 SectionSize = SectionBytes.Num();
			Writer << Tag;
			Writer << SectionSize;
			Writer.Serialize(SectionBytes.GetData(), SectionSize);
		}

		/** Per-object inner format, wrapped by SerializeActorDelta's outer container:
		 *
		 *    [uint32 PropCount]
		 *    repeated PropCount times:
		 *      [FString PropName]      // FName as string (session-portable)
		 *      [uint32 ValueSize]
		 *      [ValueSize bytes]       // FProperty::SerializeItem output (may contain
		 *                              //   nested Instanced payloads produced by our
		 *                              //   operator<<(UObject*&) override; see FDeltaWriter)
		 *    [uint32 SubCount]
		 *    repeated SubCount times:
		 *      [FString PropName]      // name of the FObjectProperty holding the subobject
		 *      [uint32 SubSize]
		 *      [SubSize bytes]         // recursive SerializeObjectDelta output
		 *    then optional sections, each [uint32 Tag][uint32 Size][Size bytes], written only when non-empty:
		 *      'INST': [uint32 PropCount] + property records as above, for IsInstancedContainer properties
		 *      'CRTD': [uint32 CreatedCount], repeated CreatedCount times:
		 *        [FString PropName]
		 *        [FString ClassPath]   // FSoftClassPath of the instance-owned subobject
		 *        [uint32 SubSize]      // may be 0: the record's existence recreates the object
		 *        [SubSize bytes]       // recursive SerializeObjectDelta output vs. the class CDO
		 *
		 *  Each segment is length-prefixed so unknown/renamed props, subobjects or sections on
		 *  apply seek past their payload without corrupting subsequent reads. */
		static void SerializeObjectDelta(
			UObject* Object,
			UObject* Defaults,
			TArray<uint8>& OutBytes,
			TSet<const UObject*>& VisitedSources,
			bool bIsSubobject = false,
			bool bSkipTransformProps = false,
			TSet<FSoftObjectPath>* CollateralSink = nullptr)
		{
			if (!Object || !Defaults)
			{
				return;
			}

			// Cycle guard. In practice outer-ownership graphs are trees (UE disallows cycles
			// in the outer chain), but operator<< recursion through non-outer-owned
			// Instanced refs could theoretically loop. Cheap safety.
			bool bAlreadyVisited = false;
			VisitedSources.Add(Object, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				return;
			}

			UClass* Class = Object->GetClass();

			TArray<FName, TInlineAllocator<4>> Claims;
			ExtensionRegistry::GatherClaims(Class, Claims);

			TArray<FProperty*> PropsToWrite;
			TArray<FProperty*> ContainersToWrite;
			for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				if (!ShouldIncludeProperty(Property, bIsSubobject, bSkipTransformProps))
				{
					continue;
				}

				if (Claims.Contains(Property->GetFName()))
				{
					continue;
				}

				// Outer-owned subobject refs at the top level are routed to the trailing
				// subobject section below. Serializing them at the property layer writes
				// the source's subobject path as a pointer, which either misresolves on
				// apply or cross-links to a foreign subobject -- and we'd also apply the
				// subobject delta, doubling state.
				if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
				{
					UObject* Inst = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Object));
					if (Inst && Inst->GetOuter() == Object)
					{
						continue;
					}
				}

				if (Property->Identical_InContainer(Object, Defaults))
				{
					continue;
				}
				(IsInstancedContainer(Property) ? ContainersToWrite : PropsToWrite).Add(Property);
			}

			TArray<FSubobjectBinding> Subobjects;
			CollectSubobjects(Object, Defaults, Claims, Subobjects);

			struct FSubDelta
			{
				FString PropName;
				FString ClassPath;
				TArray<uint8> Bytes;
			};
			TArray<FSubDelta> SubDeltas;
			TArray<FSubDelta> CreatedDeltas;
			for (const FSubobjectBinding& Sub : Subobjects)
			{
				// One record per subobject, even when two properties hold it.
				if (VisitedSources.Contains(Sub.Instance))
				{
					continue;
				}

				TArray<uint8> SubBytes;
				// Subobjects are by definition not the root component, so transform filtering
				// never applies in this branch. Forward the collateral sink so soft-path
				// references inside nested subobject state still get collected.
				SerializeObjectDelta(Sub.Instance, Sub.Defaults, SubBytes, VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);

				if (!Sub.bCreated)
				{
					if (!SubBytes.IsEmpty())
					{
						SubDeltas.Add({Sub.PropName, FString(), MoveTemp(SubBytes)});
					}
					continue;
				}

				UClass* SubClass = Sub.Instance->GetClass();
				AddClassCollateral(SubClass, CollateralSink);
				if (SubBytes.Num() > CreatedSubobjectReportSize)
				{
					ReportOnce(FString::Printf(
						TEXT("%s::%s records an instance-owned %s over %d KB; check it is not derived data."),
						*Class->GetName(), *Sub.PropName, *SubClass->GetName(), CreatedSubobjectReportSize / 1024));
				}

				// Recorded even when empty: the record's existence is what recreates the object.
				CreatedDeltas.Add({Sub.PropName, FSoftClassPath(SubClass).ToString(), MoveTemp(SubBytes)});
			}

			if (PropsToWrite.IsEmpty() && ContainersToWrite.IsEmpty() && SubDeltas.IsEmpty() && CreatedDeltas.IsEmpty())
			{
				return;
			}

			FDeltaWriter Writer(OutBytes);
			Writer.VisitedSources = &VisitedSources;
			Writer.CollateralSink = CollateralSink;

			WritePropertyRecords(Writer, Object, Defaults, PropsToWrite, /*bScoped=*/false, VisitedSources, CollateralSink);

			uint32 SubCount = SubDeltas.Num();
			Writer << SubCount;
			for (FSubDelta& SD : SubDeltas)
			{
				Writer << SD.PropName;
				uint32 SubSize = SD.Bytes.Num();
				Writer << SubSize;
				if (SubSize > 0)
				{
					Writer.Serialize(SD.Bytes.GetData(), SubSize);
				}
			}

			// The sections are omitted when empty, so other objects keep their bytes and hash.
			if (!ContainersToWrite.IsEmpty())
			{
				TArray<uint8> SectionBytes;
				FMemoryWriter SectionWriter(SectionBytes);
				WritePropertyRecords(SectionWriter, Object, Defaults, ContainersToWrite, /*bScoped=*/true, VisitedSources, CollateralSink);
				WriteSection(Writer, InstancedContainersTag, SectionBytes);
			}

			if (!CreatedDeltas.IsEmpty())
			{
				TArray<uint8> SectionBytes;
				FMemoryWriter SectionWriter(SectionBytes);

				uint32 CreatedCount = CreatedDeltas.Num();
				SectionWriter << CreatedCount;
				for (FSubDelta& CD : CreatedDeltas)
				{
					SectionWriter << CD.PropName;
					SectionWriter << CD.ClassPath;
					uint32 SubSize = CD.Bytes.Num();
					SectionWriter << SubSize;
					if (SubSize > 0)
					{
						SectionWriter.Serialize(CD.Bytes.GetData(), SubSize);
					}
				}

				WriteSection(Writer, CreatedSubobjectsTag, SectionBytes);
			}
		}

		/** Public wrapper: allocate a fresh visited set and defer to the recursive impl. */
		static void SerializeObjectDelta(UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes, bool bIsSubobject = false, bool bSkipTransformProps = false, TSet<FSoftObjectPath>* CollateralSink = nullptr)
		{
			TSet<const UObject*> Visited;
			SerializeObjectDelta(Object, Defaults, OutBytes, Visited, bIsSubobject, bSkipTransformProps, CollateralSink);
		}

		/** Reads records written by WritePropertyRecords onto Object. Missing / renamed properties seek past their value
		 *  instead of corrupting the stream, as does a scoped record whose property is no longer an instanced container:
		 *  its nested payloads would read as soft paths. */
		void ReadPropertyRecords(UObject* Object, FArchive& Reader, const int64 TotalSize, const bool bScoped)
		{
			UClass* Class = Object->GetClass();

			uint32 Count = 0;
			Reader << Count;

			for (uint32 i = 0; i < Count; ++i)
			{
				if (Reader.IsError() || Reader.Tell() >= TotalSize)
				{
					break;
				}

				FString PropName;
				Reader << PropName;

				uint32 ValueSize = 0;
				Reader << ValueSize;

				if (Reader.Tell() + static_cast<int64>(ValueSize) > TotalSize)
				{
					Reader.SetError();
					break;
				}

				const int64 ValueStart = Reader.Tell();

				// The writer filters root-component relative-transform fields at capture; we
				// trust that contract instead of mirroring the filter here. Filtering on read
				// would silently drop legitimate non-root transform overrides that the writer
				// intentionally emitted.
				FProperty* Property = Class->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName));
				if (!Property
					|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated)
					|| (bScoped && !IsInstancedContainer(Property)))
				{
					Reader.Seek(ValueStart + ValueSize);
					continue;
				}

				// A zero-size value segment carries no data -- handing it to SerializeItem
				// would feed an empty buffer to the property handler and could clobber the
				// destination with default-constructed state.
				if (ValueSize == 0)
				{
					continue;
				}

				// Extract the value segment into its own buffer so any over/under-read inside
				// the property handler stays contained and can't desync the outer stream.
				TArray<uint8> ValueBytes;
				ValueBytes.SetNumUninitialized(ValueSize);
				Reader.Serialize(ValueBytes.GetData(), ValueSize);

				FDeltaReader ValueReader(ValueBytes);
				ValueReader.SetCustomVersions(Reader.GetCustomVersions());
				ValueReader.Owner = Object;
				ValueReader.bLegacyInstancedContainer = !bScoped && IsInstancedContainer(Property);
				// Keeps a container's archetype-cloned instanced elements for the nested records to apply onto, instead of
				// emptying it first. Side effect, kept to these properties: a preserved struct element keeps its
				// archetype value for a field the writer omitted as default.
				ValueReader.ArPreserveArrayElements = Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference);
				FSerializedPropertyScope SerializedProperty(ValueReader, bScoped ? Property : nullptr);
				Property->SerializeItem(
					FStructuredArchiveFromArchive(ValueReader).GetSlot(),
					Property->ContainerPtrToValuePtr<void>(Object),
					Property->ContainerPtrToValuePtr<void>(Class->GetDefaultObject()));
			}
		}

		/** Reads the 'CRTD' section's records onto Object. */
		void ReadCreatedRecords(UObject* Object, FArchive& Reader, const int64 TotalSize)
		{
			uint32 CreatedCount = 0;
			Reader << CreatedCount;

			for (uint32 i = 0; i < CreatedCount; ++i)
			{
				if (Reader.IsError() || Reader.Tell() >= TotalSize)
				{
					break;
				}

				FString PropName;
				Reader << PropName;

				FString ClassPathStr;
				Reader << ClassPathStr;

				uint32 SubSize = 0;
				Reader << SubSize;

				if (Reader.IsError() || Reader.Tell() + static_cast<int64>(SubSize) > TotalSize)
				{
					Reader.SetError();
					break;
				}

				TArray<uint8> SubBytes;
				SubBytes.SetNumUninitialized(SubSize);
				if (SubSize > 0)
				{
					Reader.Serialize(SubBytes.GetData(), SubSize);
				}

				ApplyCreatedRecord(Object, PropName, ClassPathStr, SubBytes);
			}
		}

		/** Read per-property segments, then descend into the subobject section, then read the
		 *  optional sections. Missing / renamed properties, mismatched subobjects and unknown
		 *  sections seek past their payload instead of corrupting the outer stream. */
		static void DeserializeObjectDelta(
			UObject* Object,
			const TArray<uint8>& InBytes)
		{
			if (!Object || InBytes.IsEmpty())
			{
				return;
			}

			UClass* Class = Object->GetClass();
			FDeltaReader Reader(InBytes);
			Reader.Owner = Object;
			const int64 TotalSize = InBytes.Num();

			ReadPropertyRecords(Object, Reader, TotalSize, /*bScoped=*/false);

			// Trailing subobject section. Truncated or absent is treated as empty -- the
			// outer wire-version gate has already rejected incompatible formats, so here a
			// missing subobject count means the writer produced zero subobjects.
			if (Reader.IsError() || Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize)
			{
				return;
			}

			uint32 SubCount = 0;
			Reader << SubCount;

			for (uint32 i = 0; i < SubCount; ++i)
			{
				if (Reader.IsError() || Reader.Tell() >= TotalSize)
				{
					break;
				}

				FString PropName;
				Reader << PropName;

				uint32 SubSize = 0;
				Reader << SubSize;

				if (Reader.Tell() + static_cast<int64>(SubSize) > TotalSize)
				{
					Reader.SetError();
					break;
				}

				if (SubSize == 0)
				{
					continue;
				}

				TArray<uint8> SubBytes;
				SubBytes.SetNumUninitialized(SubSize);
				Reader.Serialize(SubBytes.GetData(), SubSize);

				FProperty* Property = Class->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName));
				FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
				if (!ObjProp
					|| ObjProp->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
				{
					continue;
				}

				UObject* Subobj = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Object));
				if (!Subobj)
				{
					continue;
				}
				// Symmetric outer-ownership guard: refuse to write into a foreign object.
				// The target's own archetype-spawned subobject is what the source delta was
				// captured against; anything else could pollute external state.
				if (Subobj->GetOuter() != Object)
				{
					continue;
				}

				DeserializeObjectDelta(Subobj, SubBytes);
			}

			// Optional sections, absent from older payloads. A tag this reader does not know is skipped.
			while (!Reader.IsError() && Reader.Tell() + static_cast<int64>(sizeof(uint32) * 2) <= TotalSize)
			{
				uint32 Tag = 0;
				uint32 SectionSize = 0;
				Reader << Tag;
				Reader << SectionSize;
				if (Reader.Tell() + static_cast<int64>(SectionSize) > TotalSize)
				{
					return;
				}

				TArray<uint8> SectionBytes;
				SectionBytes.SetNumUninitialized(SectionSize);
				Reader.Serialize(SectionBytes.GetData(), SectionSize);

				FMemoryReader SectionReader(SectionBytes);
				SectionReader.SetCustomVersions(Reader.GetCustomVersions());
				switch (Tag)
				{
				case InstancedContainersTag:
					ReadPropertyRecords(Object, SectionReader, SectionSize, /*bScoped=*/true);
					break;
				case CreatedSubobjectsTag:
					ReadCreatedRecords(Object, SectionReader, SectionSize);
					break;
				default:
					break;
				}
			}
		}

		/** Recreates, or reuses when its class matches, the instance-owned subobject one created record describes. */
		void ApplyCreatedRecord(UObject* Object, const FString& PropName, const FString& ClassPathStr, const TArray<uint8>& SubBytes)
		{
			// Property first: an editor-only slot is absent on cooked targets, whose class may not exist either.
			FObjectProperty* ObjProp = CastField<FObjectProperty>(Object->GetClass()->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName)));
			if (!ObjProp
				|| ObjProp->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
			{
				return;
			}

			// Resolve only, never load: a load here can re-enter through PostLoad. Non-native classes are collaterals,
			// which the spawn element loads ahead of the apply.
			UClass* SubClass = PCGExTypeOps::MakePathChecked<FSoftClassPath>(ClassPathStr).ResolveClass();
			if (!SubClass)
			{
				ReportOnce(FString::Printf(
					TEXT("Class '%s' is not loaded: %s::%s keeps its default instead of the recorded subobject."),
					*ClassPathStr, *Object->GetClass()->GetName(), *PropName));
				return;
			}

			if (!IsRecreatableSubobjectClass(SubClass) || !SubClass->IsChildOf(ObjProp->PropertyClass))
			{
				return;
			}

			void* ValuePtr = ObjProp->ContainerPtrToValuePtr<void>(Object);
			UObject* Current = ObjProp->GetObjectPropertyValue(ValuePtr);
			UObject* Subobject = ProvideOwnedObject(Object, Current, SubClass);
			if (Subobject != Current)
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, Subobject);
			}

			DeserializeObjectDelta(Subobject, SubBytes);
		}

		// ---- Out-of-line archive operator<< definitions ----

		FArchive& FDeltaWriter::operator<<(UObject*& Res)
		{
			if (AsNestedInstancedRef(GetSerializedProperty()))
			{
				uint8 Present = (Res != nullptr) ? 1u : 0u;
				*this << Present;
				if (!Present)
				{
					return *this;
				}

				check(Res)

				FString ClassPath = Res->GetClass()->GetPathName();
				*this << ClassPath;
				AddClassCollateral(Res->GetClass(), CollateralSink);

				// VisitedSources is always set by callers (top-level Writer + every
				// nested ValueWriter / SubWriter wires it through). Cycle guard would be
				// undefined without it.
				check(VisitedSources);

				TArray<uint8> SubBytes;
				{
					FDeltaWriter SubWriter(SubBytes);
					SubWriter.SetCustomVersions(GetCustomVersions());
					SubWriter.VisitedSources = VisitedSources;
					SubWriter.CollateralSink = CollateralSink;

					// Diff against the subobject's class CDO -- at this depth we don't
					// have a per-instance archetype handle. Target and source share the
					// class, so applying the delta onto target (which archetype-cloning
					// already seeded with the archetype baseline) reproduces source's
					// state for fields source customized past the CDO, and leaves
					// archetype-level customizations intact where source didn't touch them.
					UObject* CDO = Res->GetClass()->GetDefaultObject();
					// Nested-Instanced subobjects are never the root component, so transform
					// filtering never applies in this branch.
					SerializeObjectDelta(Res, CDO, SubBytes, *VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);
				}

				uint32 SubSize = SubBytes.Num();
				*this << SubSize;
				if (SubSize > 0)
				{
					Serialize(SubBytes.GetData(), SubSize);
				}
				return *this;
			}

			// Default fall-through: serialize as soft-object path. The base
			// FObjectWriter would write the raw 8-byte pointer value, which is
			// meaningless once the bytes are persisted. Soft path strings survive
			// across sessions.
			FString PathStr = Res ? FSoftObjectPath(Res).ToString() : FString();
			*this << PathStr;

			// IsAsset filter: intra-level refs (component AttachParent, cross-actor
			// pointers) serialize through here too, with sub-object paths the streamable
			// manager treats as load failures -- one bad path fails the whole batch.
			if (CollateralSink && Res && Res->IsAsset() && !PathStr.IsEmpty())
			{
				CollateralSink->Add(FSoftObjectPath(PathStr));
			}
			return *this;
		}

		FArchive& FDeltaReader::operator<<(UObject*& Res)
		{
			const FProperty* CurProp = GetSerializedProperty();

			if (const FObjectPropertyBase* NestedProp = AsNestedInstancedRef(CurProp))
			{
				uint8 Present = 0;
				*this << Present;
				if (!Present)
				{
					return *this;
				}

				FString ClassPath;
				*this << ClassPath;

				uint32 SubSize = 0;
				*this << SubSize;

				TArray<uint8> SubBytes;
				if (SubSize > 0)
				{
					SubBytes.SetNumUninitialized(SubSize);
					Serialize(SubBytes.GetData(), SubSize);
				}

				// Resolve only, never load (see ApplyCreatedRecord).
				UClass* SubClass = PCGExTypeOps::MakePathChecked<FSoftClassPath>(ClassPath).ResolveClass();
				if (!SubClass || !Owner)
				{
					if (!SubClass)
					{
						ReportOnce(FString::Printf(TEXT("Class '%s' is not loaded: an instanced object nested in %s keeps its default."),
						                           *ClassPath, *NestedProp->GetFullName()));
					}
					return *this;
				}

				// Apply onto the target's own subobject of that class, seeded by archetype cloning. Anything else -- null,
				// another class, or a pointer a defaults copy left to the CDO's subobject -- is replaced by a new object
				// under Owner, never written through.
				if (!Res || Res->GetOuter() != Owner || Res->GetClass() != SubClass)
				{
					if (!IsRecreatableSubobjectClass(SubClass) || (NestedProp->PropertyClass && !SubClass->IsChildOf(NestedProp->PropertyClass)))
					{
						return *this;
					}
					Res = ProvideOwnedObject(Owner, Res, SubClass);
				}

				if (SubSize > 0)
				{
					DeserializeObjectDelta(Res, SubBytes);
				}
				return *this;
			}

			// Default fall-through: read soft-object path written by FDeltaWriter.
			FString PathStr;
			*this << PathStr;

			if (PathStr.IsEmpty())
			{
				// Source had nullptr -- match source intent.
				Res = nullptr;
				return *this;
			}

			const FSoftObjectPath Path = PCGExTypeOps::MakePathChecked(PathStr);

			// A legacy container element names the source actor's own subobject. Never point the target at it: the slot
			// keeps what it holds, as when the source is not loaded, so the result does not depend on what is.
			if (bLegacyInstancedContainer && Path.IsSubobject())
			{
				return *this;
			}

			// ResolveObject only finds already-loaded objects -- no sync load. Apply
			// runs inside SerializeItem callbacks; a TryLoad here could re-enter PCG
			// or other systems via PostLoad on the loaded asset. The expected case is
			// that the source actor was loaded, which already pulled its references
			// into memory.
			UObject* Loaded = Path.ResolveObject();
			if (!Loaded)
			{
				// Path didn't resolve (transient runtime object on the source side, or
				// asset not loaded in this context). Leave Res at the archetype-cloned
				// baseline rather than nulling out a slot the component may require
				// non-null (re-registration would then crash on a stale invariant).
				return *this;
			}

			// Type-guard: the property class is the assignment-compatible base. A
			// resolved object of an incompatible type would corrupt subsequent reads
			// of the slot.
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(CurProp))
			{
				if (ObjProp->PropertyClass && !Loaded->IsA(ObjProp->PropertyClass))
				{
					return *this;
				}
			}

			Res = Loaded;
			return *this;
		}
	}

#pragma endregion

#pragma region FDeltaExtensionHandle

	FDeltaExtensionHandle::~FDeltaExtensionHandle()
	{
		ExtensionRegistry::Unregister(Id);
	}

	FDeltaExtensionHandle::FDeltaExtensionHandle(FDeltaExtensionHandle&& Other) noexcept
		: Id(Other.Id)
	{
		Other.Id = 0;
	}

	FDeltaExtensionHandle& FDeltaExtensionHandle::operator=(FDeltaExtensionHandle&& Other) noexcept
	{
		if (this != &Other)
		{
			ExtensionRegistry::Unregister(Id);
			Id = Other.Id;
			Other.Id = 0;
		}
		return *this;
	}

	void FDeltaExtensionHandle::Reset()
	{
		ExtensionRegistry::Unregister(Id);
		Id = 0;
	}

	/** Handle factory. Defined in the cpp (not exported) and befriended by the handle. */
	class FDeltaExtensionHandleFactory
	{
	public:
		static FDeltaExtensionHandle Make(const uint64 Id)
		{
			FDeltaExtensionHandle Handle;
			Handle.Id = Id;
			return Handle;
		}
	};

	FDeltaExtensionHandle RegisterDeltaExtension(UClass* Class, FDeltaExtension Extension)
	{
		return FDeltaExtensionHandleFactory::Make(ExtensionRegistry::Register(Class, FSoftClassPath(), MoveTemp(Extension)));
	}

	FDeltaExtensionHandle RegisterDeltaExtension(const FSoftClassPath& ClassPath, FDeltaExtension Extension)
	{
		return FDeltaExtensionHandleFactory::Make(ExtensionRegistry::Register(nullptr, ClassPath, MoveTemp(Extension)));
	}

	FPostApplyFixupHandle RegisterPostApplyFixup(UClass* ComponentClass, FPostApplyFixup Fixup)
	{
		FDeltaExtension Extension;
		Extension.PostApply = MoveTemp(Fixup);
		return RegisterDeltaExtension(ComponentClass, MoveTemp(Extension));
	}

	FPostApplyFixupHandle RegisterPostApplyFixup(const FSoftClassPath& ClassPath, FPostApplyFixup Fixup)
	{
		FDeltaExtension Extension;
		Extension.PostApply = MoveTemp(Fixup);
		return RegisterDeltaExtension(ClassPath, MoveTemp(Extension));
	}

#pragma endregion

#pragma region SerializeActorDelta

	namespace Internal
	{
		struct FExtensionPayload
		{
			FString Id;
			uint32 Version = 0;
			FString Target; // Empty for the actor, else the component's name.
			TArray<uint8> Bytes;
		};

		/** Runs the Capture of every extension matching Source's class, keeping the payloads it accepted. */
		void CaptureExtensions(const UObject& Source, const FString& Target, TSet<FSoftObjectPath>* CollateralSink, TArray<FExtensionPayload>& OutPayloads)
		{
			TArray<uint64, TInlineAllocator<8>> Ids;
			ExtensionRegistry::GatherMatching(Source.GetClass(), Ids);

			for (const uint64 Id : Ids)
			{
				const FDeltaExtension* Extension = ExtensionRegistry::Find(Id);
				if (!Extension || !Extension->Capture)
				{
					continue;
				}

				FExtensionPayload Payload;
				TSet<const UObject*> Visited;
				FDeltaWriter Writer(Payload.Bytes);
				Writer.VisitedSources = &Visited;
				Writer.CollateralSink = CollateralSink;
				if (!Extension->Capture(Source, Writer) || Writer.IsError())
				{
					continue;
				}

				Payload.Id = Extension->Id.ToString();
				Payload.Version = Extension->Version;
				Payload.Target = Target;
				OutPayloads.Add(MoveTemp(Payload));
			}
		}

		/** [uint32 Magic][uint32 SectionVersion][uint32 SectionSize], then SectionSize bytes:
		 *  [uint32 Count], repeated Count times [FString Id][uint32 Version][FString Target][uint32 Size][Size bytes].
		 *  SectionSize lets a reader skip a section version it does not know. */
		void WriteExtensionSection(FArchive& Writer, TArray<FExtensionPayload>& Payloads)
		{
			TArray<uint8> Body;
			FMemoryWriter BodyWriter(Body);

			uint32 Count = Payloads.Num();
			BodyWriter << Count;
			for (FExtensionPayload& Payload : Payloads)
			{
				BodyWriter << Payload.Id;
				BodyWriter << Payload.Version;
				BodyWriter << Payload.Target;
				uint32 Size = Payload.Bytes.Num();
				BodyWriter << Size;
				if (Size > 0)
				{
					BodyWriter.Serialize(Payload.Bytes.GetData(), Size);
				}
			}

			uint32 Magic = ExtensionSectionMagic;
			uint32 SectionVersion = ExtensionSectionVersion;
			uint32 SectionSize = Body.Num();
			Writer << Magic;
			Writer << SectionVersion;
			Writer << SectionSize;
			Writer.Serialize(Body.GetData(), SectionSize);
		}
	}

	TArray<uint8> SerializeActorDelta(AActor* Actor, TArray<FSoftObjectPath>* OutCollaterals)
	{
		if (!Actor)
		{
			return {};
		}

		ExtensionRegistry::TryResolvePending();

		// nullptr sink short-circuits the emit site, keeping the no-collateral path cost-free.
		TSet<FSoftObjectPath> CollateralSet;
		TSet<FSoftObjectPath>* CollateralSinkPtr = OutCollaterals ? &CollateralSet : nullptr;

		UClass* ActorClass = Actor->GetClass();
		UObject* ActorCDO = ActorClass->GetDefaultObject();

		const USceneComponent* RootComp = Actor->GetRootComponent();

		TArray<uint8> ActorBytes;
		Internal::SerializeObjectDelta(Actor, ActorCDO, ActorBytes, /*bIsSubobject=*/false, /*bSkipTransformProps=*/false, CollateralSinkPtr);

		// Collect component deltas
		struct FComponentDelta
		{
			FName Name;
			FString ClassPath;
			TArray<uint8> Bytes;
		};
		TArray<FComponentDelta> ComponentDeltas;

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			UObject* Archetype = Component->GetArchetype();
			if (!Archetype || Archetype == Component)
			{
				continue;
			}

			// Class mismatch = archetype from a different version/refactor; skip safely
			if (Component->GetClass() != Archetype->GetClass())
			{
				continue;
			}

			// Components from CreateDefaultSubobject or Blueprint SCS have a per-actor
			// archetype on the actor CDO. Runtime-NewObject'd components (e.g. a
			// UVoxelSplineComponent attached to AVoxelStampActor at runtime) instead have
			// the raw class CDO as archetype -- no per-actor baseline. We diff against
			// the class CDO anyway so dynamic state (spline points, metadata) survives,
			// and record the class path so the apply side can recreate the component if
			// the spawned target doesn't include it.
			const bool bIsRoot = (Component == RootComp);
			TArray<uint8> CompBytes;
			Internal::SerializeObjectDelta(Component, Archetype, CompBytes, /*bIsSubobject=*/false, /*bSkipTransformProps=*/bIsRoot, CollateralSinkPtr);

			if (!CompBytes.IsEmpty())
			{
				FComponentDelta CD;
				CD.Name = Component->GetFName();
				CD.ClassPath = FSoftClassPath(Component->GetClass()).ToString();
				CD.Bytes = MoveTemp(CompBytes);
				ComponentDeltas.Add(MoveTemp(CD));
			}
		}

		TArray<Internal::FExtensionPayload> ExtensionPayloads;
		Internal::CaptureExtensions(*Actor, FString(), CollateralSinkPtr, ExtensionPayloads);
		for (UActorComponent* Component : Components)
		{
			Internal::CaptureExtensions(*Component, Component->GetName(), CollateralSinkPtr, ExtensionPayloads);
		}

		if (ActorBytes.IsEmpty() && ComponentDeltas.IsEmpty() && ExtensionPayloads.IsEmpty())
		{
			return {};
		}

		// Drain before the wire-format pack so callers don't get a stale partial list when
		// capture happens to be empty above.
		if (OutCollaterals)
		{
			OutCollaterals->Append(CollateralSet.Array());
		}

		// Outer wire format:
		//   [uint32 Magic][uint32 Version]
		//   [uint32 ActorDeltaSize][ActorDelta...]
		//   [uint32 ComponentCount]
		//   For each: [FString Name][FString ClassPath][uint32 CompDeltaSize][CompDelta...]
		//   Optional extension section (Internal::WriteExtensionSection), only when a payload was captured.
		TArray<uint8> Result;
		FMemoryWriter Writer(Result);

		uint32 Magic = DeltaWireMagic;
		uint32 Version = DeltaWireVersion;
		Writer << Magic;
		Writer << Version;

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
			// Names and class paths serialized as strings for session-portability. The
			// default FArchive FName path uses session-local name-table indices.
			FString CompNameStr = CD.Name.ToString();
			Writer << CompNameStr;
			Writer << CD.ClassPath;
			uint32 CompSize = CD.Bytes.Num();
			Writer << CompSize;
			Writer.Serialize(CD.Bytes.GetData(), CompSize);
		}

		if (!ExtensionPayloads.IsEmpty())
		{
			Internal::WriteExtensionSection(Writer, ExtensionPayloads);
		}

		return Result;
	}

#pragma endregion

#pragma region FScopedActorWrite

	FScopedActorWrite::FScopedActorWrite(AActor* InActor)
		: Actor(InActor)
	{
		check(Actor && IsInGameThread());
	}

	FScopedActorWrite::~FScopedActorWrite()
	{
		// In touch order, as FMultiComponentReregisterContext does.
		for (TUniquePtr<FComponentReregisterContext>& Context : Contexts)
		{
			Context.Reset();
		}

		for (UActorComponent* Component : NewComponents)
		{
			Component->RegisterComponent();
		}

		if (bRefreshActor)
		{
			// What AActor's setters do past the member they set: components read visibility and collision back from it.
			Actor->MarkComponentsRenderStateDirty();

			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Component : Components)
			{
				Component->OnActorEnableCollisionChanged();
			}

			Actor->UpdateOverlaps();
		}

		if (bRunFixups)
		{
			ExtensionRegistry::RunPostApply(Actor);
		}
	}

	void FScopedActorWrite::Touch(UActorComponent* InComponent)
	{
		if (InComponent && InComponent->IsRegistered())
		{
			Contexts.Add(MakeUnique<FComponentReregisterContext>(InComponent));
		}
	}

#pragma endregion

#pragma region ApplyPropertyDelta

	namespace Internal
	{
		/** Dispatches one extension payload to its registration. Unknown, newer and orphaned payloads are skipped. */
		void ApplyExtensionPayload(FScopedActorWrite& InScope, const FString& IdStr, const uint32 PayloadVersion, const FString& TargetName, const TArray<uint8>& Bytes)
		{
			AActor* Actor = InScope.GetActor();

			UClass* RegisteredClass = nullptr;
			const FDeltaExtension* Extension = ExtensionRegistry::FindById(PCGExTypeOps::MakeNameChecked(IdStr), RegisteredClass);
			if (!Extension || !Extension->Apply)
			{
				ReportOnce(FString::Printf(TEXT("No delta extension '%s' is registered here; its payload is skipped."), *IdStr));
				return;
			}

			if (PayloadVersion > Extension->Version)
			{
				ReportOnce(FString::Printf(TEXT("Delta extension '%s' payload version %u is newer than the registered %u; it is skipped."),
				                           *IdStr, PayloadVersion, Extension->Version));
				return;
			}

			UObject* Target = TargetName.IsEmpty()
				                  ? static_cast<UObject*>(Actor)
				                  : FindObjectFast<UActorComponent>(Actor, PCGExTypeOps::MakeNameChecked(TargetName));
			if (!Target || !Target->IsA(RegisteredClass))
			{
				ReportOnce(FString::Printf(TEXT("Delta extension '%s': %s has no matching '%s'; its payload is skipped."),
				                           *IdStr, *Actor->GetClass()->GetName(), TargetName.IsEmpty() ? TEXT("actor") : *TargetName));
				return;
			}

			if (UActorComponent* Component = Cast<UActorComponent>(Target))
			{
				InScope.Touch(Component);
			}

			FDeltaReader PayloadReader(Bytes);
			PayloadReader.Owner = Target;
			Extension->Apply(*Target, PayloadReader, PayloadVersion, InScope);
		}

		/** Reads the extension section, when the stream has one, and applies its payloads in order. */
		void ApplyExtensionSection(FScopedActorWrite& InScope, FMemoryReader& Reader, const int64 TotalSize)
		{
			if (Reader.Tell() + static_cast<int64>(sizeof(uint32) * 3) > TotalSize)
			{
				return;
			}

			uint32 Magic = 0;
			uint32 SectionVersion = 0;
			uint32 SectionSize = 0;
			Reader << Magic;
			Reader << SectionVersion;
			Reader << SectionSize;
			if (Reader.IsError() || Magic != ExtensionSectionMagic || Reader.Tell() + static_cast<int64>(SectionSize) > TotalSize)
			{
				return;
			}

			if (SectionVersion != ExtensionSectionVersion)
			{
				ReportOnce(FString::Printf(TEXT("Skipping a delta extension section of unknown version %u (expected %u)."), SectionVersion, ExtensionSectionVersion));
				return;
			}

			TArray<uint8> Body;
			Body.SetNumUninitialized(SectionSize);
			Reader.Serialize(Body.GetData(), SectionSize);

			FMemoryReader BodyReader(Body);
			uint32 Count = 0;
			BodyReader << Count;

			for (uint32 i = 0; i < Count; ++i)
			{
				if (BodyReader.IsError() || BodyReader.Tell() >= static_cast<int64>(SectionSize))
				{
					return;
				}

				FString IdStr;
				uint32 PayloadVersion = 0;
				FString TargetName;
				uint32 Size = 0;
				BodyReader << IdStr;
				BodyReader << PayloadVersion;
				BodyReader << TargetName;
				BodyReader << Size;
				if (BodyReader.IsError() || BodyReader.Tell() + static_cast<int64>(Size) > static_cast<int64>(SectionSize))
				{
					return;
				}

				TArray<uint8> Bytes;
				Bytes.SetNumUninitialized(Size);
				if (Size > 0)
				{
					BodyReader.Serialize(Bytes.GetData(), Size);
				}

				ApplyExtensionPayload(InScope, IdStr, PayloadVersion, TargetName, Bytes);
			}
		}
	}

	/** Applies onto InScope's actor; written components re-register, and the fixups run, when the scope closes. */
	void ApplyDeltaBytes(FScopedActorWrite& InScope, const TArray<uint8>& DeltaBytes)
	{
		AActor* Actor = InScope.GetActor();
		if (DeltaBytes.IsEmpty())
		{
			return;
		}

		ExtensionRegistry::TryResolvePending();

		FMemoryReader Reader(DeltaBytes);
		const int64 TotalSize = DeltaBytes.Num();

		// Validate magic+version. Old-format deltas would misalign under the current reader
		// and corrupt the target (FName reads returning bogus indices, crashing the editor
		// on later save). Silently skip unknown-format deltas; the user must rebuild the
		// collection to regenerate deltas in the current format.
		if (TotalSize < static_cast<int64>(sizeof(uint32) * 2))
		{
			return;
		}

		uint32 Magic = 0;
		uint32 Version = 0;
		Reader << Magic;
		Reader << Version;
		if (Reader.IsError() || Magic != DeltaWireMagic)
		{
			return;
		}
		if (Version != DeltaWireVersion)
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("[PCGExActorDelta] Skipping delta with incompatible wire version %u (expected %u). Rebuild the source collection to regenerate deltas."),
			       Version, DeltaWireVersion);
			return;
		}

		// Actor-level delta
		if (Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize)
		{
			return;
		}

		uint32 ActorSize = 0;
		Reader << ActorSize;
		if (ActorSize > 0)
		{
			if (Reader.Tell() + static_cast<int64>(ActorSize) > TotalSize)
			{
				return;
			}

			TArray<uint8> ActorBytes;
			ActorBytes.SetNumUninitialized(ActorSize);
			Reader.Serialize(ActorBytes.GetData(), ActorSize);
			Internal::DeserializeObjectDelta(Actor, ActorBytes);
		}

		if (Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize)
		{
			return;
		}

		uint32 CompCount = 0;
		Reader << CompCount;

		for (uint32 i = 0; i < CompCount; ++i)
		{
			if (Reader.Tell() >= TotalSize)
			{
				return;
			}

			FString CompNameStr;
			Reader << CompNameStr;
			const FName CompName = PCGExTypeOps::MakeNameChecked(CompNameStr);

			FString CompClassPathStr;
			Reader << CompClassPathStr;

			if (Reader.Tell() + static_cast<int64>(sizeof(uint32)) > TotalSize)
			{
				return;
			}

			uint32 CompSize = 0;
			Reader << CompSize;

			if (CompSize == 0)
			{
				continue;
			}
			if (Reader.Tell() + static_cast<int64>(CompSize) > TotalSize)
			{
				return;
			}

			TArray<uint8> CompBytes;
			CompBytes.SetNumUninitialized(CompSize);
			Reader.Serialize(CompBytes.GetData(), CompSize);

			UActorComponent* Component = FindObjectFast<UActorComponent>(Actor, CompName);
			bool bWasJustCreated = false;

			// Dynamic-component path: if the source actor had a runtime-NewObject'd
			// component that the spawned target doesn't include, recreate it from the
			// captured class path so the delta bytes have somewhere to land. Inherit
			// transient flags from the actor so preview / runtime spawns don't
			// accidentally persist a subobject.
			if (!Component && !CompClassPathStr.IsEmpty())
			{
				const FSoftClassPath CompClassPath = PCGExTypeOps::MakePathChecked<FSoftClassPath>(CompClassPathStr);
				if (UClass* CompClass = CompClassPath.ResolveClass())
				{
					if (CompClass->IsChildOf(UActorComponent::StaticClass()))
					{
						EObjectFlags ObjectFlags = RF_Transactional;
						ObjectFlags |= Actor->GetFlags() & (RF_Transient | RF_NonPIEDuplicateTransient);

						Component = NewObject<UActorComponent>(Actor, CompClass, CompName, ObjectFlags);
						if (Component)
						{
							bWasJustCreated = true;
							Actor->AddInstanceComponent(Component);
							// Scene components need an attach parent before registration.
							// Non-scene components skip this step.
							if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
							{
								if (USceneComponent* Root = Actor->GetRootComponent())
								{
									SceneComp->SetupAttachment(Root);
								}
							}
						}
					}
				}
			}

			// Writing class-mismatched payload onto an existing component would partially
			// overwrite unrelated fields whose names happen to overlap. The writer-side
			// guard at SerializeActorDelta also short-circuits the same case, but
			// re-checking here defends against later writer drift.
			if (Component && !bWasJustCreated && !CompClassPathStr.IsEmpty())
			{
				if (UClass* SrcClass = PCGExTypeOps::MakePathChecked<FSoftClassPath>(CompClassPathStr).ResolveClass())
				{
					if (Component->GetClass() != SrcClass)
					{
						continue;
					}
				}
			}

			if (!Component)
			{
				continue;
			}

			if (bWasJustCreated)
			{
				// Brand-new component is unregistered. Apply the delta first so
				// RegisterComponent sees the populated state on its first init pass.
				Internal::DeserializeObjectDelta(Component, CompBytes);
				InScope.RegisterOnClose(Component);
			}
			else
			{
				// Writing to a registered component leaves its render proxy on stale data until it re-registers.
				InScope.Touch(Component);
				Internal::DeserializeObjectDelta(Component, CompBytes);
			}
		}

		// After the property deltas, so a dynamic component an extension targets exists by now.
		Internal::ApplyExtensionSection(InScope, Reader, TotalSize);

		// Repair engine-managed invariants that the per-property delta cannot express
		// (e.g. USplineComponent's Spline/SplineCurves aliasing in UE 5.7+).
		InScope.RequestFixups();
	}

	void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes)
	{
		if (!Actor || DeltaBytes.IsEmpty())
		{
			return;
		}

		FScopedActorWrite Scope(Actor);
		ApplyDeltaBytes(Scope, DeltaBytes);
	}

	void ApplyPropertyDelta(FScopedActorWrite& InScope, const TArray<uint8>& DeltaBytes)
	{
		ApplyDeltaBytes(InScope, DeltaBytes);
	}

	uint32 HashDelta(const TArray<uint8>& DeltaBytes)
	{
		if (DeltaBytes.IsEmpty())
		{
			return 0;
		}
		return FCrc::MemCrc32(DeltaBytes.GetData(), DeltaBytes.Num());
	}

#pragma endregion
}
