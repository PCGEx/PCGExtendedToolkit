// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExStructLayoutStamp.h"
#include "Metadata/PCGObjectPropertyOverride.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

class UPCGData;
struct FPCGContext;

namespace PCGExActorDelta
{
	class FScopedActorWrite;
}

namespace PCGExActorOverrides
{
	/** What one override description resolves to on a given actor class. */
	struct FResolvedTarget
	{
		/** Index of the description this was resolved from */
		int32 Description = INDEX_NONE;

		/** The properties the engine walks from the actor down to the written one, array inner properties included */
		TArray<const FProperty*> Chain;

		/** The written property holds a hard object reference, which the engine loads synchronously as it writes */
		bool bHoldsObjectReference = false;

		/** The written property is fed a soft path, which is what the engine turns a string source into */
		bool bTakesPath = false;

		/** The write lands in AActor's own members, which its components read their visibility and collision from */
		bool bActorState = false;

		// One warning per class and target, whatever the number of inputs and actors.
		bool bUnreachableReported = false;
		bool bOversizedReported = false;
		bool bFailureReported = false;
	};

	/** The targets one actor class has, out of a node's override descriptions. */
	struct FClassTargets
	{
		TArray<FResolvedTarget> Targets;

		/** Every struct the targets were looked up on: Chain is only good while this is current */
		PCGExHelpers::FStructLayoutStamp Layout;
	};

	/**
	 * A node's override descriptions, resolved per actor class. Nothing here depends on an input, so the inputs of
	 * a node share one: each class is resolved, and its issues reported, once. Game thread only.
	 */
	class PCGEXCOLLECTIONS_API FOverrideTargets
	{
	public:
		/** InDescriptions is copied: the settings it comes from can be edited while the node is still spawning. */
		FOverrideTargets(FPCGContext* InContext, const TArray<FPCGObjectPropertyOverrideDescription>& InDescriptions, bool bInQuietMissingTargets);

		FOverrideTargets(const FOverrideTargets&) = delete;
		FOverrideTargets& operator=(const FOverrideTargets&) = delete;

		FPCGContext* GetContext() const { return Context; }
		const TArray<FPCGObjectPropertyOverrideDescription>& GetDescriptions() const { return Descriptions; }

		/** Whether a target a class doesn't have, or an actor can't reach, goes unreported */
		bool IsQuietAboutMissingTargets() const { return bQuietMissingTargets; }

		/** The targets InClass has. Resolved again once the class, or a struct a target goes through, got recompiled. */
		TSharedRef<FClassTargets> Resolve(const UClass* InClass);

	private:
		FPCGContext* Context = nullptr;
		TArray<FPCGObjectPropertyOverrideDescription> Descriptions;
		bool bQuietMissingTargets = false;

		/** Weak keys only guard against a collected class; a recompiled one keeps its identity, see FClassTargets::Layout */
		TMap<TWeakObjectPtr<const UClass>, TSharedRef<FClassTargets>> ClassTargets;
	};

	/**
	 * Per-point property overrides for the actors spawned from one input, whose class can differ per point.
	 * Drives the engine's FPCGObjectSingleOverride with what live, mixed-class actors need on top: a target a class
	 * lacks or an actor can't reach is skipped, written components re-register, values to preload are found.
	 * Game thread only, except HasPreloadableSources and GatherPreloadPaths.
	 */
	class PCGEXCOLLECTIONS_API FActorPropertyOverrides
	{
	public:
		/** InSourceData is referenced, not copied: it must outlive this object. */
		FActorPropertyOverrides(const TSharedRef<FOverrideTargets>& InTargets, const UPCGData* InSourceData);
		~FActorPropertyOverrides();

		FActorPropertyOverrides(const FActorPropertyOverrides&) = delete;
		FActorPropertyOverrides& operator=(const FActorPropertyOverrides&) = delete;

		/** Whether any source can be read as an asset path. When false no class ever has anything to preload. */
		bool HasPreloadableSources() const { return bHasPreloadableSources; }

		/** Binds the overrides to InClass and lists the sources (description indices) whose values must be loaded
		 *  before Apply writes them: the ones the engine accepted for a target holding a hard object reference. */
		void PrepareClass(const UClass* InClass, TArray<int32>& OutPreloadSources);

		/** Adds the asset paths InSource holds over the points of [InStart, InStart + InCount) that InFilter accepts. */
		void GatherPreloadPaths(int32 InSource, int32 InStart, int32 InCount, TFunctionRef<bool(int32)> InFilter, TSet<FSoftObjectPath>& OutPaths) const;

		/** Writes the values read at InPointIndex onto InScope's actor. Returns whether anything got written. */
		bool Apply(PCGExActorDelta::FScopedActorWrite& InScope, int32 InPointIndex);

	private:
		struct FBoundOverride
		{
			FPCGObjectSingleOverride Override;

			/** Index in FClassTargets::Targets */
			int32 Target = INDEX_NONE;
		};

		/** The overrides the engine accepted for one class, reading from this input */
		struct FBoundClass
		{
			explicit FBoundClass(const TSharedRef<FClassTargets>& InTargets)
				: Targets(InTargets)
			{
			}

			TSharedRef<FClassTargets> Targets;
			TArray<FBoundOverride> Overrides;
		};

		struct FSourceReader
		{
			TUniquePtr<const IPCGAttributeAccessor> Accessor;
			TUniquePtr<const IPCGAttributeAccessorKeys> Keys;
			bool bIsString = false;
			bool bIsArray = false;
		};

		FBoundClass& FindOrBind(const UClass* InClass);
		bool HasOversizedString(int32 InSource, int32 InPointIndex) const;

		TSharedRef<FOverrideTargets> Targets;
		const UPCGData* SourceData = nullptr;
		bool bHasPreloadableSources = false;

		TMap<TWeakObjectPtr<const UClass>, TUniquePtr<FBoundClass>> BoundClasses;

		/** One slot per description, only set for the sources that can be read as an asset path */
		TArray<FSourceReader> SourceReaders;
	};
}
