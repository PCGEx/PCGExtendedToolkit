// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyDelta.h"

#include "ComponentReregisterContext.h"
#include "PCGExLog.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/StructuredArchiveAdapters.h"
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
	//   - operator<<(UObject*&) also intercepts Instanced refs nested in struct/TArray/TMap
	//     (gated to struct-inner positions only).
	//   - Cycle guard via TSet<const UObject*> threaded through the recursion.
	static constexpr uint32 DeltaWireMagic = 0x50434745u; // 'PCGE'
	static constexpr uint32 DeltaWireVersion = 6u;

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

		/** A nested-Instanced ref position: an Instanced FObjectProperty owned by a
		 *  struct (or container element), reached via SerializeItem recursing past the
		 *  top-level property layer. The operator<<(UObject*&) override inlines a
		 *  recursive object-delta in this case; everything else falls through to the
		 *  soft-path write/read. */
		static bool IsInstancedNestedRef(const FProperty* CurProp)
		{
			const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(CurProp);
			return P
				&& P->HasAnyPropertyFlags(CPF_InstancedReference)
				&& P->GetOwner<UClass>() == nullptr;
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
		 *   1. Nested-Instanced refs (see IsInstancedNestedRef). Inlines a recursive
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

			virtual FArchive& operator<<(FName& Name) override
			{
				FString AsString;
				*this << AsString;
				Name = FName(*AsString);
				return *this;
			}

			virtual FArchive& operator<<(UObject*& Res) override;

			/** TObjectPtr<T>-backed properties serialize through this virtual in UE 5.6/5.7,
			 *  bypassing operator<<(UObject*&). Start Raw as nullptr (do NOT call
			 *  Value.Get()): on a freshly archetype-cloned target, calling Get() on every
			 *  TObjectPtr triggers lazy resolution / sync-loads and turns each spawn into
			 *  a multi-second pause. We accept the trade-off that source-was-null intent
			 *  isn't preserved (target stays at archetype baseline instead of being
			 *  nulled out) -- archetype baseline is almost always safer than nulling a
			 *  slot the component may require non-null. */
			virtual FArchive& operator<<(FObjectPtr& Value) override
			{
				UObject* Raw = nullptr;
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
			UObject* Defaults;
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
		 * We require Defaults to hold a parallel subobject of the same class so the
		 * recursive diff has a stable baseline. One-sided null and class mismatches are
		 * silently skipped -- there'd be no safe way to apply the delta.
		 *
		 * UActorComponent instances are explicitly skipped at the actor level: they're
		 * outered to the actor (both CDSO and SCS-created), so without this guard
		 * SerializeActorDelta would double-serialize each component -- once via its outer
		 * component loop, once by recursing into it as a subobject here.
		 */
		static void CollectSubobjects(UObject* Object, UObject* Defaults, TArray<FSubobjectBinding>& Out)
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

				UObject* Inst = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Object));
				UObject* Def = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Defaults));
				if (!Inst || !Def)
				{
					continue;
				}
				if (Inst->GetOuter() != Object)
				{
					continue;
				}
				if (Inst->GetClass() != Def->GetClass())
				{
					continue;
				}

				// Components are handled by SerializeActorDelta's component loop. Recursing
				// into them here at the actor level would double-serialize their state.
				if (Inst->IsA<UActorComponent>())
				{
					continue;
				}

				Out.Add({ObjProp->GetName(), Inst, Def});
			}
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
		 *
		 *  Each segment is length-prefixed so unknown/renamed props or subobjects on apply
		 *  seek past their payload without corrupting subsequent reads. */
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

			TArray<FProperty*> PropsToWrite;
			for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				if (!ShouldIncludeProperty(Property, bIsSubobject, bSkipTransformProps))
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
				PropsToWrite.Add(Property);
			}

			TArray<FSubobjectBinding> Subobjects;
			CollectSubobjects(Object, Defaults, Subobjects);

			struct FSubDelta
			{
				FString PropName;
				TArray<uint8> Bytes;
			};
			TArray<FSubDelta> SubDeltas;
			for (const FSubobjectBinding& Sub : Subobjects)
			{
				TArray<uint8> SubBytes;
				// Subobjects are by definition not the root component, so transform filtering
				// never applies in this branch. Forward the collateral sink so soft-path
				// references inside nested subobject state still get collected.
				SerializeObjectDelta(Sub.Instance, Sub.Defaults, SubBytes, VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);
				if (!SubBytes.IsEmpty())
				{
					SubDeltas.Add({Sub.PropName, MoveTemp(SubBytes)});
				}
			}

			if (PropsToWrite.IsEmpty() && SubDeltas.IsEmpty())
			{
				return;
			}

			FDeltaWriter Writer(OutBytes);
			Writer.VisitedSources = &VisitedSources;
			Writer.CollateralSink = CollateralSink;

			uint32 Count = PropsToWrite.Num();
			Writer << Count;

			for (FProperty* Property : PropsToWrite)
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
		}

		/** Public wrapper: allocate a fresh visited set and defer to the recursive impl. */
		static void SerializeObjectDelta(UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes, bool bIsSubobject = false, bool bSkipTransformProps = false, TSet<FSoftObjectPath>* CollateralSink = nullptr)
		{
			TSet<const UObject*> Visited;
			SerializeObjectDelta(Object, Defaults, OutBytes, Visited, bIsSubobject, bSkipTransformProps, CollateralSink);
		}

		/** Read per-property segments, then descend into the subobject section. Missing /
		 *  renamed properties and mismatched subobjects seek past their payload instead of
		 *  corrupting the outer stream. */
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
			const int64 TotalSize = InBytes.Num();

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
				FProperty* Property = Class->FindPropertyByName(FName(*PropName));
				if (!Property
					|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
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
				Property->SerializeItem(
					FStructuredArchiveFromArchive(ValueReader).GetSlot(),
					Property->ContainerPtrToValuePtr<void>(Object),
					Property->ContainerPtrToValuePtr<void>(Class->GetDefaultObject()));
			}

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

				FProperty* Property = Class->FindPropertyByName(FName(*PropName));
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
		}

		// ---- Out-of-line archive operator<< definitions ----

		FArchive& FDeltaWriter::operator<<(UObject*& Res)
		{
			if (IsInstancedNestedRef(GetSerializedProperty()))
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

			if (IsInstancedNestedRef(CurProp))
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

				// Apply ONTO the target's existing subobject (seeded by archetype-
				// cloning at NewObject time). Do NOT overwrite Res with the stale
				// source-time pointer from the stream -- that pointer is meaningless in
				// the target's context.
				if (Res && SubSize > 0)
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

			// ResolveObject only finds already-loaded objects -- no sync load. Apply
			// runs inside SerializeItem callbacks; a TryLoad here could re-enter PCG
			// or other systems via PostLoad on the loaded asset. The expected case is
			// that the source actor was loaded, which already pulled its references
			// into memory.
			UObject* Loaded = FSoftObjectPath(PathStr).ResolveObject();
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

	namespace FixupRegistry
	{
		/** Single registration record. Either Class (direct UClass* registration) or
		 *  PendingPath (soft-path registration resolving lazily) identifies the target. */
		struct FEntry
		{
			FSoftClassPath PendingPath;   // populated for unresolved path registrations
			TWeakObjectPtr<UClass> Class; // resolved target (may be invalid for pending)
			FPostApplyFixup Fixup;
		};

		// Registry state. Access is main-thread only in practice (registration from module
		// startup, RunAll from spawn paths). No locking.
		//
		// Entries are keyed by a monotonic uint64 ID so removals don't invalidate bucket
		// indices. Buckets are TMap<UClass*, TArray<ID>> keyed by the *resolved* target
		// class -- RunAll walks the component class hierarchy and does O(1) bucket lookups per super.
		static TMap<uint64, FEntry>& GetEntries()
		{
			static TMap<uint64, FEntry> Instance;
			return Instance;
		}

		static TMap<UClass*, TArray<uint64>>& GetBuckets()
		{
			static TMap<UClass*, TArray<uint64>> Instance;
			return Instance;
		}

		// IDs whose PendingPath hasn't resolved yet. Retried on each RunAll so registrations
		// made before their target module loaded pick up activation at the next apply.
		static TArray<uint64>& GetPendingIds()
		{
			static TArray<uint64> Instance;
			return Instance;
		}

		static uint64 AllocId()
		{
			static uint64 Counter = 0;
			return ++Counter;
		}

		/** Resolve the target class (if necessary) and attach the entry to its bucket. If
		 *  the path can't resolve yet, the entry is parked on GetPendingIds() and retried
		 *  at the next RunAll. */
		static void Bucket(uint64 Id)
		{
			FEntry* E = GetEntries().Find(Id);
			if (!E)
			{
				return;
			}

			UClass* Target = E->Class.Get();
			if (!Target && !E->PendingPath.IsNull())
			{
				Target = E->PendingPath.ResolveClass();
				if (Target)
				{
					E->Class = Target;
				}
			}

			if (Target)
			{
				GetBuckets().FindOrAdd(Target).AddUnique(Id);
			}
			else
			{
				GetPendingIds().AddUnique(Id);
			}
		}

		static void Unregister(uint64 Id)
		{
			if (Id == 0)
			{
				return;
			}
			FEntry Removed;
			if (GetEntries().RemoveAndCopyValue(Id, Removed))
			{
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
					// Class was GC'd between registration and unregister -- the bucket
					// key is now a dangling raw pointer we can't recover. Sweep all
					// buckets for the stale Id so it doesn't leak.
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
		}

		static uint64 Register(UClass* Class, const FSoftClassPath& Path, FPostApplyFixup Fixup)
		{
			if (!Fixup)
			{
				return 0;
			}
			if (!Class && Path.IsNull())
			{
				return 0;
			}

			const uint64 Id = AllocId();
			FEntry Entry;
			Entry.PendingPath = Path;
			Entry.Class = Class;
			Entry.Fixup = MoveTemp(Fixup);
			GetEntries().Add(Id, MoveTemp(Entry));
			Bucket(Id);
			return Id;
		}

		/** Best-effort retry of pending path resolutions. Cheap (FSoftClassPath::ResolveClass
		 *  is a hash lookup when the class is loaded) so we just run it every RunAll instead
		 *  of subscribing to module-load events. */
		static void TryResolvePending()
		{
			TArray<uint64>& Pending = GetPendingIds();
			if (Pending.IsEmpty())
			{
				return;
			}

			TMap<uint64, FEntry>& Entries = GetEntries();
			for (int32 i = Pending.Num() - 1; i >= 0; --i)
			{
				const uint64 Id = Pending[i];
				FEntry* E = Entries.Find(Id);
				if (!E)
				{
					// Entry was unregistered while pending.
					Pending.RemoveAtSwap(i, EAllowShrinking::No);
					continue;
				}

				UClass* Target = E->Class.Get();
				if (!Target && !E->PendingPath.IsNull())
				{
					Target = E->PendingPath.ResolveClass();
					if (Target)
					{
						E->Class = Target;
					}
				}

				if (Target)
				{
					GetBuckets().FindOrAdd(Target).AddUnique(Id);
					Pending.RemoveAtSwap(i, EAllowShrinking::No);
				}
			}
		}

		static void RunAll(AActor* Actor)
		{
			if (!Actor)
			{
				return;
			}
			TryResolvePending();

			TMap<UClass*, TArray<uint64>>& Buckets = GetBuckets();
			if (Buckets.IsEmpty())
			{
				return;
			}

			TMap<uint64, FEntry>& Entries = GetEntries();

			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}
				UObject* Archetype = Component->GetArchetype();

				// Walk the class hierarchy: a fixup registered against UFoo fires for
				// UFoo and anything that derives from it. Bucket lookup per super class
				// is O(1), so total cost per component is O(class-hierarchy depth) --
				// independent of registration count.
				for (UClass* Cls = Component->GetClass(); Cls; Cls = Cls->GetSuperClass())
				{
					if (TArray<uint64>* Bucket = Buckets.Find(Cls))
					{
						for (uint64 Id : *Bucket)
						{
							if (FEntry* E = Entries.Find(Id))
							{
								// Bucket key is a raw UClass*; if the class was GC'd
								// the key dangles and could match a new class at the
								// same address. The entry's weak pointer is the
								// authoritative liveness check.
								if (E->Class.Get() != Cls)
								{
									UE_LOG(LogPCGEx, Warning,
									       TEXT("[PCGExActorDelta] Skipping fixup whose target class was garbage-collected; the registration is now stale."));
									continue;
								}
								E->Fixup(Component, Archetype);
							}
						}
					}
				}
			}
		}
	}

	// ---- FPostApplyFixupHandle ----

	FPostApplyFixupHandle::~FPostApplyFixupHandle()
	{
		if (Id != 0)
		{
			FixupRegistry::Unregister(Id);
		}
	}

	FPostApplyFixupHandle::FPostApplyFixupHandle(FPostApplyFixupHandle&& Other) noexcept
		: Id(Other.Id)
	{
		Other.Id = 0;
	}

	FPostApplyFixupHandle& FPostApplyFixupHandle::operator=(FPostApplyFixupHandle&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Id != 0)
			{
				FixupRegistry::Unregister(Id);
			}
			Id = Other.Id;
			Other.Id = 0;
		}
		return *this;
	}

	void FPostApplyFixupHandle::Reset()
	{
		if (Id != 0)
		{
			FixupRegistry::Unregister(Id);
			Id = 0;
		}
	}

	/** Handle factory. Defined in the cpp (not exported) and befriended by the handle. */
	class FPostApplyFixupHandleFactory
	{
	public:
		static FPostApplyFixupHandle Make(uint64 Id)
		{
			FPostApplyFixupHandle Handle;
			Handle.Id = Id;
			return Handle;
		}
	};

	FPostApplyFixupHandle RegisterPostApplyFixup(UClass* ComponentClass, FPostApplyFixup Fixup)
	{
		const uint64 Id = FixupRegistry::Register(ComponentClass, FSoftClassPath(), MoveTemp(Fixup));
		return FPostApplyFixupHandleFactory::Make(Id);
	}

	FPostApplyFixupHandle RegisterPostApplyFixup(const FSoftClassPath& ClassPath, FPostApplyFixup Fixup)
	{
		const uint64 Id = FixupRegistry::Register(nullptr, ClassPath, MoveTemp(Fixup));
		return FPostApplyFixupHandleFactory::Make(Id);
	}

	TArray<uint8> SerializeActorDelta(AActor* Actor, TArray<FSoftObjectPath>* OutCollaterals)
	{
		if (!Actor)
		{
			return {};
		}

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

		if (ActorBytes.IsEmpty() && ComponentDeltas.IsEmpty())
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

		return Result;
	}

	void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes)
	{
		if (!Actor || DeltaBytes.IsEmpty())
		{
			return;
		}

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
			const FName CompName(*CompNameStr);

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
				const FSoftClassPath CompClassPath(CompClassPathStr);
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
				if (UClass* SrcClass = FSoftClassPath(CompClassPathStr).ResolveClass())
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
				Component->RegisterComponent();
			}
			else
			{
				// Writing directly to a registered component leaves the render proxy
				// pointing at stale data (observed: splines rendering with no points
				// after apply). FComponentReregisterContext unregisters on construct,
				// re-registers on scope exit -- rebuilds the proxy from updated state.
				FComponentReregisterContext ReregContext(Component);
				Internal::DeserializeObjectDelta(Component, CompBytes);
			}
		}

		// Repair engine-managed invariants that the per-property delta cannot express
		// (e.g. USplineComponent's Spline/SplineCurves aliasing in UE 5.7+).
		FixupRegistry::RunAll(Actor);
	}

	uint32 HashDelta(const TArray<uint8>& DeltaBytes)
	{
		if (DeltaBytes.IsEmpty())
		{
			return 0;
		}
		return FCrc::MemCrc32(DeltaBytes.GetData(), DeltaBytes.Num());
	}
}
