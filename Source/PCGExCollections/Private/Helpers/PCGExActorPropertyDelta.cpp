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
#include "UObject/PropertyOptional.h"
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
	//     (gated to non-class-member positions only). One its object owns is recorded by name
	//     and diffed against the archetype that name resolves to, so apply finds its counterpart.
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
	static constexpr uint32 CreatedSubobjectsTag = 0x4352544Eu;   // 'CRTN'

#pragma region ExtensionRegistry

	namespace ExtensionRegistry
	{
		/** Either Class (direct registration) or PendingPath (soft registration resolving lazily) names the target. */
		struct FEntry
		{
			FSoftClassPath PendingPath;
			TWeakObjectPtr<UClass> Class;
			FDeltaExtension Extension;
			bool bReportedStale = false;
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

		/** Extension callbacks currently running. Registering is refused inside one; unregistering waits for it to return. */
		int32& GetCallbackDepth()
		{
			static int32 Depth = 0;
			return Depth;
		}

		TArray<uint64>& GetDeferredRemovals()
		{
			static TArray<uint64> Instance;
			return Instance;
		}

		/** For an entry whose class was collected: its bucket key is a dangling address, so every bucket is swept. */
		void RemoveFromBuckets(const uint64 Id)
		{
			for (auto It = GetBuckets().CreateIterator(); It; ++It)
			{
				It.Value().Remove(Id);
				if (It.Value().IsEmpty())
				{
					It.RemoveCurrent();
				}
			}
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

		/** Resolves the pending soft registrations, and sends back to pending one whose class was collected (a Blueprint
		 *  reload) so it resolves again from its path. A pass over the registrations, cheap enough to run on every capture
		 *  and apply instead of listening for module loads. */
		void TryResolvePending()
		{
			TArray<uint64>& Pending = GetPendingIds();
			for (TPair<uint64, FEntry>& Pair : GetEntries())
			{
				if (!Pair.Value.PendingPath.IsNull() && Pair.Value.Class.IsStale())
				{
					Pair.Value.Class.Reset();
					RemoveFromBuckets(Pair.Key);
					Pending.AddUnique(Pair.Key);
				}
			}

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

			// The running callback may be the one being removed.
			if (GetCallbackDepth() > 0)
			{
				GetDeferredRemovals().AddUnique(Id);
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
				RemoveFromBuckets(Id);
			}

			GetPendingIds().Remove(Id);
		}

		/** Brackets a call into an extension: entries stay put while it runs, removals land when the outermost one returns. */
		struct FCallbackScope
		{
			FCallbackScope()
			{
				++GetCallbackDepth();
			}

			~FCallbackScope()
			{
				if (--GetCallbackDepth() == 0 && !GetDeferredRemovals().IsEmpty())
				{
					for (const uint64 Id : TArray<uint64>(MoveTemp(GetDeferredRemovals())))
					{
						Unregister(Id);
					}
				}
			}

			FCallbackScope(const FCallbackScope&) = delete;
			FCallbackScope& operator=(const FCallbackScope&) = delete;
		};

		uint64 Register(UClass* Class, const FSoftClassPath& Path, FDeltaExtension&& Extension)
		{
			if (!Class && Path.IsNull())
			{
				return 0;
			}

			// Adding an entry can move the ones a running callback is reading from.
			if (GetCallbackDepth() > 0)
			{
				UE_LOG(LogPCGEx, Error, TEXT("[PCGExActorDelta] Rejected delta extension '%s': registered from inside an extension callback."), *Extension.Id.ToString());
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

			TMap<uint64, FEntry>& Entries = GetEntries();
			for (UClass* Cls = Class; Cls; Cls = Cls->GetSuperClass())
			{
				const TArray<uint64>* Bucket = Buckets.Find(Cls);
				if (!Bucket)
				{
					continue;
				}

				for (const uint64 Id : *Bucket)
				{
					FEntry* E = Entries.Find(Id);
					if (!E)
					{
						continue;
					}

					// A bucket key is a raw UClass*: a collected class's address can be reused by a new one. The entry's
					// weak pointer is the liveness check.
					if (E->Class.Get() != Cls)
					{
						if (!E->bReportedStale)
						{
							E->bReportedStale = true;
							UE_LOG(LogPCGEx, Warning,
							       TEXT("[PCGExActorDelta] Skipping delta extension '%s': its target class was garbage-collected; the registration is now stale."),
							       *E->Extension.Id.ToString());
						}
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
					if (const FDeltaExtension* Extension = Find(Id); Extension && Extension->PostApply)
					{
						FCallbackScope CallbackScope;
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
		bool IsInstanceEditableProperty(const FProperty* InProperty)
		{
			return InProperty->HasAnyPropertyFlags(CPF_Edit)
				&& !InProperty->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		}

		/** Relative-transform props on the root component are driven by the PCG point, never
		 *  by the source actor. Including them in the delta would overwrite the spawn
		 *  transform with the source actor's (often near-origin) values. Non-root scene
		 *  components legitimately use these props to describe their attachment offset, so
		 *  the filter only fires when the caller flags the object as "this is the root". */
		bool IsTransformProperty(const FProperty* InProperty)
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
		bool ShouldIncludeProperty(const FProperty* Property, bool bIsSubobject, bool bSkipTransformProps)
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

		/** First byte of a nested instanced object's record. */
		enum class ENestedRecord : uint8
		{
			Null = 0,
			ClassDefault = 1, // Diffed against the class CDO: objects the property's owner does not own, and older payloads.
			Named = 2,        // Owned by the property's owner: its name follows the class, its archetype is the baseline.
		};

		/** Shared by every reader of one apply. */
		struct FApplyContext
		{
			/** Named records already provided, by owner and name: slots that shared a source object share its counterpart. */
			TMap<TPair<const UObject*, FName>, UObject*> Provided;

			/** Objects this apply created. One its owner ends up not referencing is retired with those the delta dropped. */
			TArray<UObject*> Created;
		};

		// Forward declarations: the archives' operator<<(UObject*&) recurse into whole object deltas.
		void SerializeObjectDelta(
			UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes,
			TSet<const UObject*>& VisitedSources, bool bIsSubobject, bool bSkipTransformProps,
			TSet<FSoftObjectPath>* CollateralSink);

		void DeserializeObjectDelta(UObject* Object, UObject* Baseline, const TArray<uint8>& InBytes, FApplyContext& Context);

		/** A nested-Instanced ref position: an Instanced FObjectProperty owned by a
		 *  struct (or container element), reached via SerializeItem recursing past the
		 *  top-level property layer. The operator<<(UObject*&) override inlines a
		 *  recursive object-delta in this case; everything else falls through to the
		 *  soft-path write/read. Returns that property, or null. */
		const FObjectPropertyBase* AsNestedInstancedRef(const FProperty* CurProp)
		{
			// Two containers leave themselves current over an element: FSetProperty loading into an empty set (its save and
			// slow load paths make the element current), and FOptionalProperty on every path.
			if (const FSetProperty* SetProp = CastField<FSetProperty>(CurProp))
			{
				CurProp = SetProp->GetElementProperty();
			}
			else if (const FOptionalProperty* OptionalProp = CastField<FOptionalProperty>(CurProp))
			{
				CurProp = OptionalProp->GetValueProperty();
			}

			const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(CurProp);
			return (P && P->HasAnyPropertyFlags(CPF_InstancedReference) && P->GetOwner<UClass>() == nullptr) ? P : nullptr;
		}

		bool IsInstancedObjectProperty(const FProperty* Property)
		{
			return Property && Property->HasAnyPropertyFlags(CPF_InstancedReference) && Property->IsA<FObjectPropertyBase>();
		}

		/** A container whose records go to the 'INST' section, written and read whole with the container current: one
		 *  holding instanced objects directly, as it makes them current only while it is current itself, and any set or
		 *  optional holding them at all. Arrays and maps of structs stay in the property section, where tagged
		 *  serialization makes each struct member current. */
		bool IsInstancedContainer(const FProperty* Property)
		{
			if (!Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
			{
				return false;
			}
			// A set matches elements by identity and an optional resets its value on load: neither reads a delta of instances.
			if (Property->IsA<FSetProperty>() || Property->IsA<FOptionalProperty>())
			{
				return true;
			}
			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
			{
				return IsInstancedObjectProperty(ArrayProp->Inner);
			}
			if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
			{
				return IsInstancedObjectProperty(MapProp->GetKeyProperty()) || IsInstancedObjectProperty(MapProp->GetValueProperty());
			}
			return false;
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

		/** The flags an object created under Owner takes from it, as the editor's actor factories do. */
		EObjectFlags GetInheritedFlags(const UObject* Owner)
		{
			return Owner->GetFlags() & (RF_Transactional | RF_Transient | RF_NonPIEDuplicateTransient);
		}

		/** For a record diffed against the class CDO: Current when Owner owns it as exactly Class, else a new Class object. */
		UObject* ProvideClassDefaultObject(FApplyContext& Context, UObject* Owner, UObject* Current, UClass* Class)
		{
			if (Current && Current->GetOuter() == Owner && Current->GetClass() == Class)
			{
				return Current;
			}

			UObject* Created = NewObject<UObject>(Owner, Class, NAME_None, GetInheritedFlags(Owner));
			Context.Created.Add(Created);
			return Created;
		}

		/**
		 * For a named record: the counterpart of the source object under Owner, and in OutBaseline the archetype its name
		 * resolves to, which the writer diffed against. Owner's object of that name and class is the counterpart when that
		 * archetype is a default subobject, or when the slot already holds it: an instance-made object's name can match by
		 * chance. Otherwise a new object is made from that archetype, keeping the name when it is free.
		 */
		UObject* ProvideNamedObject(FApplyContext& Context, UObject* Owner, UClass* Class, const FName Name, UObject* Current, UObject*& OutBaseline)
		{
			OutBaseline = Class->GetDefaultObject();
			if (Name.IsNone())
			{
				return ProvideClassDefaultObject(Context, Owner, Current, Class);
			}

			const TPair<const UObject*, FName> Key(Owner, Name);
			OutBaseline = UObject::GetArchetypeFromRequiredInfo(Class, Owner, Name, RF_NoFlags);
			if (UObject* const* Provided = Context.Provided.Find(Key))
			{
				return *Provided;
			}

			const bool bDefaultSubobject = OutBaseline != Class->GetDefaultObject();
			UObject* Existing = StaticFindObjectFast(nullptr, Owner, Name);
			UObject* Result = nullptr;
			if (IsValid(Existing) && Existing->GetClass() == Class && (bDefaultSubobject || Existing == Current))
			{
				Result = Existing;
			}
			else
			{
				Result = NewObject<UObject>(Owner, Class, Existing ? FName() : Name, GetInheritedFlags(Owner), bDefaultSubobject ? OutBaseline : nullptr);
				Context.Created.Add(Result);
			}

			Context.Provided.Add(Key, Result);
			return Result;
		}

		/** Adds the objects Owner owns that a property value holds where a delta can replace them: object slots, and the
		 *  instanced objects inside containers and structs. */
		void GatherOwnedReferences(const FProperty* Property, const void* Value, const UObject* Owner, TSet<UObject*>& Out)
		{
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
			{
				// Resolving a handle can load. An object Owner owns lives in memory, so its handle is resolved already.
				const TObjectPtr<UObject> Ptr = ObjProp->GetObjectPtrPropertyValue(Value);
				UObject* Object = Ptr.IsResolved() ? Ptr.Get() : nullptr;
				if (Object && Object->GetOuter() == Owner)
				{
					Out.Add(Object);
				}
				return;
			}

			if (!Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
			{
				return;
			}

			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProp, Value);
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					GatherOwnedReferences(ArrayProp->Inner, Helper.GetRawPtr(i), Owner, Out);
				}
			}
			else if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
			{
				FScriptSetHelper Helper(SetProp, Value);
				for (FScriptSetHelper::FIterator It(Helper); It; ++It)
				{
					GatherOwnedReferences(SetProp->GetElementProperty(), Helper.GetElementPtr(It), Owner, Out);
				}
			}
			else if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(MapProp, Value);
				for (FScriptMapHelper::FIterator It(Helper); It; ++It)
				{
					GatherOwnedReferences(MapProp->GetKeyProperty(), Helper.GetKeyPtr(It), Owner, Out);
					GatherOwnedReferences(MapProp->GetValueProperty(), Helper.GetValuePtr(It), Owner, Out);
				}
			}
			else if (const FOptionalProperty* OptionalProp = CastField<FOptionalProperty>(Property))
			{
				if (const void* SetValue = OptionalProp->GetValuePointerForReadIfSet(Value))
				{
					GatherOwnedReferences(OptionalProp->GetValueProperty(), SetValue, Owner, Out);
				}
			}
			else if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
			{
				for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
				{
					for (int32 i = 0; i < It->ArrayDim; ++i)
					{
						GatherOwnedReferences(*It, It->ContainerPtrToValuePtr<void>(Value, i), Owner, Out);
					}
				}
			}
		}

		void GatherOwnedReferences(const UObject* Object, TSet<UObject*>& Out)
		{
			for (const FProperty* Property = Object->GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				for (int32 i = 0; i < Property->ArrayDim; ++i)
				{
					GatherOwnedReferences(Property, Property->ContainerPtrToValuePtr<void>(Object, i), Object, Out);
				}
			}
		}

		/** Retires the candidates and this apply's creations under Object that Object no longer references. Actors and
		 *  components are never moved. */
		void RetireDropped(UObject* Object, TSet<UObject*>& Candidates, const FApplyContext& Context)
		{
			for (UObject* Created : Context.Created)
			{
				if (Created->GetOuter() == Object)
				{
					Candidates.Add(Created);
				}
			}

			if (Candidates.IsEmpty())
			{
				return;
			}

			TSet<UObject*> Referenced;
			GatherOwnedReferences(Object, Referenced);
			for (UObject* Candidate : Candidates)
			{
				if (!Referenced.Contains(Candidate) && IsRecreatableSubobjectClass(Candidate->GetClass()))
				{
					RetireOwnedObject(Object, Candidate);
				}
			}
		}

		void WriteSizedBytes(FArchive& Writer, TArray<uint8>& Bytes)
		{
			uint32 Size = Bytes.Num();
			Writer << Size;
			if (Size > 0)
			{
				Writer.Serialize(Bytes.GetData(), Size);
			}
		}

		/** Reads [uint32 Size][Size bytes]. False, with the error set, when the size runs past the stream. */
		bool ReadSizedBytes(FArchive& Reader, TArray<uint8>& OutBytes)
		{
			uint32 Size = 0;
			Reader << Size;
			if (Reader.IsError() || static_cast<int64>(Size) > Reader.TotalSize() - Reader.Tell())
			{
				Reader.SetError();
				return false;
			}

			OutBytes.SetNumUninitialized(static_cast<int32>(Size));
			if (Size > 0)
			{
				Reader.Serialize(OutBytes.GetData(), Size);
			}
			return !Reader.IsError();
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
		 *        [uint8  Kind]            ENestedRecord
		 *        Unless Null:
		 *          [FString ClassPath]
		 *          [FString Name]         Named only
		 *          [uint32  SubSize]
		 *          [SubSize bytes]        recursive object delta vs. the class CDO, or for Named the archetype its
		 *                                 name resolves to
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

			/** The object whose property is being written: a nested instanced object it owns is recorded by name. */
			const UObject* Owner = nullptr;

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
			/** Strings are capped at the stream's size, so a corrupt length fails the read instead of allocating. */
			explicit FDeltaReader(const TArray<uint8>& InBytes)
				: FObjectReader(InBytes)
			{
				ArMaxSerializeSize = FMath::Max<int64>(InBytes.Num(), 1);
			}

			/** The object being read into: nested-Instanced objects are applied onto, or created under, it. */
			UObject* Owner = nullptr;

			FApplyContext* Context = nullptr;

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
			UObject* Defaults; // The parallel default subobject, or the archetype of a created one.
			bool bCreated;     // Defaults hold no same-class subobject: recorded with its class and name, recreated on apply.
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
		 * is diffed against its archetype (the class CDO, unless its name is a default
		 * subobject's) and recorded with its class and name, so apply can recreate it.
		 * Transient instances and classes IsRecreatableSubobjectClass refuses are skipped.
		 *
		 * UActorComponent instances are explicitly skipped at the actor level: they're
		 * outered to the actor (both CDSO and SCS-created), so without this guard
		 * SerializeActorDelta would double-serialize each component -- once via its outer
		 * component loop, once by recursing into it as a subobject here.
		 */
		void CollectSubobjects(UObject* Object, UObject* Defaults, TConstArrayView<FName> Claims, TArray<FSubobjectBinding>& Out)
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

				Out.Add({ObjProp->GetName(), Inst, Inst->GetArchetype(), true});
			}
		}

		/** [uint32 Count], then [FString PropName][uint32 ValueSize][ValueSize bytes] per property. Records of the 'INST'
		 *  section hold the whole value, serialized with the property current (see IsInstancedContainer). */
		void WritePropertyRecords(
			FArchive& Writer, UObject* Object, UObject* Defaults, TConstArrayView<FProperty*> Properties, const bool bInstancedSection,
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
					ValueWriter.Owner = Object;
					ValueWriter.VisitedSources = &VisitedSources;
					ValueWriter.CollateralSink = CollateralSink;
					FSerializedPropertyScope SerializedProperty(ValueWriter, bInstancedSection ? Property : nullptr);
					Property->SerializeItem(
						FStructuredArchiveFromArchive(ValueWriter).GetSlot(),
						Property->ContainerPtrToValuePtr<void>(Object),
						bInstancedSection ? nullptr : Property->ContainerPtrToValuePtr<void>(Defaults));
				}

				WriteSizedBytes(Writer, ValueBytes);
			}
		}

		void WriteSection(FArchive& Writer, uint32 Tag, TArray<uint8>& SectionBytes)
		{
			Writer << Tag;
			WriteSizedBytes(Writer, SectionBytes);
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
		 *      'INST': [uint32 PropCount] + whole-value property records, for IsInstancedContainer properties
		 *      'CRTN': [uint32 CreatedCount], repeated CreatedCount times:
		 *        [FString PropName]
		 *        [FString ClassPath]   // FSoftClassPath of the instance-owned subobject
		 *        [FString Name]        // its name, which resolves its archetype and counterpart on apply
		 *        [uint32 SubSize]      // may be 0: the record's existence recreates the object
		 *        [SubSize bytes]       // recursive SerializeObjectDelta output vs. its archetype
		 *
		 *  Each segment is length-prefixed so unknown/renamed props, subobjects or sections on
		 *  apply seek past their payload without corrupting subsequent reads. */
		void SerializeObjectDelta(
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
				TArray<uint8> Bytes;
			};
			struct FCreatedDelta
			{
				FString PropName;
				FString ClassPath;
				FString Name;
				TArray<uint8> Bytes;
			};
			TArray<FSubDelta> SubDeltas;
			TArray<FCreatedDelta> CreatedDeltas;
			for (const FSubobjectBinding& Sub : Subobjects)
			{
				// A subobject already recorded through another slot yields no bytes: a created one is still recorded, so
				// apply points this slot at the same object.
				TArray<uint8> SubBytes;
				// Subobjects are by definition not the root component, so transform filtering
				// never applies in this branch. Forward the collateral sink so soft-path
				// references inside nested subobject state still get collected.
				SerializeObjectDelta(Sub.Instance, Sub.Defaults, SubBytes, VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);

				if (!Sub.bCreated)
				{
					if (!SubBytes.IsEmpty())
					{
						SubDeltas.Add({Sub.PropName, MoveTemp(SubBytes)});
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
				CreatedDeltas.Add({Sub.PropName, FSoftClassPath(SubClass).ToString(), Sub.Instance->GetName(), MoveTemp(SubBytes)});
			}

			if (PropsToWrite.IsEmpty() && ContainersToWrite.IsEmpty() && SubDeltas.IsEmpty() && CreatedDeltas.IsEmpty())
			{
				return;
			}

			FDeltaWriter Writer(OutBytes);
			Writer.VisitedSources = &VisitedSources;
			Writer.CollateralSink = CollateralSink;

			WritePropertyRecords(Writer, Object, Defaults, PropsToWrite, /*bInstancedSection=*/false, VisitedSources, CollateralSink);

			uint32 SubCount = SubDeltas.Num();
			Writer << SubCount;
			for (FSubDelta& SD : SubDeltas)
			{
				Writer << SD.PropName;
				WriteSizedBytes(Writer, SD.Bytes);
			}

			// The sections are omitted when empty, so other objects keep their bytes and hash.
			if (!ContainersToWrite.IsEmpty())
			{
				TArray<uint8> SectionBytes;
				FMemoryWriter SectionWriter(SectionBytes);
				WritePropertyRecords(SectionWriter, Object, Defaults, ContainersToWrite, /*bInstancedSection=*/true, VisitedSources, CollateralSink);
				WriteSection(Writer, InstancedContainersTag, SectionBytes);
			}

			if (!CreatedDeltas.IsEmpty())
			{
				TArray<uint8> SectionBytes;
				FMemoryWriter SectionWriter(SectionBytes);

				uint32 CreatedCount = CreatedDeltas.Num();
				SectionWriter << CreatedCount;
				for (FCreatedDelta& CD : CreatedDeltas)
				{
					SectionWriter << CD.PropName;
					SectionWriter << CD.ClassPath;
					SectionWriter << CD.Name;
					WriteSizedBytes(SectionWriter, CD.Bytes);
				}

				WriteSection(Writer, CreatedSubobjectsTag, SectionBytes);
			}
		}

		/** Public wrapper: allocate a fresh visited set and defer to the recursive impl. */
		void SerializeObjectDelta(UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes, bool bIsSubobject = false, bool bSkipTransformProps = false, TSet<FSoftObjectPath>* CollateralSink = nullptr)
		{
			TSet<const UObject*> Visited;
			SerializeObjectDelta(Object, Defaults, OutBytes, Visited, bIsSubobject, bSkipTransformProps, CollateralSink);
		}

		/** Reads records written by WritePropertyRecords onto Object, those of the property section against Baseline (the
		 *  defaults the writer diffed against). Unknown properties are skipped, as is a record no longer in its property's
		 *  section; a property-section record of an instanced container comes from an older capture, which wrote its
		 *  instances as paths to the source actor's own objects or as a delta the container cannot read, so it is skipped
		 *  whole. */
		void ReadPropertyRecords(UObject* Object, UObject* Baseline, FArchive& Reader, const bool bInstancedSection, FApplyContext& Context)
		{
			UClass* Class = Object->GetClass();

			uint32 Count = 0;
			Reader << Count;

			for (uint32 i = 0; i < Count && !Reader.IsError() && !Reader.AtEnd(); ++i)
			{
				FString PropName;
				Reader << PropName;

				// Its own buffer: an over/under-read inside the property handler cannot desync the stream.
				TArray<uint8> ValueBytes;
				if (!ReadSizedBytes(Reader, ValueBytes))
				{
					break;
				}

				// Root-component transform fields are filtered at capture, not here: other components keep theirs. An empty
				// value carries no data, and SerializeItem on it could reset the destination.
				FProperty* Property = Class->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName));
				if (!Property
					|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated)
					|| ValueBytes.IsEmpty())
				{
					continue;
				}

				if (IsInstancedContainer(Property) != bInstancedSection)
				{
					if (!bInstancedSection)
					{
						ReportOnce(FString::Printf(
							TEXT("%s::%s holds a record from an older capture that this version cannot apply faithfully; it is skipped. Rebuild the collection to capture it."),
							*Class->GetName(), *PropName));
					}
					continue;
				}

				FDeltaReader ValueReader(ValueBytes);
				ValueReader.SetCustomVersions(Reader.GetCustomVersions());
				ValueReader.Owner = Object;
				ValueReader.Context = &Context;
				// Keeps a container's instanced elements in place for the nested records to land on, instead of emptying it first.
				ValueReader.ArPreserveArrayElements = Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference);
				FSerializedPropertyScope SerializedProperty(ValueReader, bInstancedSection ? Property : nullptr);
				Property->SerializeItem(
					FStructuredArchiveFromArchive(ValueReader).GetSlot(),
					Property->ContainerPtrToValuePtr<void>(Object),
					bInstancedSection ? nullptr : Property->ContainerPtrToValuePtr<void>(Baseline));
			}
		}

		/** Points one slot at the counterpart of the instance-owned subobject a created record describes, then applies its
		 *  delta onto it. */
		void ApplyCreatedRecord(UObject* Object, const FString& PropName, const FString& ClassPathStr, const FString& NameStr, const TArray<uint8>& SubBytes, FApplyContext& Context)
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
			UObject* Baseline = nullptr;
			UObject* Subobject = ProvideNamedObject(Context, Object, SubClass, PCGExTypeOps::MakeNameChecked(NameStr), Current, Baseline);
			if (Subobject != Current)
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, Subobject);
			}

			DeserializeObjectDelta(Subobject, Baseline, SubBytes, Context);
		}

		/** Reads the 'CRTN' section's records onto Object. */
		void ReadCreatedRecords(UObject* Object, FArchive& Reader, FApplyContext& Context)
		{
			uint32 CreatedCount = 0;
			Reader << CreatedCount;

			for (uint32 i = 0; i < CreatedCount && !Reader.IsError() && !Reader.AtEnd(); ++i)
			{
				FString PropName;
				FString ClassPathStr;
				FString NameStr;
				Reader << PropName;
				Reader << ClassPathStr;
				Reader << NameStr;

				TArray<uint8> SubBytes;
				if (!ReadSizedBytes(Reader, SubBytes))
				{
					break;
				}

				ApplyCreatedRecord(Object, PropName, ClassPathStr, NameStr, SubBytes, Context);
			}
		}

		/** Reads Object's delta against Baseline, the defaults the writer diffed it against: properties, subobjects, then
		 *  the optional sections. Unknown properties, subobjects and sections are skipped. Objects of Object's that the delta
		 *  leaves unreferenced are retired. */
		void DeserializeObjectDelta(UObject* Object, UObject* Baseline, const TArray<uint8>& InBytes, FApplyContext& Context)
		{
			if (!Object || InBytes.IsEmpty())
			{
				return;
			}
			check(Baseline);

			TSet<UObject*> OwnedBefore;
			GatherOwnedReferences(Object, OwnedBefore);

			UClass* Class = Object->GetClass();
			FDeltaReader Reader(InBytes);
			Reader.Owner = Object;
			Reader.Context = &Context;

			ReadPropertyRecords(Object, Baseline, Reader, /*bInstancedSection=*/false, Context);

			// Trailing subobject section. Truncated or absent is treated as empty -- the
			// outer wire-version gate has already rejected incompatible formats, so here a
			// missing subobject count means the writer produced zero subobjects.
			uint32 SubCount = 0;
			if (!Reader.IsError() && !Reader.AtEnd())
			{
				Reader << SubCount;
			}

			for (uint32 i = 0; i < SubCount && !Reader.IsError() && !Reader.AtEnd(); ++i)
			{
				FString PropName;
				Reader << PropName;

				TArray<uint8> SubBytes;
				if (!ReadSizedBytes(Reader, SubBytes))
				{
					break;
				}

				FObjectProperty* ObjProp = CastField<FObjectProperty>(Class->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName)));
				if (SubBytes.IsEmpty()
					|| !ObjProp
					|| ObjProp->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
				{
					continue;
				}

				// Symmetric outer-ownership guard: refuse to write into a foreign object.
				// The target's own archetype-spawned subobject is what the source delta was
				// captured against; anything else could pollute external state.
				UObject* Subobj = ObjProp->GetObjectPropertyValue_InContainer(Object);
				if (!Subobj || Subobj->GetOuter() != Object)
				{
					continue;
				}

				// The writer diffed against the baseline's own subobject in that slot.
				UObject* SubBaseline = ObjProp->GetObjectPropertyValue_InContainer(Baseline);
				if (!SubBaseline || SubBaseline->GetClass() != Subobj->GetClass())
				{
					SubBaseline = Subobj->GetArchetype();
				}
				DeserializeObjectDelta(Subobj, SubBaseline, SubBytes, Context);
			}

			// Optional sections, absent from older payloads. A tag this reader does not know is skipped.
			while (!Reader.IsError() && Reader.Tell() + static_cast<int64>(sizeof(uint32) * 2) <= Reader.TotalSize())
			{
				uint32 Tag = 0;
				Reader << Tag;

				TArray<uint8> SectionBytes;
				if (!ReadSizedBytes(Reader, SectionBytes))
				{
					break;
				}

				FDeltaReader SectionReader(SectionBytes);
				SectionReader.SetCustomVersions(Reader.GetCustomVersions());
				switch (Tag)
				{
				case InstancedContainersTag:
					ReadPropertyRecords(Object, Baseline, SectionReader, /*bInstancedSection=*/true, Context);
					break;
				case CreatedSubobjectsTag:
					ReadCreatedRecords(Object, SectionReader, Context);
					break;
				default:
					break;
				}
			}

			RetireDropped(Object, OwnedBefore, Context);
		}

		// ---- Out-of-line archive operator<< definitions ----

		FArchive& FDeltaWriter::operator<<(UObject*& Res)
		{
			if (AsNestedInstancedRef(GetSerializedProperty()))
			{
				// An object the property's owner owns is recorded by name and diffed against the archetype that name
				// resolves to (its default subobject's, or the class CDO), so apply diffs onto its counterpart. Any other
				// object is diffed against its class CDO.
				const bool bNamed = Res && Owner && Res->GetOuter() == Owner;
				uint8 Kind = static_cast<uint8>(!Res ? ENestedRecord::Null : (bNamed ? ENestedRecord::Named : ENestedRecord::ClassDefault));
				*this << Kind;
				if (!Res)
				{
					return *this;
				}

				UClass* Class = Res->GetClass();
				FString ClassPath = Class->GetPathName();
				*this << ClassPath;
				AddClassCollateral(Class, CollateralSink);

				UObject* Baseline = Class->GetDefaultObject();
				if (bNamed)
				{
					FString Name = Res->GetName();
					*this << Name;
					Baseline = Res->GetArchetype();
				}

				// Every writer of the capture tree shares the cycle guard.
				check(VisitedSources);

				// Nested-Instanced subobjects are never the root component, so transform filtering never applies here.
				TArray<uint8> SubBytes;
				SerializeObjectDelta(Res, Baseline, SubBytes, *VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);
				WriteSizedBytes(*this, SubBytes);
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
				uint8 Kind = 0;
				*this << Kind;
				if (Kind == static_cast<uint8>(ENestedRecord::Null))
				{
					return *this;
				}
				if (Kind != static_cast<uint8>(ENestedRecord::ClassDefault) && Kind != static_cast<uint8>(ENestedRecord::Named))
				{
					// A kind this reader does not know has a layout it cannot skip: the rest of the value is unreadable.
					SetError();
					return *this;
				}

				FString ClassPath;
				*this << ClassPath;

				FString NameStr;
				if (Kind == static_cast<uint8>(ENestedRecord::Named))
				{
					*this << NameStr;
				}

				TArray<uint8> SubBytes;
				if (!ReadSizedBytes(*this, SubBytes))
				{
					return *this;
				}

				// Resolve only, never load (see ApplyCreatedRecord).
				UClass* SubClass = PCGExTypeOps::MakePathChecked<FSoftClassPath>(ClassPath).ResolveClass();
				if (!SubClass)
				{
					ReportOnce(FString::Printf(TEXT("Class '%s' is not loaded: an instanced object nested in %s keeps its default."),
					                           *ClassPath, *NestedProp->GetFullName()));
					return *this;
				}

				// Actors and components have paths of their own. Anything this does provide is Owner's own object of the
				// record's class: a pointer a defaults copy left to the CDO's subobject is never written through.
				if (!Owner || !Context
					|| !IsRecreatableSubobjectClass(SubClass)
					|| (NestedProp->PropertyClass && !SubClass->IsChildOf(NestedProp->PropertyClass)))
				{
					return *this;
				}

				UObject* Baseline = SubClass->GetDefaultObject();
				Res = (Kind == static_cast<uint8>(ENestedRecord::Named))
					      ? ProvideNamedObject(*Context, Owner, SubClass, PCGExTypeOps::MakeNameChecked(NameStr), Res, Baseline)
					      : ProvideClassDefaultObject(*Context, Owner, Res, SubClass);
				DeserializeObjectDelta(Res, Baseline, SubBytes, *Context);
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
				Payload.Id = Extension->Id.ToString();
				Payload.Version = Extension->Version;
				Payload.Target = Target;

				TSet<const UObject*> Visited;
				FDeltaWriter Writer(Payload.Bytes);
				Writer.Owner = &Source;
				Writer.VisitedSources = &Visited;
				Writer.CollateralSink = CollateralSink;

				bool bCaptured = false;
				{
					ExtensionRegistry::FCallbackScope CallbackScope;
					bCaptured = Extension->Capture(Source, Writer);
				}

				if (bCaptured && !Writer.IsError())
				{
					OutPayloads.Add(MoveTemp(Payload));
				}
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
				WriteSizedBytes(BodyWriter, Payload.Bytes);
			}

			uint32 Magic = ExtensionSectionMagic;
			uint32 SectionVersion = ExtensionSectionVersion;
			Writer << Magic;
			Writer << SectionVersion;
			WriteSizedBytes(Writer, Body);
		}
	}

	TArray<uint8> SerializeActorDelta(AActor* Actor, TArray<FSoftObjectPath>* OutCollaterals)
	{
		if (!Actor)
		{
			return {};
		}

		// The extension registry is unsynchronized.
		check(IsInGameThread());
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

		Internal::WriteSizedBytes(Writer, ActorBytes);

		uint32 CompCount = ComponentDeltas.Num();
		Writer << CompCount;

		for (FComponentDelta& CD : ComponentDeltas)
		{
			// Names and class paths serialized as strings for session-portability. The
			// default FArchive FName path uses session-local name-table indices.
			FString CompNameStr = CD.Name.ToString();
			Writer << CompNameStr;
			Writer << CD.ClassPath;
			Internal::WriteSizedBytes(Writer, CD.Bytes);
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
		void ApplyExtensionPayload(FScopedActorWrite& InScope, const FString& IdStr, const uint32 PayloadVersion, const FString& TargetName, const TArray<uint8>& Bytes, FApplyContext& Context)
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
			PayloadReader.Context = &Context;

			ExtensionRegistry::FCallbackScope CallbackScope;
			Extension->Apply(*Target, PayloadReader, PayloadVersion, InScope);
		}

		/** Reads the extension section, when the stream has one, and applies its payloads in order. */
		void ApplyExtensionSection(FScopedActorWrite& InScope, FArchive& Reader, FApplyContext& Context)
		{
			if (Reader.Tell() + static_cast<int64>(sizeof(uint32) * 3) > Reader.TotalSize())
			{
				return;
			}

			uint32 Magic = 0;
			uint32 SectionVersion = 0;
			Reader << Magic;
			Reader << SectionVersion;

			TArray<uint8> Body;
			if (Magic != ExtensionSectionMagic || !ReadSizedBytes(Reader, Body))
			{
				return;
			}

			if (SectionVersion != ExtensionSectionVersion)
			{
				ReportOnce(FString::Printf(TEXT("Skipping a delta extension section of unknown version %u (expected %u)."), SectionVersion, ExtensionSectionVersion));
				return;
			}

			FDeltaReader BodyReader(Body);
			uint32 Count = 0;
			BodyReader << Count;

			for (uint32 i = 0; i < Count && !BodyReader.IsError() && !BodyReader.AtEnd(); ++i)
			{
				FString IdStr;
				uint32 PayloadVersion = 0;
				FString TargetName;
				BodyReader << IdStr;
				BodyReader << PayloadVersion;
				BodyReader << TargetName;

				TArray<uint8> Bytes;
				if (!ReadSizedBytes(BodyReader, Bytes))
				{
					return;
				}

				ApplyExtensionPayload(InScope, IdStr, PayloadVersion, TargetName, Bytes, Context);
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

		Internal::FDeltaReader Reader(DeltaBytes);

		// Validate magic+version. Old-format deltas would misalign under the current reader
		// and corrupt the target (FName reads returning bogus indices, crashing the editor
		// on later save). Silently skip unknown-format deltas; the user must rebuild the
		// collection to regenerate deltas in the current format.
		if (DeltaBytes.Num() < static_cast<int32>(sizeof(uint32) * 2))
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

		Internal::FApplyContext Context;

		TArray<uint8> ActorBytes;
		if (!Internal::ReadSizedBytes(Reader, ActorBytes))
		{
			return;
		}
		Internal::DeserializeObjectDelta(Actor, Actor->GetClass()->GetDefaultObject(), ActorBytes, Context);

		uint32 CompCount = 0;
		if (!Reader.AtEnd())
		{
			Reader << CompCount;
		}

		// A truncated stream stops the records, not the fixups: they repair what the earlier records wrote.
		for (uint32 i = 0; i < CompCount && !Reader.IsError() && !Reader.AtEnd(); ++i)
		{
			FString CompNameStr;
			Reader << CompNameStr;
			const FName CompName = PCGExTypeOps::MakeNameChecked(CompNameStr);

			FString CompClassPathStr;
			Reader << CompClassPathStr;

			TArray<uint8> CompBytes;
			if (!Internal::ReadSizedBytes(Reader, CompBytes))
			{
				break;
			}
			if (CompBytes.IsEmpty())
			{
				continue;
			}

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

			// The writer diffed each component against its archetype (the class CDO for a dynamic one).
			UObject* Baseline = Component->GetArchetype();
			if (bWasJustCreated)
			{
				// Brand-new component is unregistered. Apply the delta first so
				// RegisterComponent sees the populated state on its first init pass.
				Internal::DeserializeObjectDelta(Component, Baseline, CompBytes, Context);
				InScope.RegisterOnClose(Component);
			}
			else
			{
				// Writing to a registered component leaves its render proxy on stale data until it re-registers.
				InScope.Touch(Component);
				Internal::DeserializeObjectDelta(Component, Baseline, CompBytes, Context);
			}
		}

		// After the property deltas, so a dynamic component an extension targets exists by now.
		Internal::ApplyExtensionSection(InScope, Reader, Context);

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
