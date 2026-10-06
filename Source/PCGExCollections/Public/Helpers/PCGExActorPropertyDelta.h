// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "UObject/SoftObjectPath.h"

class AActor;
class FComponentReregisterContext;
class UActorComponent;

namespace PCGExActorDelta
{
	/**
	 * Brackets raw property writes on a live actor, so that several writers share one refresh.
	 * A component reported through Touch stays unregistered until the scope closes. Closing re-registers those,
	 * registers the components created meanwhile, then runs whichever of the actor refresh and the post-apply
	 * fixups a writer asked for. Game thread only.
	 */
	class PCGEXCOLLECTIONS_API FScopedActorWrite
	{
	public:
		explicit FScopedActorWrite(AActor* InActor);
		~FScopedActorWrite();

		FScopedActorWrite(const FScopedActorWrite&) = delete;
		FScopedActorWrite& operator=(const FScopedActorWrite&) = delete;

		AActor* GetActor() const { return Actor; }

		/** Call before writing into InComponent. Does nothing when it is not registered, which covers a second call. */
		void Touch(UActorComponent* InComponent);

		/** Registers a component created inside the scope when it closes, after the touched ones are back. */
		void RegisterOnClose(UActorComponent* InComponent) { NewComponents.Add(InComponent); }

		/** For a write into AActor's own members: when the scope closes, the components get told what the actor's
		 *  setters would have told them (render state, collision). */
		void RequestActorRefresh() { bRefreshActor = true; }

		/** Runs the post-apply fixups on the actor when the scope closes. */
		void RequestFixups() { bRunFixups = true; }

	private:
		AActor* Actor = nullptr;
		TArray<TUniquePtr<FComponentReregisterContext>> Contexts;
		TArray<UActorComponent*> NewComponents;
		bool bRefreshActor = false;
		bool bRunFixups = false;
	};

	/**
	 * Serialize properties that differ from defaults for an actor AND its components.
	 * Actor-level properties are diffed against the actor CDO.
	 * Each instanced component is diffed against its archetype.
	 * Returns empty array if actor and all components match defaults exactly.
	 *
	 * Format is opaque -- use ApplyPropertyDelta to deserialize.
	 *
	 * @param Actor             Source actor to capture.
	 * @param OutCollaterals    Optional. When provided, asset soft paths the delta
	 *                          references are APPENDED (deduplicated). Caller is
	 *                          responsible for clearing the array between reuse.
	 *                          Pass nullptr to skip collection.
	 */
	PCGEXCOLLECTIONS_API TArray<uint8> SerializeActorDelta(AActor* Actor, TArray<FSoftObjectPath>* OutCollaterals = nullptr);

	/**
	 * Apply a previously serialized property delta to an actor and its components.
	 * Components are matched by name. Missing/renamed components are safely skipped.
	 *
	 * After delta bytes are applied, any post-apply fixups registered via
	 * RegisterPostApplyFixup() are invoked on matching components.
	 */
	PCGEXCOLLECTIONS_API void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes);

	/**
	 * Same, as one writer among others on InScope's actor: the components it writes into re-register, and the
	 * fixups run, when the scope closes rather than as it goes.
	 */
	PCGEXCOLLECTIONS_API void ApplyPropertyDelta(FScopedActorWrite& InScope, const TArray<uint8>& DeltaBytes);

	/** Compute CRC32 hash of delta bytes. Returns 0 for empty input. */
	PCGEXCOLLECTIONS_API uint32 HashDelta(const TArray<uint8>& DeltaBytes);

	/**
	 * Callback invoked on each component of the target actor after deltas are applied.
	 *
	 * Fixups exist to repair engine-managed invariants that a property delta cannot
	 * express: aliased EditAnywhere fields the engine expects to stay consistent
	 * (e.g. USplineComponent's SplineCurves and Spline in UE 5.7+), transient caches
	 * that must be rebuilt (reparam tables, bounds), component-type-specific regen
	 * triggers after state is restored, etc.
	 *
	 * A fixup fires for every component whose class IS or DERIVES FROM the registered
	 * class. Fixups run regardless of whether the component had delta bytes applied --
	 * the same inconsistencies can arise from archetype cloning alone.
	 *
	 * Archetype may be null when a component has no per-actor archetype (engine-managed
	 * components). Fixups must tolerate that case.
	 *
	 * Typical registration from an IModuleInterface subclass; the handle's RAII
	 * unregister fires when the owning module shuts down:
	 *
	 *    FPostApplyFixupHandle MyFixupHandle;
	 *
	 *    virtual void StartupModule() override
	 *    {
	 *        MyFixupHandle = PCGExActorDelta::RegisterPostApplyFixup(
	 *            UMyComponent::StaticClass(),
	 *            [](UActorComponent* C, UObject*) { ... });
	 *    }
	 *
	 * Use the FSoftClassPath overload when you don't want a hard link to the target
	 * component's module -- resolution is retried lazily, so the fixup stays dormant
	 * in projects where the target class never loads:
	 *
	 *    MyFixupHandle = PCGExActorDelta::RegisterPostApplyFixup(
	 *        FSoftClassPath(TEXT("/Script/OtherPlugin.UFooComponent")),
	 *        [](UActorComponent* C, UObject*) { ... });
	 */
	using FPostApplyFixup = TFunction<void(UActorComponent* Component, UObject* Archetype)>;

	/**
	 * RAII handle returned by RegisterPostApplyFixup. Destruction unregisters the fixup;
	 * reassignment unregisters the previous registration first. Move-only.
	 *
	 * Hold as a member of a long-lived object (typically an IModuleInterface subclass)
	 * so the registration's lifetime tracks the module's. A handle destroyed early
	 * silently unregisters the fixup; a handle that outlives the registry (i.e. the
	 * core module) is a contract violation the consumer is responsible for avoiding.
	 */
	class PCGEXCOLLECTIONS_API FPostApplyFixupHandle
	{
	public:
		FPostApplyFixupHandle() = default;
		~FPostApplyFixupHandle();

		FPostApplyFixupHandle(FPostApplyFixupHandle&& Other) noexcept;
		FPostApplyFixupHandle& operator=(FPostApplyFixupHandle&& Other) noexcept;

		FPostApplyFixupHandle(const FPostApplyFixupHandle&) = delete;
		FPostApplyFixupHandle& operator=(const FPostApplyFixupHandle&) = delete;

		bool IsValid() const
		{
			return Id != 0;
		}

		/** Unregister early (before destruction). Safe to call multiple times. */
		void Reset();

	private:
		uint64 Id = 0;

		friend class FPostApplyFixupHandleFactory;
	};

	/**
	 * Register a fixup for components whose class is or derives from ComponentClass.
	 * Returns a handle; hold it for the duration the fixup should remain active.
	 * Dropping the handle unregisters the fixup.
	 */
	[[nodiscard]] PCGEXCOLLECTIONS_API FPostApplyFixupHandle RegisterPostApplyFixup(
		UClass* ComponentClass, FPostApplyFixup Fixup);

	/**
	 * Register a fixup against a soft class path. Resolution is lazy: if the target
	 * class isn't loaded at registration time, resolution is retried on each apply
	 * invocation until successful. Use this form when the consuming module must not
	 * hard-link against the target module -- the fixup simply never fires in projects
	 * where the target class never loads.
	 */
	[[nodiscard]] PCGEXCOLLECTIONS_API FPostApplyFixupHandle RegisterPostApplyFixup(
		const FSoftClassPath& ClassPath, FPostApplyFixup Fixup);
}
