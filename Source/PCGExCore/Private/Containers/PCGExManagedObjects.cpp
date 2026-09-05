// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Containers/PCGExManagedObjects.h"

#include "CoreMinimal.h"
#include "Containers/PCGExManagedObjectsInterfaces.h"

namespace PCGEx
{
	FPCGExAsyncStateScope::FPCGExAsyncStateScope(FPCGContext* InContext, const bool bDesired)
		: Context(InContext)
	{
		if (Context)
		{
			// Ensure PCG AsyncState is up to date
			bRestoreTo = Context->AsyncState.bIsRunningOnMainThread;
			Context->AsyncState.bIsRunningOnMainThread = bDesired;
		}
	}

	FPCGExAsyncStateScope::~FPCGExAsyncStateScope()
	{
		if (Context)
		{
			Context->AsyncState.bIsRunningOnMainThread = bRestoreTo;
		}
	}

	FManagedObjects::FManagedObjects(const TWeakPtr<FPCGContextHandle>& InContextHandle, const TWeakPtr<FWorkHandle>& InWorkHandle)
		: WorkHandle(InWorkHandle)
		  , WeakHandle(InContextHandle)
	{
	}

	FManagedObjects::~FManagedObjects()
	{
		Flush();
	}

	bool FManagedObjects::IsAvailable() const
	{
		FReadScopeLock WriteScopeLock(ManagedObjectLock);
		return WeakHandle.IsValid() && !IsFlushing();
	}

	// Atomic one-shot flush: compare_exchange ensures only one thread enters the flush path.
	// Objects are removed from root (allowing GC) and have their Async internal flag cleared
	// recursively, since PCGEx creates UObjects off the game thread and the Async flag must
	// be cleared before the GC can safely collect them.
	void FManagedObjects::Flush()
	{
		bool bExpected = false;
		if (bIsFlushing.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
		{
			FWriteScopeLock WriteScopeLock(ManagedObjectLock);

			// Flush remaining managed objects & mark them as garbage
			for (UObject* ObjectPtr : ManagedObjects)
			{
				/*FCOLLECTOR_IMPL*/
				ObjectPtr->RemoveFromRoot();
				RecursivelyClearAsyncFlag_Unsafe(ObjectPtr);

				if (IPCGExManagedObjectInterface* ManagedObject = Cast<IPCGExManagedObjectInterface>(ObjectPtr))
				{
					ManagedObject->Cleanup();
				}
			}

			ManagedObjects.Empty();
			bIsFlushing.store(false, std::memory_order_release);
		}
	}

	bool FManagedObjects::Add(UObject* InObject)
	{
		check(!IsFlushing())

		if (!IsValid(InObject))
		{
			return false;
		}

		bool bIsAlreadyInSet = false;
		{
			FWriteScopeLock WriteScopeLock(ManagedObjectLock);
			ManagedObjects.Add(InObject, &bIsAlreadyInSet);
			/*FCOLLECTOR_IMPL*/
			InObject->AddToRoot();
		}

		return !bIsAlreadyInSet;
	}

	bool FManagedObjects::Remove(UObject* InObject)
	{
		if (IsFlushing())
		{
			return false;
		} // Will be removed anyway

		{
			FWriteScopeLock WriteScopeLock(ManagedObjectLock);

			if (!IsValid(InObject))
			{
				return false;
			}
			int32 Removed = ManagedObjects.Remove(InObject);
			if (Removed == 0)
			{
				return false;
			}

			/*FCOLLECTOR_IMPL*/
			InObject->RemoveFromRoot();
			RecursivelyClearAsyncFlag_Unsafe(InObject);
		}

		if (IPCGExManagedObjectInterface* ManagedObject = Cast<IPCGExManagedObjectInterface>(InObject))
		{
			ManagedObject->Cleanup();
		}

		return true;
	}

	void FManagedObjects::Remove(const TArray<FPCGTaggedData>& InTaggedData)
	{
		if (IsFlushing())
		{
			return;
		} // Will be removed anyway

		TRACE_CPUPROFILER_EVENT_SCOPE(FManagedObjects::Remove);

		{
			FWriteScopeLock WriteScopeLock(ManagedObjectLock);

			for (const FPCGTaggedData& FData : InTaggedData)
			{
				if (UObject* InObject = const_cast<UPCGData*>(FData.Data.Get());
					IsValid(InObject))
				{
					if (ManagedObjects.Remove(InObject) == 0)
					{
						continue;
					}

					/*FCOLLECTOR_IMPL*/
					InObject->RemoveFromRoot();
					RecursivelyClearAsyncFlag_Unsafe(InObject);
					if (IPCGExManagedObjectInterface* ManagedObject = Cast<IPCGExManagedObjectInterface>(InObject))
					{
						ManagedObject->Cleanup();
					}
				}
			}
		}
	}

	void FManagedObjects::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
	{
		//This is causing a deadlock
	}

	void FManagedObjects::Destroy(UObject* InObject)
	{

		Remove(InObject);
	}

	void FManagedObjects::RecursivelyClearAsyncFlag_Unsafe(UObject* InObject) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FManagedObjects::RecursivelyClearAsyncFlag_Unsafe);

		{
			FReadScopeLock ReadScopeLock(DuplicatedObjectLock);
			if (DuplicateObjects.Contains(InObject))
			{
				return;
			}
		}

		if (InObject->HasAnyInternalFlags(EInternalObjectFlags::Async))
		{
			InObject->ClearInternalFlags(EInternalObjectFlags::Async);

			ForEachObjectWithOuter(InObject, [&](UObject* SubObject)
			{
				if (ManagedObjects.Contains(SubObject))
				{
					return;
				}
				SubObject->ClearInternalFlags(EInternalObjectFlags::Async);
			}, EGetObjectsFlags::IncludeNestedObjects);
		}
	}
}
