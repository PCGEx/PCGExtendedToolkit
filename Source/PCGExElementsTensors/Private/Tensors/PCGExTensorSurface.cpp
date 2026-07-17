// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorSurface.h"
#include "PCGExVersion.h"

#include "Components/PrimitiveComponent.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExTensorOperation.h"
#include "Data/PCGExData.h"
#include "Data/PCGSurfaceData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sampling/PCGExSamplingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateTensorSurface"
#define PCGEX_NAMESPACE CreateTensorSurface

namespace PCGExTensorSurface
{
	const FName SourceActorReferencesLabel = TEXT("Actor References");
	const FName SourcePCGSurfacesLabel = TEXT("Surfaces");
}

bool FPCGExTensorSurface::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}

	const UPCGExTensorSurfaceFactory* TypedFactory = Cast<UPCGExTensorSurfaceFactory>(InFactory);
	if (!TypedFactory)
	{
		return false;
	}

	World = TypedFactory->CachedWorld;
	CachedPrimitives = TypedFactory->CachedPrimitives;
	CachedSurfaces = TypedFactory->CachedSurfaces;

	bHasWorldCollision = TypedFactory->bHasWorldCollision;
	bHasPrimitives = TypedFactory->bHasPrimitives;
	bHasSurfaces = TypedFactory->bHasSurfaces;

	Config.CollisionSettings.Update(CollisionParams);

	return true;
}

PCGExTensor::FTensorSample FPCGExTensorSurface::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	FPCGExSurfaceHit Hit;

	if (!FindNearestSurface(InProbe.GetLocation(), Hit))
	{
		if (Config.bReturnZeroOnMiss)
		{
			// Return a zero-weight sample that won't affect results
			PCGExTensor::FEffectorSamples Samples;
			Samples.Emplace_GetRef(FVector::ZeroVector, 0, 0);
			return Samples.Flatten(0);
		}
		return PCGExTensor::FTensorSample(); // No surface found - sampling fails
	}

	// Compute direction based on mode
	const FVector Direction = ComputeDirection(InProbe, Hit);

	// Compute factor for falloff curves (0 = at surface, 1 = at max distance)
	const double Factor = FMath::Clamp(Hit.Distance / Config.MaxDistance, 0.0, 1.0);
	const double Potency = Config.PotencyValue.Constant * Config.PotencyScale * PotencyFalloffLUT->Eval(Factor);
	const double Weight = Config.WeightValue.Constant * WeightFalloffLUT->Eval(Factor);

	PCGExTensor::FEffectorSamples Samples;
	Samples.Emplace_GetRef(Direction, Potency, Weight);

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

bool FPCGExTensorSurface::FindNearestSurface(const FVector& Position, FPCGExSurfaceHit& OutHit) const
{
	OutHit = FPCGExSurfaceHit(); // Reset

	// Check all available sources and keep the closest hit
	if (bHasSurfaces)
	{
		CheckPCGSurfaces(Position, OutHit);
	}
	if (bHasPrimitives)
	{
		CheckPrimitives(Position, OutHit);
	}
	if (bHasWorldCollision)
	{
		CheckWorldCollision(Position, OutHit);
	}

	return OutHit.IsValid() && OutHit.Distance <= Config.MaxDistance;
}

void FPCGExTensorSurface::CheckWorldCollision(const FVector& Position, FPCGExSurfaceHit& OutHit) const
{
	if (!World.IsValid())
	{
		return;
	}

	const UWorld* WorldPtr = World.Get();
	const FCollisionShape CollisionShape = FCollisionShape::MakeSphere(Config.MaxDistance);

	TArray<FOverlapResult> OutOverlaps;
	bool bHasOverlaps = false;

	switch (Config.CollisionSettings.CollisionType)
	{
	case EPCGExCollisionFilterType::Channel:
		bHasOverlaps = WorldPtr->OverlapMultiByChannel(
			OutOverlaps, Position, FQuat::Identity,
			Config.CollisionSettings.CollisionChannel,
			CollisionShape, CollisionParams);
		break;

	case EPCGExCollisionFilterType::ObjectType:
		bHasOverlaps = WorldPtr->OverlapMultiByObjectType(
			OutOverlaps, Position, FQuat::Identity,
			FCollisionObjectQueryParams(Config.CollisionSettings.CollisionObjectType),
			CollisionShape, CollisionParams);
		break;

	case EPCGExCollisionFilterType::Profile:
		bHasOverlaps = WorldPtr->OverlapMultiByProfile(
			OutOverlaps, Position, FQuat::Identity,
			Config.CollisionSettings.CollisionProfileName,
			CollisionShape, CollisionParams);
		break;

	default:
		return;
	}

	if (!bHasOverlaps || OutOverlaps.IsEmpty())
	{
		return;
	}

	// Find closest surface point among all overlaps
	for (const FOverlapResult& Overlap : OutOverlaps)
	{
		if (!Overlap.Component.IsValid())
		{
			continue;
		}

		FVector ClosestPoint;
		const float Dist = Overlap.Component->GetClosestPointOnCollision(Position, ClosestPoint);

		if (Dist < 0)
		{
			continue;
		} // Invalid result

		// Get normal via line trace for accuracy
		FVector Normal = FVector::UpVector;
		if (Dist > KINDA_SMALL_NUMBER)
		{
			Normal = (Position - ClosestPoint).GetSafeNormal();

			// Try complex trace for better normal
			if (Config.CollisionSettings.bTraceComplex)
			{
				const FVector TraceDir = (ClosestPoint - Position).GetSafeNormal();
				FHitResult HitResult;
				FCollisionQueryParams TraceParams = CollisionParams;
				TraceParams.bTraceComplex = true;

				if (Overlap.Component->LineTraceComponent(
					HitResult,
					Position,
					ClosestPoint + TraceDir * 10.0,
					TraceParams))
				{
					Normal = HitResult.ImpactNormal;
					ClosestPoint = HitResult.Location;
				}
			}
		}

		OutHit.UpdateIfCloser(ClosestPoint, Normal, Dist);
	}
}

void FPCGExTensorSurface::CheckPrimitives(const FVector& Position, FPCGExSurfaceHit& OutHit) const
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrimitive : CachedPrimitives)
	{
		if (!WeakPrimitive.IsValid())
		{
			continue;
		}

		UPrimitiveComponent* Primitive = WeakPrimitive.Get();

		FVector ClosestPoint;
		const float Dist = Primitive->GetClosestPointOnCollision(Position, ClosestPoint);

		if (Dist < 0 || Dist > Config.MaxDistance)
		{
			continue;
		}

		// Get normal
		FVector Normal = FVector::UpVector;
		if (Dist > KINDA_SMALL_NUMBER)
		{
			Normal = (Position - ClosestPoint).GetSafeNormal();

			// Try complex trace for better normal
			if (Config.CollisionSettings.bTraceComplex)
			{
				const FVector TraceDir = (ClosestPoint - Position).GetSafeNormal();
				FHitResult HitResult;
				FCollisionQueryParams TraceParams = CollisionParams;
				TraceParams.bTraceComplex = true;

				if (Primitive->LineTraceComponent(
					HitResult,
					Position,
					ClosestPoint + TraceDir * 10.0,
					TraceParams))
				{
					Normal = HitResult.ImpactNormal;
					ClosestPoint = HitResult.Location;
				}
			}
		}

		OutHit.UpdateIfCloser(ClosestPoint, Normal, Dist);
	}
}

void FPCGExTensorSurface::CheckPCGSurfaces(const FVector& Position, FPCGExSurfaceHit& OutHit) const
{
	for (const TWeakObjectPtr<const UPCGSurfaceData>& WeakSurface : CachedSurfaces)
	{
		if (!WeakSurface.IsValid())
		{
			continue;
		}

		const UPCGSurfaceData* Surface = WeakSurface.Get();

		// Project point onto surface
		FPCGPoint ProjectedPoint;
		if (!Surface->ProjectPoint(FTransform(Position), FBox(FVector(-1), FVector(1)), ProjectionParams, ProjectedPoint, nullptr))
		{
			continue;
		}

		const FVector ProjectedLocation = ProjectedPoint.Transform.GetLocation();
		const double Dist = FVector::Dist(Position, ProjectedLocation);

		if (Dist > Config.MaxDistance)
		{
			continue;
		}

		// Estimate normal from nearby samples for landscapes
		FVector Normal = FVector::UpVector;
		const double SampleOffset = FMath::Max(10.0, Dist * 0.1);

		FPCGPoint SamplePX, SampleNX, SamplePY, SampleNY;
		bool bHasPX = Surface->ProjectPoint(FTransform(Position + FVector(SampleOffset, 0, 0)), FBox(FVector(-1), FVector(1)), ProjectionParams, SamplePX, nullptr);
		bool bHasNX = Surface->ProjectPoint(FTransform(Position - FVector(SampleOffset, 0, 0)), FBox(FVector(-1), FVector(1)), ProjectionParams, SampleNX, nullptr);
		bool bHasPY = Surface->ProjectPoint(FTransform(Position + FVector(0, SampleOffset, 0)), FBox(FVector(-1), FVector(1)), ProjectionParams, SamplePY, nullptr);
		bool bHasNY = Surface->ProjectPoint(FTransform(Position - FVector(0, SampleOffset, 0)), FBox(FVector(-1), FVector(1)), ProjectionParams, SampleNY, nullptr);

		// Compute normal from gradient
		if (bHasPX && bHasNX && bHasPY && bHasNY)
		{
			const FVector DX = SamplePX.Transform.GetLocation() - SampleNX.Transform.GetLocation();
			const FVector DY = SamplePY.Transform.GetLocation() - SampleNY.Transform.GetLocation();
			FVector ComputedNormal = FVector::CrossProduct(DX, DY).GetSafeNormal();

			// Ensure normal points upward (away from surface for landscapes)
			if (ComputedNormal.Z < 0)
			{
				ComputedNormal *= -1;
			}
			if (!ComputedNormal.IsNearlyZero())
			{
				Normal = ComputedNormal;
			}
		}
		else if (bHasPX && bHasPY)
		{
			// Fallback with fewer samples
			const FVector DX = SamplePX.Transform.GetLocation() - ProjectedLocation;
			const FVector DY = SamplePY.Transform.GetLocation() - ProjectedLocation;
			FVector ComputedNormal = FVector::CrossProduct(DX, DY).GetSafeNormal();
			if (ComputedNormal.Z < 0)
			{
				ComputedNormal *= -1;
			}
			if (!ComputedNormal.IsNearlyZero())
			{
				Normal = ComputedNormal;
			}
		}

		OutHit.UpdateIfCloser(ProjectedLocation, Normal, Dist);
	}
}

FVector FPCGExTensorSurface::ComputeDirection(const FTransform& InProbe, const FPCGExSurfaceHit& Hit) const
{
	const FVector ProbeLocation = InProbe.GetLocation();
	const FVector ToSurface = (Hit.Location - ProbeLocation).GetSafeNormal();

	switch (Config.Mode)
	{
	case EPCGExSurfaceTensorMode::AlongSurface:
	{
		// Project reference direction onto surface tangent plane
		const FVector RefDir = PCGExMath::GetDirection(InProbe.GetRotation(), Config.ReferenceAxis);

		// Project onto tangent plane (perpendicular to normal)
		FVector Projected = RefDir - (FVector::DotProduct(RefDir, Hit.Normal) * Hit.Normal);

		if (Projected.IsNearlyZero())
		{
			// Reference is parallel to normal - use any tangent direction
			// Find a vector perpendicular to the normal
			const FVector Arbitrary = FMath::Abs(Hit.Normal.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
			Projected = FVector::CrossProduct(Hit.Normal, Arbitrary);
		}

		return Projected.GetSafeNormal();
	}

	case EPCGExSurfaceTensorMode::TowardSurface:
		return ToSurface;

	case EPCGExSurfaceTensorMode::AwayFromSurface:
		if (Config.bUseNormalForAway)
		{
			return Hit.Normal;
		}
		return -ToSurface;

	case EPCGExSurfaceTensorMode::SurfaceNormal:
		return Hit.Normal;

	case EPCGExSurfaceTensorMode::Orbit:
	{
		// Orbit around surface - perpendicular to both normal and reference
		const FVector RefDir = PCGExMath::GetDirection(InProbe.GetRotation(), Config.ReferenceAxis);
		FVector OrbitDir = FVector::CrossProduct(Hit.Normal, RefDir);

		if (OrbitDir.IsNearlyZero())
		{
			// Reference is parallel to normal - use arbitrary perpendicular
			const FVector Arbitrary = FMath::Abs(Hit.Normal.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
			OrbitDir = FVector::CrossProduct(Hit.Normal, Arbitrary);
		}

		return OrbitDir.GetSafeNormal();
	}

	default:
		return ToSurface;
	}
}

// Factory implementation

PCGEX_TENSOR_BOILERPLATE(Surface, {}, {})

#if WITH_EDITOR
void UPCGExCreateTensorSurfaceSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateTensorSurfaceSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGExFactories::EPreparationResult UPCGExTensorSurfaceFactory::InitInternalData(FPCGExContext* InContext)
{
	PCGExFactories::EPreparationResult Result = Super::InitInternalData(InContext);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	// Cache world
	CachedWorld = InContext->GetWorld();
	if (!CachedWorld.IsValid())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Could not get World reference for Surface Tensor"));
		return PCGExFactories::EPreparationResult::Fail;
	}

	// Initialize collision settings
	Config.CollisionSettings.Init(InContext);

	// Check world collision availability
	bHasWorldCollision = Config.bUseWorldCollision;

	// Try to initialize actor references (optional input)
	bHasPrimitives = InitActorReferences(InContext);

	// Try to initialize PCG surfaces (optional input, used by default if available)
	bHasSurfaces = InitPCGSurfaces(InContext);

	// Must have at least one source
	if (!bHasWorldCollision && !bHasPrimitives && !bHasSurfaces)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Surface Tensor requires at least one surface source (enable World Collision or connect Surfaces/Actor References)"));
		return PCGExFactories::EPreparationResult::Fail;
	}

	return PCGExFactories::EPreparationResult::Success;
}

bool UPCGExTensorSurfaceFactory::InitActorReferences(FPCGExContext* InContext)
{
	TSharedPtr<PCGExData::FFacade> ActorRefFacade = PCGExData::TryGetSingleFacade(
		InContext, PCGExTensorSurface::SourceActorReferencesLabel, false, false);

	if (!ActorRefFacade)
	{
		// Actor references are optional
		return false;
	}

	TMap<AActor*, int32> IncludedActors;
	if (!PCGExSampling::Helpers::GetIncludedActors(InContext, ActorRefFacade.ToSharedRef(), Config.ActorReferenceAttribute, IncludedActors))
	{
		return false;
	}

	TSet<UPrimitiveComponent*> PrimitiveSet;
	for (const TPair<AActor*, int32>& Pair : IncludedActors)
	{
		if (!IsValid(Pair.Key))
		{
			continue;
		}

		TArray<UPrimitiveComponent*> FoundPrimitives;
		Pair.Key->GetComponents(UPrimitiveComponent::StaticClass(), FoundPrimitives);

		for (UPrimitiveComponent* Primitive : FoundPrimitives)
		{
			if (IsValid(Primitive))
			{
				PrimitiveSet.Add(Primitive);
			}
		}
	}

	if (PrimitiveSet.IsEmpty())
	{
		return false;
	}

	CachedPrimitives.Reserve(PrimitiveSet.Num());
	for (UPrimitiveComponent* Primitive : PrimitiveSet)
	{
		CachedPrimitives.Add(Primitive);
	}

	return true;
}

bool UPCGExTensorSurfaceFactory::InitPCGSurfaces(FPCGExContext* InContext)
{
	TArray<FPCGTaggedData> SurfaceInputs = InContext->InputData.GetInputsByPin(PCGExTensorSurface::SourcePCGSurfacesLabel);

	if (SurfaceInputs.IsEmpty())
	{
		// PCG surfaces are optional
		return false;
	}

	for (const FPCGTaggedData& TaggedData : SurfaceInputs)
	{
		if (const UPCGSurfaceData* SurfaceData = Cast<UPCGSurfaceData>(TaggedData.Data))
		{
			CachedSurfaces.Add(SurfaceData);
		}
	}

	return !CachedSurfaces.IsEmpty();
}

// Settings implementation

TArray<FPCGPinProperties> UPCGExCreateTensorSurfaceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	// Actor References pin (optional)
	PCGEX_PIN_POINT(PCGExTensorSurface::SourceActorReferencesLabel, "Points with actor reference paths (optional)", Normal)

	// PCG Surfaces pin (optional, default source)
	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGExTensorSurface::SourcePCGSurfacesLabel, FPCGDataTypeInfoSurface::AsId());
		PCGEX_PIN_TOOLTIP("PCG Surface data such as landscapes (optional, used by default if connected)")
		PCGEX_PIN_STATUS(Normal)
	}

	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
