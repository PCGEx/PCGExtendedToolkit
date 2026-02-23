// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

#include "PCGContext.h"
#include "Async/Async.h"

namespace PCGEx
{
	class PCGEXCORE_API FPCGExAsyncStateScope
	{
	public:
		explicit FPCGExAsyncStateScope(FPCGContext* InContext, const bool bDesired);
		~FPCGExAsyncStateScope();

	private:
		FPCGContext* Context = nullptr;
		bool bRestoreTo = false;
	};

	class PCGEXCORE_API FWorkHandle final : public TSharedFromThis<FWorkHandle>
	{
	public:
		FWorkHandle() = default;
		~FWorkHandle() = default;
	};

	class PCGEXCORE_API FManagedObjects : public TSharedFromThis<FManagedObjects>
	{
	protected:
		mutable FRWLock ManagedObjectLock;
		mutable FRWLock DuplicatedObjectLock;

	public:
		TWeakPtr<FWorkHandle> WorkHandle;
		TWeakPtr<FPCGContextHandle> WeakHandle;
		TSet<TObjectPtr<UObject>> ManagedObjects;

		bool IsFlushing() const { return bIsFlushing.load(std::memory_order_acquire); }

		explicit FManagedObjects(FPCGContext* InContext, const TWeakPtr<FWorkHandle>& InWorkHandle);

		~FManagedObjects();

		bool IsAvailable() const;

		void Flush();

		bool Add(UObject* InObject);
		bool Remove(UObject* InObject);
		void Remove(const TArray<FPCGTaggedData>& InTaggedData);

		void AddExtraStructReferencedObjects(FReferenceCollector& Collector);

		template <class T, typename... Args>
		T* New(Args&&... InArgs)
		{
			if (!WorkHandle.IsValid()) { return nullptr; }

			FPCGContext::FSharedContext<FPCGContext> SharedContext(WeakHandle);
			if (!SharedContext.Get()) { return nullptr; }

			T* Object = nullptr;
			if (!IsInGameThread())
			{
				{
					FGCScopeGuard Scope;
					Object = NewObject<T>(std::forward<Args>(InArgs)...);
				}
				check(Object);
			}
			else
			{
				Object = NewObject<T>(std::forward<Args>(InArgs)...);
			}

			Add(Object);
			return Object;
		}
		
		template <class T>
		T* DuplicateData(const UPCGData* InData)
		{
			if (!WorkHandle.IsValid()) { return nullptr; }

			FPCGContext::FSharedContext<FPCGContext> SharedContext(WeakHandle);
			if (!SharedContext.Get()) { return nullptr; }

			T* Object = nullptr;

			if (!IsInGameThread())
			{
				FWriteScopeLock WriteScopeLock(ManagedObjectLock);
				FPCGExAsyncStateScope ForceAsyncState(SharedContext.Get(), false);

				// Do the duplicate (uses AnyThread that requires bIsRunningOnMainThread to be up-to-date)
				Object = Cast<T>(InData->DuplicateData(SharedContext.Get(), true));

				check(Object);
				{
					FWriteScopeLock DupeLock(DuplicatedObjectLock);
					DuplicateObjects.Add(Object);
				}
			}
			else
			{
				FWriteScopeLock WriteScopeLock(ManagedObjectLock);
				Object = Cast<T>(InData->DuplicateData(SharedContext.Get(), true));
				check(Object);
				{
					FWriteScopeLock DupeLock(DuplicatedObjectLock);
					DuplicateObjects.Add(Object);
				}
			}

			Add(Object);
			return Object;
		}

		void Destroy(UObject* InObject);

	protected:
		TSet<UObject*> DuplicateObjects;
		void RecursivelyClearAsyncFlag_Unsafe(UObject* InObject) const;

	private:
		std::atomic<bool> bIsFlushing{false};
	};
}
