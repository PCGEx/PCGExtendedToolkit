// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorDeltaExtensionRegistry.h"

#include "PCGExLog.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"

namespace PCGExActorDelta
{
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

		/** Sweeps every bucket: a collected class leaves its bucket keyed by a dangling address. */
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

			RemoveFromBuckets(Id);
			GetPendingIds().Remove(Id);
		}

		FCallbackScope::FCallbackScope()
		{
			++GetCallbackDepth();
		}

		FCallbackScope::~FCallbackScope()
		{
			if (--GetCallbackDepth() == 0 && !GetDeferredRemovals().IsEmpty())
			{
				for (const uint64 Id : TArray<uint64>(MoveTemp(GetDeferredRemovals())))
				{
					Unregister(Id);
				}
			}
		}

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

		const FDeltaExtension* FindById(const FName PayloadId, UClass*& OutClass)
		{
			const uint64* Id = GetIdIndex().Find(PayloadId);
			const FEntry* E = Id ? GetEntries().Find(*Id) : nullptr;
			OutClass = E ? E->Class.Get() : nullptr;
			return OutClass ? &E->Extension : nullptr;
		}

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
}
