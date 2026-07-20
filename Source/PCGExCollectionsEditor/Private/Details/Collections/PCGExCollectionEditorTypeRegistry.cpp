// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"

FCollectionEditorTypeRegistry& FCollectionEditorTypeRegistry::Get()
{
	static FCollectionEditorTypeRegistry Instance;
	return Instance;
}

PCGExAssetCollection::FTypeId FCollectionEditorTypeRegistry::Register(FCollectionEditorTypeInfo&& Info)
{
	if (Info.Id == NAME_None)
	{
		UE_LOG(LogTemp, Warning, TEXT("FCollectionEditorTypeRegistry: Cannot register type with NAME_None"));
		return NAME_None;
	}

	FWriteScopeLock Lock(RegistryLock);

	if (Types.Contains(Info.Id))
	{
		UE_LOG(LogTemp, Warning, TEXT("FCollectionEditorTypeRegistry: Type '%s' already registered"), *Info.Id.ToString());
		return Info.Id;
	}

	const PCGExAssetCollection::FTypeId Id = Info.Id;
	const TWeakObjectPtr<UClass> ClassKey = Info.CollectionClass;

	Types.Add(Id, MoveTemp(Info));

	if (ClassKey.IsValid())
	{
		ClassToType.Add(ClassKey, Id);
	}

	return Id;
}

const FCollectionEditorTypeInfo* FCollectionEditorTypeRegistry::Find(PCGExAssetCollection::FTypeId Id) const
{
	FReadScopeLock Lock(RegistryLock);
	return Types.Find(Id);
}

const FCollectionEditorTypeInfo* FCollectionEditorTypeRegistry::FindByCollectionClass(const UClass* Class) const
{
	if (!Class)
	{
		return nullptr;
	}

	FReadScopeLock Lock(RegistryLock);

	if (const PCGExAssetCollection::FTypeId* Id = ClassToType.Find(MakeWeakObjectPtr(const_cast<UClass*>(Class))))
	{
		return Types.Find(*Id);
	}

	// Subclasses inherit their parent's editor metadata.
	for (const UClass* Current = Class->GetSuperClass(); Current; Current = Current->GetSuperClass())
	{
		if (const PCGExAssetCollection::FTypeId* Id = ClassToType.Find(MakeWeakObjectPtr(const_cast<UClass*>(Current))))
		{
			return Types.Find(*Id);
		}
	}

	return nullptr;
}

void FCollectionEditorTypeRegistry::Customize(PCGExAssetCollection::FTypeId Id, TFunctionRef<void(FCollectionEditorTypeInfo&)> Mutator)
{
	FWriteScopeLock Lock(RegistryLock);
	if (FCollectionEditorTypeInfo* Info = Types.Find(Id))
	{
		Mutator(*Info);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FCollectionEditorTypeRegistry::Customize: Type '%s' is not registered; mutator skipped."), *Id.ToString());
	}
}

void FCollectionEditorTypeRegistry::GetAll(TArray<const FCollectionEditorTypeInfo*>& OutInfos) const
{
	FReadScopeLock Lock(RegistryLock);
	OutInfos.Reset(Types.Num());
	for (const TPair<PCGExAssetCollection::FTypeId, FCollectionEditorTypeInfo>& Pair : Types)
	{
		OutInfos.Add(&Pair.Value);
	}
}

void FCollectionEditorTypeRegistry::AddPendingRegistration(TFunction<void()>&& Func)
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

void FCollectionEditorTypeRegistry::ProcessPendingRegistrations()
{
	if (IsProcessed())
	{
		return;
	}
	IsProcessed() = true;

	for (TFunction<void()>& Func : GetPendingRegistrations())
	{
		Func();
	}
	GetPendingRegistrations().Empty();
	GetPendingRegistrations().Shrink();
}

TArray<TFunction<void()>>& FCollectionEditorTypeRegistry::GetPendingRegistrations()
{
	static TArray<TFunction<void()>> Pending;
	return Pending;
}

bool& FCollectionEditorTypeRegistry::IsProcessed()
{
	static bool bProcessed = false;
	return bProcessed;
}
