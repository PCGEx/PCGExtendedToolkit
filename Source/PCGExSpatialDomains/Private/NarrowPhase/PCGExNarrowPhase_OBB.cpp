// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/OBB/PCGExOBB.h"
#include "Math/OBB/PCGExOBBTests.h"
#include "NarrowPhase/PCGExNarrowPhase.h"
#include "NarrowPhase/PCGExNarrowPhaseRegistrations.h"
#include "Shapes/PCGExFootprintShape.h"

namespace PCGExSpatial::NarrowPhase
{
	namespace OBBPair
	{
		/**
		 * OBB-vs-OBB precise overlap test. Sphere pre-cull + SAT (the
		 * EPCGExBoxCheckMode::Box path inside PCGExMath::OBB::TestOverlap).
		 */
		bool OBBvsOBB_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& OBB_A = static_cast<const FPCGExFootprintShape_OBB&>(A);
			const auto& OBB_B = static_cast<const FPCGExFootprintShape_OBB&>(B);
			return PCGExMath::OBB::TestOverlap(OBB_A.Bounds, OBB_B.Bounds, EPCGExBoxCheckMode::Box);
		}

		/**
		 * OBB-vs-OBB SAT-MTV penetration depth. Returns the magnitude of the
		 * minimum translation vector required to separate the two boxes (zero
		 * when not overlapping).
		 */
		float OBBvsOBB_Penetration(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& OBB_A = static_cast<const FPCGExFootprintShape_OBB&>(A);
			const auto& OBB_B = static_cast<const FPCGExFootprintShape_OBB&>(B);
			return PCGExMath::OBB::SATPenetrationDepth(OBB_A.Bounds, OBB_B.Bounds);
		}

		float OBB_QueryPoint(const FVector& Point, const FPCGExFootprintShape& Stored)
		{
			return PCGExMath::OBB::SignedDistance(static_cast<const FPCGExFootprintShape_OBB&>(Stored).Bounds, Point);
		}
	}

	void RegisterOBBPairTests()
	{
		Register(
			FPCGExFootprintShape_OBB::StaticStruct(),
			FPCGExFootprintShape_OBB::StaticStruct(),
			{&OBBPair::OBBvsOBB_Overlap, &OBBPair::OBBvsOBB_Penetration});

		RegisterQueryPoint(
			FPCGExFootprintShape_OBB::StaticStruct(),
			&OBBPair::OBB_QueryPoint);
	}
}
