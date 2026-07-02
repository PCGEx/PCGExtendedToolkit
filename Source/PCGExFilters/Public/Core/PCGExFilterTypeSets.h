// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"

#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExClusterStates.h"

// Acceptance sets used to filter incoming factories on a pin (replaces the legacy
// PCGExFactories::EType sets). They live here, in PCGExFilters, because they reference
// FPCGDataTypeInfo structs that belong to this module and are invisible to PCGExCore.
//
// Membership is FLAT/EXACT: a factory is accepted when its GetDataTypeId() is contained
// in the set. We intentionally do NOT use FPCGDataTypeIdentifier compositions here -
// those auto-reduce a parent + child down to the parent, which would silently widen the
// "point but not its cluster sub-filters" sets into "any filter". A plain TSet of base
// ids preserves the legacy semantics exactly.
//
// Accessors are lazy (function-local statics) so they are built on first use, after the
// reflection system is up - never during cross-module static initialization.

namespace PCGExFactories
{
	// All filter kinds (point, vtx/node, edge, group, collection).
	inline const TSet<FPCGDataTypeBaseId>& AnyFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterPoint::AsId(),
			FPCGExDataTypeInfoFilterVtx::AsId(),
			FPCGExDataTypeInfoFilterEdge::AsId(),
			FPCGExDataTypeInfoFilter::AsId(), // filter group reports the base Filter id
			FPCGExDataTypeInfoFilterCollection::AsId()
		};
		return Set;
	}

	// Point-context filters: point, group, collection - NOT cluster vtx/edge.
	inline const TSet<FPCGDataTypeBaseId>& PointFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterPoint::AsId(),
			FPCGExDataTypeInfoFilter::AsId(),
			FPCGExDataTypeInfoFilterCollection::AsId()
		};
		return Set;
	}

	// Cluster node (vtx) context: point, vtx, group, collection - NOT edge.
	inline const TSet<FPCGDataTypeBaseId>& ClusterNodeFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterPoint::AsId(),
			FPCGExDataTypeInfoFilterVtx::AsId(),
			FPCGExDataTypeInfoFilter::AsId(),
			FPCGExDataTypeInfoFilterCollection::AsId()
		};
		return Set;
	}

	// Cluster edge context: point, edge, group, collection - NOT vtx.
	inline const TSet<FPCGDataTypeBaseId>& ClusterEdgeFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterPoint::AsId(),
			FPCGExDataTypeInfoFilterEdge::AsId(),
			FPCGExDataTypeInfoFilter::AsId(),
			FPCGExDataTypeInfoFilterCollection::AsId()
		};
		return Set;
	}

	// Filters that operate on cluster data (vtx, edge, cluster state) plus group & collection.
	inline const TSet<FPCGDataTypeBaseId>& SupportsClusterFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterEdge::AsId(),
			FPCGExDataTypeInfoFilterVtx::AsId(),
			FPCGExDataTypeInfoClusterState::AsId(),
			FPCGExDataTypeInfoFilter::AsId(),
			FPCGExDataTypeInfoFilterCollection::AsId()
		};
		return Set;
	}

	// Cluster-only filters: vtx, edge, cluster state.
	inline const TSet<FPCGDataTypeBaseId>& ClusterOnlyFilters()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoFilterEdge::AsId(),
			FPCGExDataTypeInfoFilterVtx::AsId(),
			FPCGExDataTypeInfoClusterState::AsId()
		};
		return Set;
	}

	// State factories (point + cluster).
	inline const TSet<FPCGDataTypeBaseId>& ClusterStates()
	{
		static const TSet<FPCGDataTypeBaseId> Set = {
			FPCGExDataTypeInfoClusterState::AsId(),
			FPCGExDataTypeInfoPointState::AsId()
		};
		return Set;
	}
}
