// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorRegistry.h"

#include "PCGExLog.h"
#include "JsonObjectStructInterface.h"
#include "UObject/Class.h"

namespace PCGExMediatorRegistry
{
	struct FStorage
	{
		TMap<FName, TSharedRef<FPCGExMediatorFormat>> Formats;
		TMap<const UClass*, TSharedRef<FPCGExMediatorBinding>> Bindings;
	};

	FStorage& Storage()
	{
		static FStorage Instance;
		return Instance;
	}
}

void FPCGExMediatorRegistry::RegisterFormat(const FPCGExMediatorFormat& InFormat)
{
	if (!InFormat.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register format '%s': id, struct and converter are all required."), *InFormat.Id.ToString());
		return;
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Formats.Contains(InFormat.Id))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Format '%s' registered twice; the later registration wins."), *InFormat.Id.ToString());
		UnregisterFormat(InFormat.Id);
	}

	FJsonObjectStructInterfaceRegistry::RegisterStructConverter(InFormat.Struct, InFormat.Converter);
	S.Formats.Add(InFormat.Id, MakeShared<FPCGExMediatorFormat>(InFormat));
}

void FPCGExMediatorRegistry::UnregisterFormat(const FName InId)
{
	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (const TSharedRef<FPCGExMediatorFormat>* Found = S.Formats.Find(InId))
	{
		// Key only: UObjects may already be gone at module shutdown.
		FJsonObjectStructInterfaceRegistry::UnregisterStructConverter((*Found)->Struct);
		S.Formats.Remove(InId);
	}
}

TSharedPtr<const FPCGExMediatorFormat> FPCGExMediatorRegistry::FindFormat(const FName InId)
{
	if (const TSharedRef<FPCGExMediatorFormat>* Found = PCGExMediatorRegistry::Storage().Formats.Find(InId))
	{
		return *Found;
	}
	return nullptr;
}

TSharedPtr<const FPCGExMediatorFormat> FPCGExMediatorRegistry::FindFormatForStruct(const UScriptStruct* InStruct)
{
	for (const TPair<FName, TSharedRef<FPCGExMediatorFormat>>& Pair : PCGExMediatorRegistry::Storage().Formats)
	{
		if (Pair.Value->Struct == InStruct)
		{
			return Pair.Value;
		}
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetFormats(TArray<TSharedPtr<const FPCGExMediatorFormat>>& OutFormats)
{
	for (const TPair<FName, TSharedRef<FPCGExMediatorFormat>>& Pair : PCGExMediatorRegistry::Storage().Formats)
	{
		OutFormats.Add(Pair.Value);
	}
	OutFormats.Sort([](const TSharedPtr<const FPCGExMediatorFormat>& A, const TSharedPtr<const FPCGExMediatorFormat>& B)
	{
		return A->Id.LexicalLess(B->Id);
	});
}

void FPCGExMediatorRegistry::RegisterBinding(const FPCGExMediatorBinding& InBinding)
{
	if (!InBinding.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register a binding without a host class or members."));
		return;
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Bindings.Contains(InBinding.HostClass))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Binding for '%s' registered twice; the later registration wins."), *InBinding.HostClass->GetName());
	}
	S.Bindings.Add(InBinding.HostClass, MakeShared<FPCGExMediatorBinding>(InBinding));
}

void FPCGExMediatorRegistry::UnregisterBinding(const UClass* InHostClass)
{
	PCGExMediatorRegistry::Storage().Bindings.Remove(InHostClass);
}

TSharedPtr<const FPCGExMediatorBinding> FPCGExMediatorRegistry::FindBinding(const UClass* InHostClass)
{
	const PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	for (const UClass* Class = InHostClass; Class; Class = Class->GetSuperClass())
	{
		if (const TSharedRef<FPCGExMediatorBinding>* Found = S.Bindings.Find(Class))
		{
			return *Found;
		}
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetBindings(TArray<TSharedPtr<const FPCGExMediatorBinding>>& OutBindings)
{
	for (const TPair<const UClass*, TSharedRef<FPCGExMediatorBinding>>& Pair : PCGExMediatorRegistry::Storage().Bindings)
	{
		OutBindings.Add(Pair.Value);
	}
	OutBindings.Sort([](const TSharedPtr<const FPCGExMediatorBinding>& A, const TSharedPtr<const FPCGExMediatorBinding>& B)
	{
		return A->HostClass->GetName() < B->HostClass->GetName();
	});
}
