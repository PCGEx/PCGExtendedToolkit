// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Bake/PCGExSpatialBake.h"

#include "PCGContext.h"
#include "PCGData.h"
#include "PCGElement.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGVolumeData.h"
#include "GameFramework/Volume.h"
#include "PCGExCoreMacros.h"
#include "PhysicsEngine/BodySetup.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineStruct.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/Geo/PCGExGeo.h"
#include "Math/OBB/PCGExOBB.h"

#define LOCTEXT_NAMESPACE "PCGExSpatialBake"

FPCGExExternalDomainBakeSettings::FPCGExExternalDomainBakeSettings()
	: TagPrefix(TEXT("SpatialChannel"))
	  , FallbackChannel(TEXT("Default"))
	  , ZMin(NAME_None, /*DefaultValue=*/0.0)
	  , ZMax(NAME_None, /*DefaultValue=*/100.0)
{
}

namespace PCGExSpatial::Bake
{
	namespace Detail
	{
		// Bake one closed spline into a polygon shape entry. Returns false on
		// open splines, < 3 sampled vertices, or invalid Z band.
		//
		// Skips FPolyPath entirely -- we don't need its runtime spline / edge
		// octree. Direct path: sample world positions -> FPCGExGeo2DProjectionDetails
		// (Normal-from-data with FBestFitPlane fallback, or pure BestFit) ->
		// project to 2D outline. Mirrors FPolyPath::FPolyPath(UPCGSplineData*)
		// projection logic minus the unused spline/octree construction.
		static bool BakeSplineToPolygonEntry(
			const UPCGSplineData* SplineData,
			double Fidelity,
			FPCGExGeo2DProjectionDetails Projection,
			double InZMin, double InZMax,
			FPCGExSpatialPolygonEntry& OutEntry)
		{
			if (!SplineData)
			{
				return false;
			}
			if (!SplineData->IsClosed())
			{
				return false;
			}

			TArray<FVector> WorldPositions;
			SplineData->SplineStruct.ConvertSplineToPolyLine(
				ESplineCoordinateSpace::World, FMath::Square(Fidelity), WorldPositions);
			if (WorldPositions.Num() < 3)
			{
				return false;
			}

			const float ZLow = static_cast<float>(FMath::Min(InZMin, InZMax));
			const float ZHigh = static_cast<float>(FMath::Max(InZMin, InZMax));
			if (ZHigh <= ZLow)
			{
				return false;
			}

			// Projection setup mirrors FPolyPath ctor for splines:
			//   - Non-Normal method: compute FBestFitPlane from sampled positions.
			//   - Normal method:     try data-driven init (spline's transform);
			//                        fall back to FBestFitPlane on failure.
			if (Projection.Method != EPCGExProjectionMethod::Normal)
			{
				Projection.Init(PCGExMath::FBestFitPlane(MakeArrayView(WorldPositions)));
			}
			else if (!Projection.Init(SplineData))
			{
				Projection.Init(PCGExMath::FBestFitPlane(MakeArrayView(WorldPositions)));
			}

			TArray<FVector2D> Outline;
			Projection.Project(MakeArrayView(WorldPositions), Outline);
			if (Outline.Num() < 3)
			{
				return false;
			}

			// WorldOrigin stays at zero: the projection is rotation-only and
			// the outline coords are already in world-XY-rotated-into-frame
			// space. At QueryPoint time, Entry.ProjectionQuat.UnrotateVector(P)
			// reproduces the same projection without an origin shift.
			OutEntry.Outline = MoveTemp(Outline);
			OutEntry.WorldOrigin = FVector::ZeroVector;
			OutEntry.ProjectionQuat = Projection.ProjectionQuat;
			OutEntry.ZMin = ZLow;
			OutEntry.ZMax = ZHigh;
			OutEntry.WorldAABB = PCGExMath::Geo::ProjectPrismToWorldAABB(
				OutEntry.Outline, OutEntry.ZMin, OutEntry.ZMax,
				OutEntry.WorldOrigin, OutEntry.ProjectionQuat);
			return OutEntry.IsValid();
		}

		// Build one OBB per point. Scale handling mirrors
		// FPCGExValencyGrowthOperation::ComputeWorldBounds: world origin =
		// transform location + rotation * (local center * scale); extents =
		// local extents * |scale|. Factory::FromTransform doesn't apply scale,
		// so we do it explicitly.
		static int32 BakePointsToOBBs(
			const UPCGBasePointData* PointData,
			TArray<FBakedEntry>& OutEntries,
			FName ChannelKey)
		{
			if (!PointData)
			{
				return 0;
			}
			const int32 NumPoints = PointData->GetNumPoints();
			if (NumPoints <= 0)
			{
				return 0;
			}
			if (ChannelKey.IsNone())
			{
				return 0;
			}

			const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
			const TConstPCGValueRange<FVector> BoundsMin = PointData->GetConstBoundsMinValueRange();
			const TConstPCGValueRange<FVector> BoundsMax = PointData->GetConstBoundsMaxValueRange();

			int32 Emitted = 0;
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const FVector& BMin = BoundsMin[i];
				const FVector& BMax = BoundsMax[i];
				const FVector LocalCenter = (BMin + BMax) * 0.5;
				const FVector LocalExtents = (BMax - BMin) * 0.5;
				if (LocalExtents.GetMin() <= 0.0)
				{
					continue;
				}

				const FTransform& Xform = Transforms[i];
				const FVector Scale = Xform.GetScale3D();
				const FVector AbsScale = Scale.GetAbs();
				const FVector ScaledCenter = LocalCenter * Scale;
				const FVector ScaledExtents = LocalExtents * AbsScale;
				if (ScaledExtents.GetMin() <= 0.0)
				{
					continue;
				}

				const FQuat Rotation = Xform.GetRotation();
				const FVector WorldOrigin = Xform.GetLocation() + Rotation.RotateVector(ScaledCenter);

				const PCGExMath::OBB::FOBB OBB(
					PCGExMath::OBB::FBounds(WorldOrigin, ScaledExtents, INDEX_NONE),
					PCGExMath::OBB::FOrientation(Rotation));

				FBakedEntry& Out = OutEntries.Emplace_GetRef();
				Out.ChannelKey = ChannelKey;
				Out.Shape.InitializeAs<FPCGExFootprintShape_OBB>(OBB);
				++Emitted;
			}
			return Emitted;
		}

		// Fallback when a UObject-backed input cannot answer an overlap query: an
		// approximate box beats a region that reports "no overlap" forever.
		bool EmitBoundsOBB(const FBox& WorldBounds, FName ChannelKey, TArray<FBakedEntry>& OutEntries)
		{
			FBakedEntry& Out = OutEntries.Emplace_GetRef();
			Out.ChannelKey = ChannelKey;
			Out.Shape.InitializeAs<FPCGExFootprintShape_OBB>(
				PCGExMath::OBB::Factory::FromAABB(WorldBounds, INDEX_NONE));
			return true;
		}
	}

	FName ResolveChannelKey(
		const FPCGTaggedData& InData,
		const FPCGExExternalDomainBakeSettings& InSettings)
	{
		// PCGExData::FTags parses tags shaped "Key:Value" into ValueTags on
		// construction. For "<TagPrefix>:<ChannelName>" we look up the prefix
		// and read the FString value. Fallback when missing keeps untagged
		// inputs flowing through to the default channel rather than dropping.
		const PCGExData::FTags ParsedTags(InData.Tags);
		const FString Channel = ParsedTags.GetValue<FString>(InSettings.TagPrefix.ToString(), FString());
		if (Channel.IsEmpty())
		{
			return InSettings.FallbackChannel;
		}
		return FName(*Channel);
	}

	bool TryBake(
		const FPCGTaggedData& InData,
		FPCGExContext* InContext,
		const FPCGExExternalDomainBakeSettings& InSettings,
		TArray<FBakedEntry>& OutEntries)
	{
		const UPCGData* RawData = InData.Data.Get();
		if (!RawData)
		{
			return false;
		}

		const FName ChannelKey = ResolveChannelKey(InData, InSettings);
		if (ChannelKey.IsNone())
		{
			return false;
		}

		// Per-input-type dispatch. Adding a new input kind = one branch here.
		// Promote to TMap<UClass*, FBakeFn> once we have >= 3 kinds.

		if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(RawData))
		{
			// Z band reads may consult @Data-domain attributes on this input.
			double ZMinVal = 0.0;
			double ZMaxVal = 0.0;
			const bool bZMinOk = InSettings.ZMin.TryReadDataValue(InContext, RawData, ZMinVal, /*bQuiet=*/true);
			const bool bZMaxOk = InSettings.ZMax.TryReadDataValue(InContext, RawData, ZMaxVal, /*bQuiet=*/true);
			if (!bZMinOk || !bZMaxOk)
			{
				return false;
			}

			FPCGExSpatialPolygonEntry Entry;
			if (!Detail::BakeSplineToPolygonEntry(
				SplineData, InSettings.SplineFidelity, InSettings.Projection, ZMinVal, ZMaxVal, Entry))
			{
				return false;
			}

			FBakedEntry& Out = OutEntries.Emplace_GetRef();
			Out.ChannelKey = ChannelKey;
			Out.Shape.InitializeAs<FPCGExFootprintShape_Polygon>(Entry);
			return true;
		}

		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(RawData))
		{
			return Detail::BakePointsToOBBs(PointData, OutEntries, ChannelKey) > 0;
		}

		// Nothing roots the actor -- UPCGVolumeData holds it weakly too -- so the
		// shape stays weak and every pair test re-resolves it; a dead actor drops out.
		if (const UPCGVolumeData* VolumeData = Cast<UPCGVolumeData>(RawData))
		{
			const FBox WorldBounds = VolumeData->GetBounds();
			if (!WorldBounds.IsValid)
			{
				return false;
			}

			// bThreadsafeTest: off the game thread the no-arg IsValid() reports false
			// for a live actor during a GC mark phase.
			const TWeakObjectPtr<AVolume> Actor = VolumeData->GetVolumeActor();
			const AVolume* Volume = Actor.IsValid(/*bEvenIfPendingKill*/false, /*bThreadsafeTest*/true)
				                        ? Actor.Get()
				                        : nullptr;
			const UBrushComponent* Brush = Volume ? Volume->GetBrushComponent() : nullptr;

			// Bounds-only volume data (Get Actor Data on a partitioned component, HiGen
			// cells, runtime-gen Self, WorldVolumetric) has no actor; its box is exact.
			if (!Brush)
			{
				return Detail::EmitBoundsOBB(WorldBounds, ChannelKey, OutEntries);
			}

			// A NoCollision brush has no physics actor, so every overlap query answers
			// false. Stock Post Process / NavMesh Bounds / Audio / Level Streaming
			// volumes all ship that way -- common, not exotic.
			if (Brush->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				PCGE_LOG_C(
					Warning, GraphAndLog, InContext,
					FTEXT("Volume input has collision disabled on its brush, so it cannot answer overlap queries. Falling back to its bounding box -- enable collision on the volume for an exact region."));
				return Detail::EmitBoundsOBB(WorldBounds, ChannelKey, OutEntries);
			}

			FBakedEntry& Out = OutEntries.Emplace_GetRef();
			Out.ChannelKey = ChannelKey;
			Out.Shape.InitializeAs<FPCGExFootprintShape_Volume>(Actor, WorldBounds);
			return true;
		}

		if (const UPCGPrimitiveData* PrimitiveData = Cast<UPCGPrimitiveData>(RawData))
		{
			const TWeakObjectPtr<UPrimitiveComponent> CompPtr = PrimitiveData->GetComponent();
			UPrimitiveComponent* Comp = CompPtr.IsValid(/*bEvenIfPendingKill*/false, /*bThreadsafeTest*/true)
				                            ? CompPtr.Get()
				                            : nullptr;
			if (!Comp)
			{
				return false;
			}

			// GetBounds() is RENDER bounds but the pair test asks COLLISION geometry,
			// and the broadphase AABB gate is a hard reject -- union the collision AABB
			// in so it can never cull a candidate the narrow phase would accept.
			UBodySetup* BodySetup = Comp->GetBodySetup();
			const bool bHasSimpleCollision = BodySetup && BodySetup->AggGeom.GetElementCount() > 0;

			FBox WorldBounds = PrimitiveData->GetBounds();
			if (bHasSimpleCollision)
			{
				const FBox CollisionAABB = BodySetup->AggGeom.CalcAABB(Comp->GetComponentTransform());
				if (CollisionAABB.IsValid)
				{
					WorldBounds = WorldBounds.IsValid ? (WorldBounds + CollisionAABB) : CollisionAABB;
				}
			}
			if (!WorldBounds.IsValid)
			{
				return false;
			}

			// OverlapComponent queries simple collision only, so a complex-only mesh or
			// a disabled component answers false everywhere.
			if (!bHasSimpleCollision || Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				PCGE_LOG_C(
					Warning, GraphAndLog, InContext,
					FTEXT("Primitive input has no queryable simple collision, so it cannot answer overlap queries. Falling back to its bounding box -- add simple collision (or set Use Complex As Simple) for an exact region."));
				return Detail::EmitBoundsOBB(WorldBounds, ChannelKey, OutEntries);
			}

			FBakedEntry& Out = OutEntries.Emplace_GetRef();
			Out.ChannelKey = ChannelKey;
			Out.Shape.InitializeAs<FPCGExFootprintShape_Primitive>(CompPtr, WorldBounds);
			return true;
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
