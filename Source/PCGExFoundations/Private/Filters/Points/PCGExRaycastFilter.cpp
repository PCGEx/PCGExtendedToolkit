// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExRaycastFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/Debug/PCGExDrawAttributes.h"
#include "Engine/HitResult.h"
#include "Sampling/PCGExSamplingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExRaycastFilterDefinition"
#define PCGEX_NAMESPACE PCGExRaycastFilterDefinition

#pragma region UPCGExRaycastFilterFactory

bool UPCGExRaycastFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}
	Config.Sanitize();
	Config.CollisionSettings.Init(InContext);

	bUseInclude = Config.SurfaceSource == EPCGExSurfaceSource::ActorReferences;
	if (bUseInclude)
	{
		{
			if (!PCGExMetaHelpers::IsWritableAttributeName(Config.ActorReference))
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Actor Reference, Config.ActorReference)
				return false;
			}

			InContext->AddConsumableAttributeName(Config.ActorReference);
		}

		const TSharedPtr<PCGExData::FFacade> ActorReferenceDataFacade = PCGExData::TryGetSingleFacade(InContext, PCGExRaycastFilter::SourceActorReferencesLabel, false, true);
		if (!ActorReferenceDataFacade)
		{
			return false;
		}

		if (!PCGExSampling::Helpers::GetIncludedActors(InContext, ActorReferenceDataFacade.ToSharedRef(), Config.ActorReference, IncludedActors))
		{
			return false;
		}
	}

	return true;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExRaycastFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FRaycastFilter>(this);
}

void UPCGExRaycastFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	if (Config.OriginMode != EPCGExRaycastOriginMode::PointPosition)
	{
		Config.Origin.RegisterBufferDependencies(InContext, FacadePreloader);
	}
	Config.Direction.RegisterBufferDependencies(InContext, FacadePreloader);
	Config.MaxDistance.RegisterBufferDependencies(InContext, FacadePreloader);

	if (Config.CollisionSettings.TraceMode == EPCGExTraceMode::Sphere)
	{
		Config.CollisionSettings.SphereRadius.RegisterBufferDependencies(InContext, FacadePreloader);
	}
	else if (Config.CollisionSettings.TraceMode == EPCGExTraceMode::Box)
	{
		Config.CollisionSettings.BoxHalfExtents.RegisterBufferDependencies(InContext, FacadePreloader);
	}

	if (Config.TestMode == EPCGExRaycastTestMode::CompareDistance)
	{
		Config.DistanceThreshold.RegisterBufferDependencies(InContext, FacadePreloader);
	}
}

#pragma endregion

#pragma region FRaycastFilter

bool PCGExPointFilter::FRaycastFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	const FPCGExRaycastFilterConfig& Config = TypedFilterFactory->Config;

	CollisionSettings = Config.CollisionSettings;

	if (Config.OriginMode != EPCGExRaycastOriginMode::PointPosition)
	{
		OriginGetter = Config.Origin.GetValueSetting();
		OriginGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!OriginGetter->Init(InPointDataFacade))
		{
			return false;
		}
	}

	DirectionGetter = Config.Direction.GetValueSetting();
	DirectionGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!DirectionGetter->Init(InPointDataFacade))
	{
		return false;
	}

	MaxDistanceGetter = Config.MaxDistance.GetValueSetting();
	MaxDistanceGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!MaxDistanceGetter->Init(InPointDataFacade))
	{
		return false;
	}

	if (CollisionSettings.TraceMode == EPCGExTraceMode::Sphere)
	{
		SphereRadiusGetter = CollisionSettings.SphereRadius.GetValueSetting();
		SphereRadiusGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!SphereRadiusGetter->Init(InPointDataFacade))
		{
			return false;
		}
	}
	else if (CollisionSettings.TraceMode == EPCGExTraceMode::Box)
	{
		BoxHalfExtentsGetter = CollisionSettings.BoxHalfExtents.GetValueSetting();
		BoxHalfExtentsGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!BoxHalfExtentsGetter->Init(InPointDataFacade))
		{
			return false;
		}
	}

	if (Config.TestMode == EPCGExRaycastTestMode::CompareDistance)
	{
		DistanceThresholdGetter = Config.DistanceThreshold.GetValueSetting();
		DistanceThresholdGetter->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!DistanceThresholdGetter->Init(InPointDataFacade))
		{
			return false;
		}
	}

	InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

bool PCGExPointFilter::FRaycastFilter::DoTrace(const FVector& Start, const FVector& End, const FQuat& Orientation, const int32 Index, FHitResult& OutHit) const
{
	switch (CollisionSettings.TraceMode)
	{
	case EPCGExTraceMode::Line:
		return CollisionSettings.Linecast(Start, End, OutHit);

	case EPCGExTraceMode::Sphere:
		return CollisionSettings.SphereSweep(Start, End, SphereRadiusGetter->Read(Index), OutHit, Orientation);

	case EPCGExTraceMode::Box:
		return CollisionSettings.BoxSweep(Start, End, BoxHalfExtentsGetter->Read(Index), OutHit, Orientation);
	}

	return false;
}

bool PCGExPointFilter::FRaycastFilter::DoTraceMulti(const FVector& Start, const FVector& End, const FQuat& Orientation, const int32 Index, FHitResult& OutHit) const
{
	const TMap<AActor*, int32>& IncludedActors = TypedFilterFactory->IncludedActors;

	TArray<FHitResult> HitResults;
	bool bHit = false;

	switch (CollisionSettings.TraceMode)
	{
	case EPCGExTraceMode::Line:
		bHit = CollisionSettings.LinecastMulti(Start, End, HitResults);
		break;

	case EPCGExTraceMode::Sphere:
		bHit = CollisionSettings.SphereSweepMulti(Start, End, SphereRadiusGetter->Read(Index), HitResults, Orientation);
		break;

	case EPCGExTraceMode::Box:
		bHit = CollisionSettings.BoxSweepMulti(Start, End, BoxHalfExtentsGetter->Read(Index), HitResults, Orientation);
		break;
	}

	if (!bHit || HitResults.IsEmpty())
	{
		return false;
	}

	// Find the first hit that matches an included actor
	for (const FHitResult& Hit : HitResults)
	{
		if (IncludedActors.Contains(Hit.GetActor()))
		{
			OutHit = Hit;
			return true;
		}
	}

	return false;
}

bool PCGExPointFilter::FRaycastFilter::Test(const int32 PointIndex) const
{
	const FPCGExRaycastFilterConfig& Config = TypedFilterFactory->Config;

	const FTransform& Transform = InTransforms[PointIndex];
	const FVector PointPosition = Transform.GetLocation();

	FVector Start;
	switch (Config.OriginMode)
	{
	default:
	case EPCGExRaycastOriginMode::PointPosition:
		Start = PointPosition;
		break;
	case EPCGExRaycastOriginMode::OffsetWorld:
		Start = PointPosition + OriginGetter->Read(PointIndex);
		break;
	case EPCGExRaycastOriginMode::OffsetRelative:
		Start = PointPosition + Transform.TransformVector(OriginGetter->Read(PointIndex));
		break;
	case EPCGExRaycastOriginMode::WorldPosition:
		Start = OriginGetter->Read(PointIndex);
		break;
	}

	FVector Direction = DirectionGetter->Read(PointIndex);
	if (Config.Direction.bFlip)
	{
		Direction *= -1;
	}
	if (Config.bTransformDirection)
	{
		Direction = Transform.TransformVectorNoScale(Direction);
	}
	Direction = Direction.GetSafeNormal();

	const double MaxDistance = MaxDistanceGetter->Read(PointIndex);
	const FVector End = Start + Direction * MaxDistance;
	const FQuat Orientation = Config.bTransformDirection ? Transform.GetRotation() : FQuat::Identity;

	FHitResult HitResult;
	bool bHit = false;

	if (TypedFilterFactory->bUseInclude)
	{
		bHit = DoTraceMulti(Start, End, Orientation, PointIndex, HitResult);
	}
	else
	{
		bHit = DoTrace(Start, End, Orientation, PointIndex, HitResult);
	}

	// Handle result based on test mode
	if (Config.TestMode == EPCGExRaycastTestMode::AnyHit)
	{
		// AnyHit mode: hit = pass, no hit = fail, then apply invert
		return Config.bInvert ? !bHit : bHit;
	}

	// CompareDistance mode
	if (!bHit)
	{
		// No hit - return fallback (NOT affected by bInvert)
		return Config.NoHitFallback == EPCGExFilterFallback::Pass;
	}

	const double HitDistance = (HitResult.ImpactPoint - Start).Size();
	const double Threshold = DistanceThresholdGetter->Read(PointIndex);

	bool bResult = PCGExCompare::Compare(Config.Comparison, HitDistance, Threshold, Config.Tolerance);

	// Apply inversion to actual comparison result only
	return Config.bInvert ? !bResult : bResult;
}

#pragma endregion

#pragma region UPCGExRaycastFilterProviderSettings

TArray<FPCGPinProperties> UPCGExRaycastFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	if (Config.SurfaceSource == EPCGExSurfaceSource::ActorReferences)
	{
		PCGEX_PIN_POINT(PCGExRaycastFilter::SourceActorReferencesLabel, "Points with actor reference paths.", Required)
	}

	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Raycast)

#if WITH_EDITOR
FString UPCGExRaycastFilterProviderSettings::GetDisplayName() const
{
	FString TraceModeStr;
	switch (Config.CollisionSettings.TraceMode)
	{
	case EPCGExTraceMode::Line:
		TraceModeStr = TEXT("Line");
		break;
	case EPCGExTraceMode::Sphere:
		TraceModeStr = TEXT("Sphere");
		break;
	case EPCGExTraceMode::Box:
		TraceModeStr = TEXT("Box");
		break;
	}

	FString TestModeStr;
	switch (Config.TestMode)
	{
	case EPCGExRaycastTestMode::AnyHit:
		TestModeStr = TEXT("Any Hit");
		break;
	case EPCGExRaycastTestMode::CompareDistance:
		TestModeStr = PCGExCompare::ToString(Config.Comparison);
		break;
	}

	return FString::Printf(TEXT("%s, %s"), *TraceModeStr, *TestModeStr);
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
