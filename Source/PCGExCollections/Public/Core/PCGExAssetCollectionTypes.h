// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"

#include "PCGExAssetCollectionTypes.generated.h"

class UPCGExAssetCollection;
struct FPCGExAssetCollectionEntry;
#if WITH_EDITOR
struct FAssetData;
#endif

/**
 * Runtime type registry for collection types. Allows the system to discover, query,
 * and check inheritance between collection types without compile-time coupling.
 *
 * Built-in types (registered via PCGEX_REGISTER_COLLECTION_TYPE):
 *   Base → Mesh, Actor, PCGDataAsset
 *
 * Registering a custom type:
 *   1. Define a FTypeId constant (just an FName):
 *        inline const FTypeId MyType = FName(TEXT("MyType"));
 *   2. In your collection .cpp, use the macro:
 *        PCGEX_REGISTER_COLLECTION_TYPE(MyType, UMyCollection, FMyEntry, "My Collection", Base)
 *      This registers at static init via pending registration (safe before module load).
 *   3. Your collection's GetTypeId() should return your FTypeId.
 *
 * Querying:
 *   FTypeRegistry::Get().GetInfo(TypeIds::Mesh, Out)   -- copy FTypeInfo out by ID
 *   FTypeRegistry::Get().GetInfoByClass(UClass*, Out)  -- reverse lookup from UClass
 *   FTypeRegistry::Get().IsA(Mesh, Base)               -- inheritance check
 *   Entry->IsType(TypeIds::Mesh)                       -- check entry type
 *
 * Thread safety: storage is a value TMap guarded by an FRWLock, and late registrations
 * (hot-reload, late-loaded plugins) can relocate every stored FTypeInfo. Interior pointers
 * must therefore NEVER escape the lock -- which is why every lookup is copy-out. Pointer
 * VALUES copied out of an info (EntryStruct, GlobalsStruct, CollectionClass/StateClass
 * targets) are stable UObjects and safe to keep.
 */

namespace PCGExAssetCollection
{
	// Type identifier - FName for debuggability and Unreal integration
	using FTypeId = FName;

	// "Native" type IDs
	namespace TypeIds
	{
		inline const FTypeId None = NAME_None;
		inline const FTypeId Base = FName(TEXT("Base"));
		inline const FTypeId Mesh = FName(TEXT("Mesh"));
		inline const FTypeId SkinnedMesh = FName(TEXT("SkinnedMesh"));
		inline const FTypeId Actor = FName(TEXT("Actor"));
		inline const FTypeId PCGDataAsset = FName(TEXT("PCGDataAsset"));
		inline const FTypeId Level = FName(TEXT("Level"));
		inline const FTypeId Variant = FName(TEXT("Variant"));
		inline const FTypeId Omni = FName(TEXT("Omni"));
	}

	/**
	 * Information about a registered collection type
	 */
	struct PCGEXCOLLECTIONS_API FTypeInfo
	{
		FTypeId Id = NAME_None;
		TWeakObjectPtr<UClass> CollectionClass = nullptr;

		// Null for heterogeneous collection types (Omni) with no single entry struct --
		// resolve per entry via Entry->GetTypeId() -> Find(TypeId)->EntryStruct instead.
		UScriptStruct* EntryStruct = nullptr;

		// The type's globals-block struct, when it has one. Consumed by conversion/merge
		// and config-block UI. Registered via AddPendingCustomization.
		UScriptStruct* GlobalsStruct = nullptr;

		// The type's machinery state/processor class (UPCGExCollectionTypeState derivative),
		// when the type has cross-entry collection machinery. Heterogeneous hosts instantiate
		// one per present entry type and dispatch their lifecycle into it; a registered class
		// is also what makes such hosts answer SupportsTypeMachinery for the type.
		TWeakObjectPtr<UClass> StateClass = nullptr;

		FText DisplayName;
		FTypeId ParentType = NAME_None; // For inheritance checking

#if WITH_EDITOR
		/**
		 * True when the given content-browser asset can seed an entry of this type (drives
		 * Omni drop routing). Register via AddPendingCustomization; null = doesn't participate.
		 */
		TFunction<bool(const FAssetData&)> DetectSourceAsset;

		/**
		 * Optional: build a fully-initialized entry payload from a detected asset, when the
		 * generic InitializeAs + SetAssetPath path isn't enough (e.g. Actor resolves the
		 * Blueprint's GeneratedClass). Return false to reject the asset after all.
		 */
		TFunction<bool(const FAssetData&, FInstancedStruct&)> MakeEntryFromSourceAsset;

		/** Lower values are offered the asset first when several detectors could claim it. */
		int32 SourceDetectPriority = 100;
#endif

		bool IsValid() const
		{
			return Id != NAME_None && CollectionClass.IsValid();
		}
	};

	/**
	 * Singleton registry for collection types
	 */
	class PCGEXCOLLECTIONS_API FTypeRegistry
	{
	public:
		static FTypeRegistry& Get();

		/**
		 * Register a new collection type
		 * @return The registered type ID, or NAME_None if registration failed
		 */
		FTypeId Register(const FTypeInfo& Info);

		/** Copy type info out by ID. Returns false when the id is unregistered. */
		bool GetInfo(FTypeId Id, FTypeInfo& OutInfo) const;

		/** Copy type info out by collection class (walks super classes). */
		bool GetInfoByClass(const UClass* Class, FTypeInfo& OutInfo) const;

		/** Copy type info out by entry struct (walks super structs). */
		bool GetInfoByEntryStruct(const UScriptStruct* Struct, FTypeInfo& OutInfo) const;

		/**
		 * Copy type info out by ID, resolving machinery inheritance: when the type itself
		 * registers no GlobalsStruct / StateClass, the nearest ancestor's (ParentType chain)
		 * fills them in. Everything else in the copy is the LEAF registration. This is how
		 * capability queries and per-type setup stay consistent with the lineage-aware entry
		 * checks (IsA/IsType) -- a type derived from PCGDataAsset runs the base machinery.
		 */
		bool GetInfoResolved(FTypeId Id, FTypeInfo& OutInfo) const;

		/** The registered parent type id, or None when unregistered / root. */
		FTypeId GetParentType(FTypeId Id) const;

		/** The registered entry struct, or null. (UScriptStruct values are stable objects.) */
		const UScriptStruct* GetEntryStruct(FTypeId Id) const;

		/**
		 * Apply a mutator to a registered entry. Must NOT re-enter the registry (FRWLock is
		 * non-recursive). Prefer AddPendingCustomization when registration order isn't guaranteed.
		 */
		void Customize(FTypeId Id, TFunctionRef<void(FTypeInfo&)> Mutator);

		/** Check if a type is or derives from another type */
		bool IsA(FTypeId Type, FTypeId BaseType) const;

		/** Get all registered type IDs */
		void GetAllTypeIds(TArray<FTypeId>& OutIds) const;

		/**
		 * Iterate over all registered types under the read lock. The callback must NOT
		 * re-enter the registry (FRWLock is non-recursive) and must NOT retain references or
		 * pointers to the visited infos -- copy values out instead (see class doc).
		 */
		template <typename Func>
		void ForEach(Func&& Callback) const
		{
			FReadScopeLock Lock(RegistryLock);
			for (const auto& Pair : Types)
			{
				Callback(Pair.Value);
			}
		}

		static void AddPendingRegistration(TFunction<void()>&& Func);

		/**
		 * Queue a customization applied AFTER every pending registration (or immediately if
		 * registration already ran) -- cross-TU static-init ordering never matters.
		 */
		static void AddPendingCustomization(FTypeId Id, TFunction<void(FTypeInfo&)>&& Mutator);

		static void ProcessPendingRegistrations();

	private:
		FTypeRegistry() = default;

		/** Interior lookup -- caller must hold RegistryLock. The returned pointer is only
		 *  valid while the lock is held (storage relocates on later registrations). */
		const FTypeInfo* FindUnsafe(FTypeId Id) const
		{
			return Types.Find(Id);
		}

		static TArray<TFunction<void()>>& GetPendingRegistrations();
		static TArray<TPair<FTypeId, TFunction<void(FTypeInfo&)>>>& GetPendingCustomizations();
		static bool& IsProcessed();

		mutable FRWLock RegistryLock;
		TMap<FTypeId, FTypeInfo> Types;
		TMap<TWeakObjectPtr<UClass>, FTypeId> ClassToType;
		TMap<UScriptStruct*, FTypeId> StructToType;
	};

	/**
	 * Helper macro for registering collection types at static init
	 * Place in the cpp file of your collection class
	 */
#define PCGEX_REGISTER_COLLECTION_TYPE(_TypeId, _CollectionClass, _EntryStruct, _DisplayName, _ParentType) \
namespace { \
struct FAutoRegister##_CollectionClass { \
FAutoRegister##_CollectionClass() { \
PCGExAssetCollection::FTypeRegistry::AddPendingRegistration([]() { \
PCGExAssetCollection::FTypeInfo Info; \
Info.Id = PCGExAssetCollection::TypeIds::_TypeId; \
Info.CollectionClass = _CollectionClass::StaticClass(); \
Info.EntryStruct = _EntryStruct::StaticStruct(); \
Info.DisplayName = NSLOCTEXT("PCGEx", #_TypeId "Collection", _DisplayName); \
Info.ParentType = PCGExAssetCollection::TypeIds::_ParentType; \
PCGExAssetCollection::FTypeRegistry::Get().Register(Info); \
}); \
} \
} GAutoRegister##_CollectionClass; \
}
}

/**
 * Type set - efficient storage for multiple type IDs
 * Replaces the bit-flag enum approach
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExCollectionTypeSet
{
	GENERATED_BODY()

	FPCGExCollectionTypeSet() = default;

	explicit FPCGExCollectionTypeSet(PCGExAssetCollection::FTypeId SingleType);

	FPCGExCollectionTypeSet(std::initializer_list<PCGExAssetCollection::FTypeId> InTypes);

	void Add(PCGExAssetCollection::FTypeId Type)
	{
		Types.Add(Type);
	}

	void Remove(PCGExAssetCollection::FTypeId Type)
	{
		Types.Remove(Type);
	}

	bool Contains(PCGExAssetCollection::FTypeId Type) const
	{
		return Types.Contains(Type);
	}

	bool IsEmpty() const
	{
		return Types.IsEmpty();
	}

	int32 Num() const
	{
		return Types.Num();
	}

	// Check if this set contains a type or any of its parent types
	bool ContainsOrDerives(PCGExAssetCollection::FTypeId Type) const;

	FPCGExCollectionTypeSet operator|(const FPCGExCollectionTypeSet& Other) const;
	FPCGExCollectionTypeSet operator&(const FPCGExCollectionTypeSet& Other) const;

private:
	UPROPERTY()
	TSet<FName> Types;
};
