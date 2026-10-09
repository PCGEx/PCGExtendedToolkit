// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExSplineSampleHandler.h"

#include "Misc/ScopeRWLock.h"

#include "PCGExLog.h"

#pragma region FHandlerRegistry

namespace PCGExSplineSampling
{
	FHandlerRegistry& FHandlerRegistry::Get()
	{
		static FHandlerRegistry Instance;
		return Instance;
	}

	bool FHandlerRegistry::Register(const TSubclassOf<UPCGExSplineSampleHandler> HandlerClass)
	{
		if (!HandlerClass || HandlerClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Spline sample handler registration refused: null or abstract class."));
			return false;
		}

		// The registry holds raw class pointers: only a native class keeps one valid.
		if (!HandlerClass->HasAnyClassFlags(CLASS_Native))
		{
			UE_LOG(LogPCGEx, Error, TEXT("Spline sample handler '%s' refused: not a native class."), *HandlerClass->GetName());
			return false;
		}

		const FTopLevelAssetPath ComponentClassPath = HandlerClass->GetDefaultObject<UPCGExSplineSampleHandler>()->GetComponentClassPath();
		if (ComponentClassPath.IsNull())
		{
			UE_LOG(LogPCGEx, Error, TEXT("Spline sample handler '%s' refused: empty component class path."), *HandlerClass->GetName());
			return false;
		}

		FWriteScopeLock WriteLock(Lock);
		for (const TPair<FTopLevelAssetPath, FHandlerStack>& Registration : Registrations)
		{
			if (Registration.Value.Contains(HandlerClass.Get()))
			{
				UE_LOG(LogPCGEx, Error, TEXT("Spline sample handler '%s' is already registered."), *HandlerClass->GetName());
				return false;
			}
		}

		// Latest wins, until unregistered.
		Registrations.FindOrAdd(ComponentClassPath).Add(HandlerClass.Get());
		return true;
	}

	void FHandlerRegistry::Unregister(const UClass* HandlerClass)
	{
		FWriteScopeLock WriteLock(Lock);
		for (auto It = Registrations.CreateIterator(); It; ++It)
		{
			It.Value().Remove(HandlerClass);
			if (It.Value().IsEmpty()) { It.RemoveCurrent(); }
		}
	}

	const UPCGExSplineSampleHandler* FHandlerRegistry::Find(const UClass* ComponentClass) const
	{
		FReadScopeLock ReadLock(Lock);

		// Most-derived wins.
		for (const UClass* Class = ComponentClass; Class; Class = Class->GetSuperClass())
		{
			if (const FHandlerStack* Handlers = Registrations.Find(Class->GetClassPathName()))
			{
				return Handlers->Last()->GetDefaultObject<UPCGExSplineSampleHandler>();
			}
		}

		return nullptr;
	}
}

#pragma endregion
