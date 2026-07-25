// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Math/OBB/PCGExOBB.h"

#include "PCGExFootprintShape.generated.h"

class AVolume;
class UPrimitiveComponent;

/**
 * Extruded-prism polygon entry. Outline lives in projection-frame XY; the
 * Z band is along the frame's normal. World placement = WorldOrigin
 * (translation) + ProjectionQuat (world->frame). To test a world point P:
 *   LocalP = ProjectionQuat.UnrotateVector(P - WorldOrigin); 2D test on Outline; Z vs band.
 *
 * Pure data -- UPROPERTY-less inner fields keep the struct trivially copyable
 * at the CPU level. Stored inside FPCGExFootprintShape_Polygon at runtime;
 * never serialized through reflection (the placed-modules tracker is rebuilt
 * per growth run).
 */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExSpatialPolygonEntry
{
	GENERATED_BODY()

	/** 2D outline in projection-frame XY. Concave allowed; winding-agnostic. */
	TArray<FVector2D> Outline;

	FVector WorldOrigin = FVector::ZeroVector;
	FQuat ProjectionQuat = FQuat::Identity;

	float ZMin = 0.0f;
	float ZMax = 0.0f;

	/** Placement-instance identity. Reserved INDEX_NONE = skip-by-owner sentinel. */
	int32 OwnerIndex = INDEX_NONE;

	/** Cached at append time for the cheap-reject pre-filter tier. */
	FBox WorldAABB = FBox(ForceInit);

	FORCEINLINE bool IsValid() const
	{
		return Outline.Num() >= 3 && ZMax > ZMin;
	}
};

/**
 * Polymorphic world-space shape descriptor. Carried by reference at the
 * Domain.Append / Domain.Overlaps boundary; copied into TInstancedStruct
 * storage when the broadphase domain accepts it.
 *
 * The base is abstract: subclasses MUST override GetScriptStruct() (returns
 * their StaticStruct, used as the type-keyed registry lookup key) and
 * GetWorldAABB() (broadphase pre-cull AABB, always implementable for any shape).
 *
 * Adding a new shape kind is a pure addition: declare a USTRUCT subclass,
 * implement the two virtuals, register pair tests via
 * PCGExSpatial::NarrowPhase::Register from the shape's owning module's
 * StartupModule. Existing shapes / domains / placement code never change.
 *
 * Value semantics are the design: a shape's bytes fully describe it, so pair
 * tests stay pure math -- deterministic, lock-free, snapshot-safe. _Volume /
 * _Primitive deliberately break that by holding a live UObject. Roadmap: bake
 * UBodySetup::AggGeom at TryBake time and make live-physics shapes an opt-in.
 */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExFootprintShape
{
	GENERATED_BODY()

	// Out-of-line virtual destructor anchors the vtable in the cpp. UE pattern
	// for _API-marked polymorphic USTRUCTs -- inline `= default` risks no-emit
	// when consuming modules need to reference the destructor symbol (LNK2019).
	virtual ~FPCGExFootprintShape();

	/**
	 * Return this instance's reflection identity. Subclasses override with
	 * `return StaticStruct();`. Used by the narrow-phase registry to resolve
	 * pair tests.
	 */
	virtual UScriptStruct* GetScriptStruct() const
	{
		return nullptr;
	}

	/**
	 * World-space AABB. Must be implementable for any shape -- the broadphase
	 * indexes entries by AABB and never inspects shape-specific fields.
	 */
	virtual FBox GetWorldAABB() const
	{
		return FBox(ForceInit);
	}
};

/** Oriented-box shape -- the default contribution kind for cage modules. */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExFootprintShape_OBB : public FPCGExFootprintShape
{
	GENERATED_BODY()

	PCGExMath::OBB::FOBB Bounds;

	// Out-of-line ctors/dtor: see base-class comment for rationale.
	FPCGExFootprintShape_OBB();
	explicit FPCGExFootprintShape_OBB(const PCGExMath::OBB::FOBB& InBounds);
	virtual ~FPCGExFootprintShape_OBB() override;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FBox GetWorldAABB() const override;
};

/** Extruded-prism polygon shape -- concave allowed, projection-frame aware. */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExFootprintShape_Polygon : public FPCGExFootprintShape
{
	GENERATED_BODY()

	FPCGExSpatialPolygonEntry Entry;

	FPCGExFootprintShape_Polygon();
	explicit FPCGExFootprintShape_Polygon(FPCGExSpatialPolygonEntry InEntry);
	virtual ~FPCGExFootprintShape_Polygon() override;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FBox GetWorldAABB() const override
	{
		return Entry.WorldAABB;
	}
};

/**
 * Occupancy region backed by a level AVolume brush, overlap-tested through its
 * brush component's collision. Weak pointer, never GC-reflected -- these shapes
 * are transient, rebuilt per run, matching the _Polygon convention.
 *
 * No QueryPoint, and none can be written honestly from this handle:
 * EncompassesPoint's out-distance is UNSIGNED (0 inside, -1 on failure), so it
 * cannot express the depth the union-min CSG semantics need.
 */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExFootprintShape_Volume : public FPCGExFootprintShape
{
	GENERATED_BODY()

	TWeakObjectPtr<AVolume> Volume;

	/** Snapshotted at bake so broadphase culling never dereferences the weak pointer. */
	FBox WorldAABB = FBox(ForceInit);

	FPCGExFootprintShape_Volume();
	FPCGExFootprintShape_Volume(const TWeakObjectPtr<AVolume>& InVolume, const FBox& InWorldAABB);
	virtual ~FPCGExFootprintShape_Volume() override;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FBox GetWorldAABB() const override
	{
		return WorldAABB;
	}
};

/**
 * Occupancy region backed by a UPrimitiveComponent. Same transient convention as
 * the Volume shape, and no QueryPoint for the same reason -- OverlapComponent is
 * boolean, with no distance field.
 *
 * WorldAABB unions render bounds with the simple-collision AABB: the broadphase
 * gate is a hard reject, and collision can reach outside render bounds.
 */
USTRUCT()
struct PCGEXSPATIALDOMAINS_API FPCGExFootprintShape_Primitive : public FPCGExFootprintShape
{
	GENERATED_BODY()

	TWeakObjectPtr<UPrimitiveComponent> Primitive;

	/** Snapshotted at bake so broadphase culling never dereferences the weak pointer. */
	FBox WorldAABB = FBox(ForceInit);

	FPCGExFootprintShape_Primitive();
	FPCGExFootprintShape_Primitive(const TWeakObjectPtr<UPrimitiveComponent>& InPrimitive, const FBox& InWorldAABB);
	virtual ~FPCGExFootprintShape_Primitive() override;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FBox GetWorldAABB() const override
	{
		return WorldAABB;
	}
};
