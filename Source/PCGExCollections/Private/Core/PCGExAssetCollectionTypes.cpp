// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExAssetCollectionTypes.h"

#include "PCGExLog.h"

namespace PCGExAssetCollection
{
	FTypeRegistry& FTypeRegistry::Get()
	{
		static FTypeRegistry Instance;
		return Instance;
	}

	FTypeId FTypeRegistry::Register(const FTypeInfo& Info)
	{
		if (Info.Id == NAME_None)
		{
			UE_LOG(LogTemp, Warning, TEXT("PCGExAssetCollection: Cannot register type with NAME_None"));
			return NAME_None;
		}

		FWriteScopeLock Lock(RegistryLock);

		if (Types.Contains(Info.Id))
		{
			UE_LOG(LogTemp, Warning, TEXT("PCGExAssetCollection: Type '%s' already registered"), *Info.Id.ToString());
			return Info.Id; // Return existing - idempotent
		}

		Types.Add(Info.Id, Info);

		if (Info.CollectionClass.IsValid())
		{
			ClassToType.Add(Info.CollectionClass, Info.Id);
		}

		if (Info.EntryStruct)
		{
			StructToType.Add(Info.EntryStruct, Info.Id);
		}

		UE_LOG(LogTemp, Verbose, TEXT("PCGExAssetCollection: Registered type '%s'"), *Info.Id.ToString());
		return Info.Id;
	}


	void FTypeRegistry::Customize(FTypeId Id, TFunctionRef<void(FTypeInfo&)> Mutator)
	{
		FWriteScopeLock Lock(RegistryLock);

		FTypeInfo* Info = Types.Find(Id);
		if (!Info)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("PCGExAssetCollection: Cannot customize unregistered type '%s'"), *Id.ToString());
			return;
		}

		Mutator(*Info);
	}

	// Two-phase registration: before module startup, registrations are queued.
	// ProcessPendingRegistrations() flushes the queue during module init.
	// Late registrations (e.g. hot-reload or plugins loaded after init) execute immediately.
	void FTypeRegistry::AddPendingRegistration(TFunction<void()>&& Func)
	{
		if (IsProcessed())
		{
			Func();
		}
		else
		{
			GetPendingRegistrations().Add(MoveTemp(Func));
		}
	}

	void FTypeRegistry::AddPendingCustomization(FTypeId Id, TFunction<void(FTypeInfo&)>&& Mutator)
	{
		if (IsProcessed())
		{
			Get().Customize(Id, Mutator);
		}
		else
		{
			GetPendingCustomizations().Emplace(Id, MoveTemp(Mutator));
		}
	}

	void FTypeRegistry::ProcessPendingRegistrations()
	{
		if (IsProcessed())
		{
			return;
		}
		IsProcessed() = true;

		for (auto& Func : GetPendingRegistrations())
		{
			Func();
		}
		GetPendingRegistrations().Empty();
		GetPendingRegistrations().Shrink();

		// Customizations run after every registration -- cross-TU static-init order never matters.
		for (auto& Pair : GetPendingCustomizations())
		{
			Get().Customize(Pair.Key, Pair.Value);
		}
		GetPendingCustomizations().Empty();
		GetPendingCustomizations().Shrink();
	}

	TArray<TFunction<void()>>& FTypeRegistry::GetPendingRegistrations()
	{
		static TArray<TFunction<void()>> Pending;
		return Pending;
	}

	TArray<TPair<FTypeId, TFunction<void(FTypeInfo&)>>>& FTypeRegistry::GetPendingCustomizations()
	{
		static TArray<TPair<FTypeId, TFunction<void(FTypeInfo&)>>> Pending;
		return Pending;
	}

	bool& FTypeRegistry::IsProcessed()
	{
		static bool bProcessed = false;
		return bProcessed;
	}

	bool FTypeRegistry::GetInfo(FTypeId Id, FTypeInfo& OutInfo) const
	{
		FReadScopeLock Lock(RegistryLock);
		if (const FTypeInfo* Info = FindUnsafe(Id))
		{
			OutInfo = *Info;
			return true;
		}
		return false;
	}

	bool FTypeRegistry::GetInfoByClass(const UClass* Class, FTypeInfo& OutInfo) const
	{
		if (!Class)
		{
			return false;
		}

		FReadScopeLock Lock(RegistryLock);

		// Direct lookup, then parent classes
		for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
		{
			if (const FTypeId* Id = ClassToType.Find(MakeWeakObjectPtr(const_cast<UClass*>(Current))))
			{
				if (const FTypeInfo* Info = FindUnsafe(*Id))
				{
					OutInfo = *Info;
					return true;
				}
			}
		}

		return false;
	}

	bool FTypeRegistry::GetInfoByEntryStruct(const UScriptStruct* Struct, FTypeInfo& OutInfo) const
	{
		if (!Struct)
		{
			return false;
		}

		FReadScopeLock Lock(RegistryLock);

		// Direct lookup, then parent structs
		for (const UScriptStruct* Current = Struct;
		     Current;
		     Current = Cast<UScriptStruct>(Current->GetSuperStruct()))
		{
			if (const FTypeId* Id = StructToType.Find(Current))
			{
				if (const FTypeInfo* Info = FindUnsafe(*Id))
				{
					OutInfo = *Info;
					return true;
				}
			}
		}

		return false;
	}

	bool FTypeRegistry::GetInfoResolved(FTypeId Id, FTypeInfo& OutInfo) const
	{
		FReadScopeLock Lock(RegistryLock);

		const FTypeInfo* Leaf = FindUnsafe(Id);
		if (!Leaf)
		{
			return false;
		}

		OutInfo = *Leaf;

		// Fill missing machinery slots from the nearest ancestor registration.
		FTypeId Current = Leaf->ParentType;
		while (Current != NAME_None && (!OutInfo.GlobalsStruct || !OutInfo.StateClass.IsValid()))
		{
			const FTypeInfo* Ancestor = FindUnsafe(Current);
			if (!Ancestor)
			{
				break;
			}

			if (!OutInfo.GlobalsStruct)
			{
				OutInfo.GlobalsStruct = Ancestor->GlobalsStruct;
			}
			if (!OutInfo.StateClass.IsValid())
			{
				OutInfo.StateClass = Ancestor->StateClass;
			}

			Current = Ancestor->ParentType;
		}

		return true;
	}

	FTypeId FTypeRegistry::GetParentType(FTypeId Id) const
	{
		FReadScopeLock Lock(RegistryLock);
		const FTypeInfo* Info = FindUnsafe(Id);
		return Info ? Info->ParentType : NAME_None;
	}

	const UScriptStruct* FTypeRegistry::GetEntryStruct(FTypeId Id) const
	{
		FReadScopeLock Lock(RegistryLock);
		const FTypeInfo* Info = FindUnsafe(Id);
		return Info ? Info->EntryStruct : nullptr;
	}

	// Walks the ParentType chain from Type upward, checking for BaseType at each level.
	bool FTypeRegistry::IsA(FTypeId Type, FTypeId BaseType) const
	{
		if (Type == BaseType)
		{
			return true;
		}
		if (Type == NAME_None || BaseType == NAME_None)
		{
			return false;
		}

		FReadScopeLock Lock(RegistryLock);

		// Walk up the parent chain
		FTypeId Current = Type;
		while (Current != NAME_None)
		{
			if (Current == BaseType)
			{
				return true;
			}

			const FTypeInfo* Info = Types.Find(Current);
			if (!Info)
			{
				break;
			}

			Current = Info->ParentType;
		}

		return false;
	}

	void FTypeRegistry::GetAllTypeIds(TArray<FTypeId>& OutIds) const
	{
		FReadScopeLock Lock(RegistryLock);
		Types.GetKeys(OutIds);
	}
}

FPCGExCollectionTypeSet::FPCGExCollectionTypeSet(PCGExAssetCollection::FTypeId SingleType)
{
	Types.Add(SingleType);
}

FPCGExCollectionTypeSet::FPCGExCollectionTypeSet(std::initializer_list<PCGExAssetCollection::FTypeId> InTypes)
{
	for (const auto& Type : InTypes)
	{
		Types.Add(Type);
	}
}

bool FPCGExCollectionTypeSet::ContainsOrDerives(PCGExAssetCollection::FTypeId Type) const
{
	if (Types.Contains(Type))
	{
		return true;
	}

	for (const auto& T : Types)
	{
		if (PCGExAssetCollection::FTypeRegistry::Get().IsA(T, Type))
		{
			return true;
		}
	}
	return false;
}

FPCGExCollectionTypeSet FPCGExCollectionTypeSet::operator|(const FPCGExCollectionTypeSet& Other) const
{
	FPCGExCollectionTypeSet Result = *this;
	Result.Types.Append(Other.Types);
	return Result;
}

FPCGExCollectionTypeSet FPCGExCollectionTypeSet::operator&(const FPCGExCollectionTypeSet& Other) const
{
	FPCGExCollectionTypeSet Result;
	for (const auto& Type : Types)
	{
		if (Other.Types.Contains(Type))
		{
			Result.Types.Add(Type);
		}
	}
	return Result;
}
