// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/TopLevelAssetPath.h"

#include "PCGExSplineSampleHandler.generated.h"

class UActorComponent;

namespace PCGExSplineSampling
{
	/** Node fallbacks, in world units from the centerline, for whatever a spline type doesn't store. */
	struct PCGEXELEMENTSPATHS_API FParams
	{
		double HalfWidth = 30;
		double Above = 30;
		double Below = 0;

		/** Extend to soft edges where the type has them (landscape spline falloff). */
		bool bIncludeFalloff = false;
	};

	/** One evaluated position, in world space. Extents are distances from the centerline (>= 0). */
	struct PCGEXELEMENTSPATHS_API FStation
	{
		FVector Position = FVector::ZeroVector;
		FVector Up = FVector::UpVector; // Unit
		double Left = 0;
		double Right = 0;
		double Above = 0;
		double Below = 0;
	};

	/** A snapshot of one sampleable run = one output path. Built on the game thread, then prepared and evaluated on
	 *  workers: it must not touch UObjects past creation. */
	class PCGEXELEMENTSPATHS_API FSource
	{
	public:
		virtual ~FSource() = default;

		/** World-space arc length. Must be valid once Prepare has run. */
		double Length = 0;
		bool bClosedLoop = false;

		/** False keeps the whole source on one worker. */
		bool bConcurrentEval = true;

		/** Worker thread, once, before Length is read: the place for heavy derived work. */
		virtual void Prepare() {}

		/** Fills OutStations[i] for Distances[i], which ascend within [0, Length]; a closed source is never asked for
		 *  Length. Worker threads, concurrently on disjoint ranges when bConcurrentEval. */
		virtual void Evaluate(TConstArrayView<double> Distances, TArrayView<FStation> OutStations) const = 0;
	};
}

/** Turns one kind of spline component into sample sources for Get Path Data's Sample In Place.
 *  Registered by class and run as the CDO: handlers are stateless, per-component state lives in their sources. */
UCLASS(Abstract)
class PCGEXELEMENTSPATHS_API UPCGExSplineSampleHandler : public UObject
{
	GENERATED_BODY()

public:
	/** Component class sampled; subclasses match. A path, so classes from modules PCGEx doesn't link can be named. */
	virtual FTopLevelAssetPath GetComponentClassPath() const PURE_VIRTUAL(UPCGExSplineSampleHandler::GetComponentClassPath, return FTopLevelAssetPath(););

	/** Game thread. One source per output path; keep it to copying, heavy work belongs in FSource::Prepare. */
	virtual void CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const PURE_VIRTUAL(UPCGExSplineSampleHandler::CreateSources,);
};

namespace PCGExSplineSampling
{
	/** Handler classes by component class path: the nearest registered class wins, and the latest registration
	 *  overrides until unregistered. Register from StartupModule, never a static initializer (the handler CDO is
	 *  read), and unregister from ShutdownModule: the registry outlives the module. Native classes only. */
	class PCGEXELEMENTSPATHS_API FHandlerRegistry
	{
	public:
		static FHandlerRegistry& Get();

		/** False when HandlerClass is abstract, not native, names no component class path, or is already registered. */
		bool Register(TSubclassOf<UPCGExSplineSampleHandler> HandlerClass);

		/** Only compares the pointer, so it is safe during shutdown. */
		void Unregister(const UClass* HandlerClass);

		/** Handler registered for ComponentClass or its nearest registered super class; null when none. */
		const UPCGExSplineSampleHandler* Find(const UClass* ComponentClass) const;

	private:
		FHandlerRegistry() = default;

		// Raw pointers: any TSubclassOf read calls IsChildOf, and the classes are already purged when ShutdownModule unregisters.
		using FHandlerStack = TArray<const UClass*, TInlineAllocator<1>>;

		mutable FRWLock Lock;
		TMap<FTopLevelAssetPath, FHandlerStack> Registrations;
	};
}
