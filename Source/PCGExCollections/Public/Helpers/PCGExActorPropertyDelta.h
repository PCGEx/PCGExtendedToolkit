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
	 * Subobjects the instance owns but its defaults lack are recorded with their class and name, to be recreated on apply.
	 * Matching delta extensions append their payloads (see FDeltaExtension).
	 * Returns empty array if actor and all components match defaults exactly. Game thread only.
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
	 * Runs inside its own FScopedActorWrite: written components re-register, then the post-apply fixups run,
	 * once the whole delta is in. Game thread only.
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
	 * Runs on each component whose class is or derives from the registered one, once the whole delta is applied,
	 * whether or not that component had bytes of its own. Repairs engine invariants a property delta cannot express
	 * (aliased fields, transient caches). Archetype is null for components without a per-actor archetype.
	 */
	using FPostApplyFixup = TFunction<void(UActorComponent* Component, UObject* Archetype)>;

	/**
	 * Writes the extension's payload for Source. Return false to write none. Ar is the delta writer: FNames and object
	 * references serialize session-portable, as they do in the property delta.
	 */
	using FDeltaCapture = TFunction<bool(const UObject& Source, FArchive& Ar)>;

	/**
	 * Reads a payload Capture wrote at PayloadVersion. Ar holds that payload only, so an over-read sets Ar.IsError()
	 * without reaching the rest of the delta: parse it whole before mutating Target. A component Target has already
	 * been touched on InScope.
	 */
	using FDeltaApply = TFunction<void(UObject& Target, FArchive& Ar, uint32 PayloadVersion, FScopedActorWrite& InScope)>;

	/**
	 * Class-specific state the generic property delta cannot carry: data that lives in a custom Serialize, or
	 * derived state that must be rebuilt. Registered against an actor or component class, it matches that class and
	 * every class deriving from it. Every member is optional.
	 *
	 * Capture runs at SerializeActorDelta and Apply at ApplyPropertyDelta, after the property deltas and before the
	 * scope closes. PostApply runs on components only, when the scope closes. ClaimedProperties are left out of the
	 * generic delta of the matching classes, with any subobject they hold, because the extension owns them.
	 * A callback cannot register an extension; one it unregisters goes once the callback returns.
	 */
	struct FDeltaExtension
	{
		/** Wire identity of the payload, unique among extensions that Capture or Apply. Convention: "Plugin.Name". */
		FName Id;

		/** Written with each payload; a payload newer than the registered Version is skipped. */
		uint32 Version = 1;

		FDeltaCapture Capture;
		FDeltaApply Apply;
		FPostApplyFixup PostApply;
		TArray<FName> ClaimedProperties;
	};

	/**
	 * RAII handle of a registration. Destruction unregisters it; reassignment unregisters the previous one first.
	 * Move-only. Hold it on a long-lived object (typically the registering module); a handle that outlives this
	 * module is a contract violation.
	 */
	class PCGEXCOLLECTIONS_API FDeltaExtensionHandle
	{
	public:
		FDeltaExtensionHandle() = default;
		~FDeltaExtensionHandle();

		FDeltaExtensionHandle(FDeltaExtensionHandle&& Other) noexcept;
		FDeltaExtensionHandle& operator=(FDeltaExtensionHandle&& Other) noexcept;

		FDeltaExtensionHandle(const FDeltaExtensionHandle&) = delete;
		FDeltaExtensionHandle& operator=(const FDeltaExtensionHandle&) = delete;

		bool IsValid() const
		{
			return Id != 0;
		}

		/** Unregister early. Safe to call multiple times. */
		void Reset();

	private:
		uint64 Id = 0;

		friend class FDeltaExtensionHandleFactory;
	};

	using FPostApplyFixupHandle = FDeltaExtensionHandle;

	/**
	 * Register an extension for Class and its subclasses. Returns an invalid handle when the extension is empty, has
	 * Capture or Apply without an Id, or reuses the Id of a live registration. Game thread only.
	 */
	[[nodiscard]] PCGEXCOLLECTIONS_API FDeltaExtensionHandle RegisterDeltaExtension(UClass* Class, FDeltaExtension Extension);

	/**
	 * Same, against a soft class path resolved lazily on each capture and apply, so the registering module needs no link
	 * to the target's: the extension stays dormant in projects where that class never loads, and resolves again after
	 * its class is reloaded.
	 */
	[[nodiscard]] PCGEXCOLLECTIONS_API FDeltaExtensionHandle RegisterDeltaExtension(const FSoftClassPath& ClassPath, FDeltaExtension Extension);

	/** A PostApply-only extension on ComponentClass. */
	[[nodiscard]] PCGEXCOLLECTIONS_API FPostApplyFixupHandle RegisterPostApplyFixup(
		UClass* ComponentClass, FPostApplyFixup Fixup);

	/** A PostApply-only extension on a soft component class path. */
	[[nodiscard]] PCGEXCOLLECTIONS_API FPostApplyFixupHandle RegisterPostApplyFixup(
		const FSoftClassPath& ClassPath, FPostApplyFixup Fixup);
}
