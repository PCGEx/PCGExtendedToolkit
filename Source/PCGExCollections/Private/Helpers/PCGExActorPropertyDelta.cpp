// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyDelta.h"

#include "ComponentReregisterContext.h"
#include "PCGExLog.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorDeltaExtensionRegistry.h"
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
	// Wire format, persisted in collection assets. A delta without this magic, or of another version, is skipped, and its
	// collection must be rebuilt. Every segment is length-prefixed so a reader skips what it does not know: the optional
	// sections were added that way, without a version bump.
	//
	// Actor delta:
	//   [u32 'PCGE'][u32 Version][u32 Size][object delta of the actor, vs. its CDO]
	//   [u32 Count] x [FString Name][FString ClassPath][u32 Size][object delta of the component, vs. its archetype]
	//   only when an extension captured a payload: [u32 'EXTN'][u32 SectionVersion][u32 Size]
	//     [u32 Count] x [FString Id][u32 Version][FString Target][u32 Size][payload]
	// Object delta, empty when nothing differs:
	//   [u32 Count] x [FString PropName][u32 Size][SerializeItem bytes]
	//   [u32 Count] x [FString PropName][u32 Size][object delta of the subobject, vs. the defaults' one in that slot]
	//   tagged sections, each only when non-empty, [u32 Tag][u32 Size] then:
	//     'INST' [u32 Count] x property records of IsInstancedContainer properties, values written whole
	//     'CRTN' [u32 Count] x [FString PropName][FString ClassPath][FString Name][u32 Size][object delta, vs. its archetype]
	// In SerializeItem bytes, FNames are strings and object references soft paths (empty for null), except a nested
	// instanced object: [u8 ENestedRecord], then unless Null [FString ClassPath], [FString Name] if Named, and
	// [u32 Size][object delta, vs. its archetype if Named, else its class CDO].
	static constexpr uint32 DeltaWireMagic = 0x50434745u; // 'PCGE'
	static constexpr uint32 DeltaWireVersion = 6u;

	static constexpr uint32 ExtensionSectionMagic = 0x4558544Eu; // 'EXTN'
	static constexpr uint32 ExtensionSectionVersion = 1u;

	// Past this, a created subobject's record is reported once: a large capture is usually derived data.
	static constexpr int32 CreatedSubobjectReportSize = 64 * 1024;

	// Tags of the optional sections closing an object delta.
	static constexpr uint32 InstancedContainersTag = 0x494E5354u; // 'INST'
	static constexpr uint32 CreatedSubobjectsTag = 0x4352544Eu;   // 'CRTN'

#pragma region Internal

	namespace Internal
	{
		/** Transient or deprecated: no state a delta restores lives in these. */
		constexpr EPropertyFlags SkippedPropertyFlags = CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated;

		/** A subobject records every persistent property: its state is internal and rarely editable. An actor or component
		 *  records only what can be edited on the instance, never engine bookkeeping, and its root leaves out the relative
		 *  transform, which the spawn point drives. */
		bool ShouldIncludeProperty(const FProperty* Property, const bool bIsSubobject, const bool bSkipTransformProps)
		{
			if (bIsSubobject)
			{
				return !Property->HasAnyPropertyFlags(SkippedPropertyFlags);
			}

			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance))
			{
				return false;
			}

			if (!bSkipTransformProps)
			{
				return true;
			}

			static const FName NAME_RelativeLocation = USceneComponent::GetRelativeLocationPropertyName();
			static const FName NAME_RelativeRotation = USceneComponent::GetRelativeRotationPropertyName();
			static const FName NAME_RelativeScale3D = USceneComponent::GetRelativeScale3DPropertyName();
			const FName PropName = Property->GetFName();
			return PropName != NAME_RelativeLocation && PropName != NAME_RelativeRotation && PropName != NAME_RelativeScale3D;
		}

		/** The property a record names, unless its class lost it or no longer persists it. */
		template <typename T = FProperty>
		T* FindRecordProperty(const UClass* Class, const FString& PropName)
		{
			T* Property = CastField<T>(Class->FindPropertyByName(PCGExTypeOps::MakeNameChecked(PropName)));
			return (Property && !Property->HasAnyPropertyFlags(SkippedPropertyFlags)) ? Property : nullptr;
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

		/** The instanced object property current at a nested position (a struct member or a container element, never a class
		 *  member), or null: the archives inline an object delta there and write a soft path anywhere else. */
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

		/** Writes FNames as strings and object references as soft paths, or as inline deltas at nested instanced positions
		 *  (see the wire format): the base archives write session-local name indices and raw pointers. */
		class FDeltaWriter : public FObjectWriter
		{
		public:
			using FObjectWriter::FObjectWriter;

			/** The object whose property is being written: a nested instanced object it owns is recorded by name. */
			const UObject* Owner = nullptr;

			/** Cycle guard of the capture root, shared by every nested writer. */
			TSet<const UObject*>* VisitedSources = nullptr;

			/** Assets and non-native classes the delta references, which the spawn element loads before applying it so the
			 *  reader resolves them without loading. Null collects nothing. */
			TSet<FSoftObjectPath>* CollateralSink = nullptr;

			virtual FArchive& operator<<(FName& Name) override
			{
				FString AsString = Name.ToString();
				*this << AsString;
				return *this;
			}

			virtual FArchive& operator<<(UObject*& Res) override;

			/** TObjectPtr storage serializes through this virtual instead of operator<<(UObject*&): routed the same way. */
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

			/** TObjectPtr storage serializes through this virtual. Only a nested instanced position reads the current value, the
			 *  object to apply onto: reading every other would resolve, and sync-load, each reference of the target. A null
			 *  source leaves the target's value, safer than nulling a slot its component may require. */
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

		/** A subobject one of Object's slots holds. */
		struct FSubobjectBinding
		{
			FString PropName;
			UObject* Instance;
			UObject* Defaults; // The parallel default subobject, or the archetype of a created one.
			bool bCreated;     // Defaults hold no same-class subobject: recorded with its class and name, recreated on apply.
		};

		/** Binds the subobject Inst a slot holds to the defaults' same-class one in that slot or, when they hold none, records
		 *  it as created against its archetype. Components have records of their own; transient instances and classes
		 *  IsRecreatableSubobjectClass refuses are left out. */
		void BindSubobject(const FObjectProperty* ObjProp, UObject* Inst, UObject* Defaults, TArray<FSubobjectBinding>& Out)
		{
			if (Inst->IsA<UActorComponent>())
			{
				return;
			}

			UObject* Def = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Defaults));
			if (Def && Def->GetClass() == Inst->GetClass())
			{
				Out.Add({ObjProp->GetName(), Inst, Def, false});
				return;
			}

			if (!Inst->HasAnyFlags(RF_Transient) && IsRecreatableSubobjectClass(Inst->GetClass()))
			{
				Out.Add({ObjProp->GetName(), Inst, Inst->GetArchetype(), true});
			}
		}

		/** One record per property (see the wire format). An 'INST' record holds the whole value, serialized with the property
		 *  current (see IsInstancedContainer). */
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

				// Its own buffer for the length prefix, with the outer writer's custom versions (FSpline needs them).
				TArray<uint8> ValueBytes;
				{
					FDeltaWriter ValueWriter(ValueBytes);
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

		/** Object's delta against Defaults (see the wire format), empty when nothing differs. */
		void SerializeObjectDelta(
			UObject* Object,
			UObject* Defaults,
			TArray<uint8>& OutBytes,
			TSet<const UObject*>& VisitedSources,
			bool bIsSubobject,
			bool bSkipTransformProps,
			TSet<FSoftObjectPath>* CollateralSink)
		{
			if (!Object || !Defaults)
			{
				return;
			}

			// Outer chains cannot cycle, but nested instanced references can.
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
			TArray<FSubobjectBinding> Subobjects;
			for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				if (Claims.Contains(Property->GetFName()))
				{
					continue;
				}

				// Reading a slot can resolve its reference: one neither a record nor a binding uses stays unread.
				const bool bInclude = ShouldIncludeProperty(Property, bIsSubobject, bSkipTransformProps);
				const bool bBindable = !Property->HasAnyPropertyFlags(SkippedPropertyFlags);
				if (!bInclude && !bBindable)
				{
					continue;
				}

				// An object Object owns is a subobject, never written as a pointer to the source's own object. Ownership
				// decides, not CPF_InstancedReference: UHT sets that only for Instanced and DefaultToInstanced.
				if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
				{
					UObject* Inst = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Object));
					if (Inst && Inst->GetOuter() == Object)
					{
						if (bBindable)
						{
							BindSubobject(ObjProp, Inst, Defaults, Subobjects);
						}
						continue;
					}
				}

				if (bInclude && !Property->Identical_InContainer(Object, Defaults))
				{
					(IsInstancedContainer(Property) ? ContainersToWrite : PropsToWrite).Add(Property);
				}
			}

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

		/** A capture root (the actor, each component) starts its own cycle guard. */
		void SerializeObjectDelta(UObject* Object, UObject* Defaults, TArray<uint8>& OutBytes, bool bIsSubobject, bool bSkipTransformProps, TSet<FSoftObjectPath>* CollateralSink)
		{
			TSet<const UObject*> Visited;
			SerializeObjectDelta(Object, Defaults, OutBytes, Visited, bIsSubobject, bSkipTransformProps, CollateralSink);
		}

		/** Reads WritePropertyRecords' records onto Object, the property section's against Baseline (the writer's defaults).
		 *  Unknown properties are skipped, and so is a record in the wrong section: an instanced container's in the property
		 *  section comes from an older capture, which wrote paths to the source's own objects or a delta it cannot read. */
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
				FProperty* Property = FindRecordProperty(Class, PropName);
				if (!Property || ValueBytes.IsEmpty())
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
			FObjectProperty* ObjProp = FindRecordProperty<FObjectProperty>(Object->GetClass(), PropName);
			if (!ObjProp)
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

			// Absent only from a truncated stream.
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

				FObjectProperty* ObjProp = FindRecordProperty<FObjectProperty>(Class, PropName);
				if (SubBytes.IsEmpty() || !ObjProp)
				{
					continue;
				}

				// Only into an object Object owns: anything else is external state.
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

		FArchive& FDeltaWriter::operator<<(UObject*& Res)
		{
			if (AsNestedInstancedRef(GetSerializedProperty()))
			{
				// An object the owner owns is recorded by name and diffed against its archetype, so apply finds its counterpart;
				// any other is diffed against its class CDO.
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

				TArray<uint8> SubBytes;
				SerializeObjectDelta(Res, Baseline, SubBytes, *VisitedSources, /*bIsSubobject=*/true, /*bSkipTransformProps=*/false, CollateralSink);
				WriteSizedBytes(*this, SubBytes);
				return *this;
			}

			FString PathStr = Res ? FSoftObjectPath(Res).ToString() : FString();
			*this << PathStr;

			// Assets only: a subobject path (an AttachParent, another actor) fails the whole streamable batch.
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

			FString PathStr;
			*this << PathStr;

			if (PathStr.IsEmpty())
			{
				Res = nullptr;
				return *this;
			}

			// Resolve only, never load (see ApplyCreatedRecord): the spawn element loads the collaterals first.
			const FSoftObjectPath Path = PCGExTypeOps::MakePathChecked(PathStr);
			UObject* Loaded = Path.ResolveObject();
			if (!Loaded)
			{
				// Keeps the target's value rather than null a slot its component may require.
				return *this;
			}

			// An object of another type would corrupt the slot.
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

		/** The extension section (see the wire format). Its size lets a reader skip a section version it does not know. */
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

		struct FComponentDelta
		{
			FString Name;
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

			// A component added at runtime has its class CDO as archetype: diffed against it, and recreated on apply from its
			// recorded class when the target lacks it.
			const bool bIsRoot = (Component == RootComp);
			TArray<uint8> CompBytes;
			Internal::SerializeObjectDelta(Component, Archetype, CompBytes, /*bIsSubobject=*/false, /*bSkipTransformProps=*/bIsRoot, CollateralSinkPtr);

			if (!CompBytes.IsEmpty())
			{
				FComponentDelta CD;
				CD.Name = Component->GetName();
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

		// Only for a delta that is written: an empty one reports no collaterals.
		if (OutCollaterals)
		{
			OutCollaterals->Append(CollateralSet.Array());
		}

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
			Writer << CD.Name;
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
		/** Recreates, under the root, a component the source added at runtime and the target lacks, for its delta to land on.
		 *  Transient flags come from the actor, so a preview or runtime spawn never persists it. */
		UActorComponent* CreateInstanceComponent(AActor* Actor, UClass* Class, const FName Name)
		{
			if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()))
			{
				return nullptr;
			}

			const EObjectFlags Flags = RF_Transactional | (Actor->GetFlags() & (RF_Transient | RF_NonPIEDuplicateTransient));
			UActorComponent* Component = NewObject<UActorComponent>(Actor, Class, Name, Flags);
			if (!Component)
			{
				return nullptr;
			}

			Actor->AddInstanceComponent(Component);
			// Scene components need an attach parent before registration.
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
			{
				if (USceneComponent* Root = Actor->GetRootComponent())
				{
					SceneComp->SetupAttachment(Root);
				}
			}
			return Component;
		}

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

		// Another format would misread into the target: it is skipped, and its collection must be rebuilt.
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

			UClass* SourceClass = CompClassPathStr.IsEmpty() ? nullptr : PCGExTypeOps::MakePathChecked<FSoftClassPath>(CompClassPathStr).ResolveClass();

			UActorComponent* Component = FindObjectFast<UActorComponent>(Actor, CompName);
			bool bWasJustCreated = false;
			if (!Component)
			{
				Component = Internal::CreateInstanceComponent(Actor, SourceClass, CompName);
				bWasJustCreated = Component != nullptr;
			}
			else if (SourceClass && Component->GetClass() != SourceClass)
			{
				// Another class's payload would overwrite whichever of its fields share a name.
				continue;
			}

			if (!Component)
			{
				continue;
			}

			// The writer diffed each component against its archetype (the class CDO for a dynamic one).
			UObject* Baseline = Component->GetArchetype();
			if (bWasJustCreated)
			{
				// Not registered yet: the delta goes in before its first registration.
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

		// The fixups repair invariants a property delta cannot express (USplineComponent's Spline/SplineCurves aliasing).
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
