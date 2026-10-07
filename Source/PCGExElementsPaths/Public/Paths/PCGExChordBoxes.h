// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class UPCGBasePointData;

namespace PCGExMT
{
	struct FScope;
}

/** Chord boxes: points whose box runs along the chord to the next point (Get Path Data samples, Path Solidify). */
namespace PCGExPaths::ChordBoxes
{
	/** A chord box's local axes (0 = X, 1 = Y, 2 = Z). Primary runs along the chord, backward when bReversed.
	 *  A negative Primary marks a point that isn't a box: its joints are left alone. */
	struct PCGEXELEMENTSPATHS_API FBoxAxes
	{
		int8 Primary = 0;
		int8 Lateral = 1;
		int8 Normal = 2;
		bool bReversed = false;

		bool IsValid() const { return Primary >= 0; }
	};

	/**
	 * Miters the joint after each box in Scope: both boxes extend along their chord by Extent * tan(Bend / 2), capped
	 * at MiterLimit * Extent. Boxes are points [0, NumBoxes): a closed loop wraps its last joint, an open path keeps
	 * its ends. An empty BoxAxes means X along the chord, Y sideways, Z up.
	 * A joint only writes the forward end of its first box and the backward end of its second, so disjoint scopes
	 * can run concurrently.
	 */
	PCGEXELEMENTSPATHS_API void Miter(UPCGBasePointData* PointData, int32 NumBoxes, bool bClosedLoop, double MiterLimit, const PCGExMT::FScope& Scope, TConstArrayView<FBoxAxes> BoxAxes = {});
}
