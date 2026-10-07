// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExChordBoxes.h"

#include "Data/PCGBasePointData.h"

#include "Core/PCGExMTCommon.h" // FScope, PCGEX_SCOPE_LOOP

namespace PCGExPaths::ChordBoxes
{
	void Miter(UPCGBasePointData* PointData, const int32 NumBoxes, const bool bClosedLoop, const double MiterLimit, const PCGExMT::FScope& Scope, const TConstArrayView<FBoxAxes> BoxAxes)
	{
		if (NumBoxes < 2 || NumBoxes > PointData->GetNumPoints()) { return; }
		if (!BoxAxes.IsEmpty() && BoxAxes.Num() < NumBoxes) { return; }

		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		TPCGValueRange<FVector> BoundsMin = PointData->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> BoundsMax = PointData->GetBoundsMaxValueRange(false);

		const FBoxAxes DefaultAxes;

		auto GetAxis = [](const FQuat& Rotation, const int32 Axis)
		{
			return Axis == 0 ? Rotation.GetAxisX() : Axis == 1 ? Rotation.GetAxisY() : Rotation.GetAxisZ();
		};

		// tan(Bend / 2) without trig: |Side| / (|(Forward, Side)| + Forward), capped at the miter limit.
		auto HalfBendTan = [MiterLimit](const double Forward, const double Side)
		{
			const double Planar = FMath::Sqrt(Forward * Forward + Side * Side);
			if (Planar <= UE_SMALL_NUMBER) { return 0.0; } // No bend in this plane

			const double Denominator = Planar + Forward;
			if (Denominator <= UE_SMALL_NUMBER) { return MiterLimit; } // Folding back on itself

			return FMath::Min(FMath::Abs(Side) / Denominator, MiterLimit);
		};

		// World extent on the outer side of the bend: heading toward +Axis (Toward > 0) opens the gap on -Axis.
		auto GetOuterExtent = [&](const int32 Box, const FVector& Scale, const int32 Axis, const double Toward)
		{
			const double Extent = Toward > 0 ? -BoundsMin[Box][Axis] : BoundsMax[Box][Axis];
			return FMath::Max(0.0, Extent * Scale[Axis]);
		};

		PCGEX_SCOPE_LOOP(Index)
		{
			if (Index >= NumBoxes) { break; }

			const int32 Next = Index + 1 < NumBoxes ? Index + 1 : (bClosedLoop ? 0 : INDEX_NONE);
			if (Next == INDEX_NONE) { continue; }

			const FBoxAxes& AxesA = BoxAxes.IsEmpty() ? DefaultAxes : BoxAxes[Index];
			const FBoxAxes& AxesB = BoxAxes.IsEmpty() ? DefaultAxes : BoxAxes[Next];
			if (!AxesA.IsValid() || !AxesB.IsValid()) { continue; }

			const FQuat RotationA = Transforms[Index].GetRotation();
			const FQuat RotationB = Transforms[Next].GetRotation();
			const FVector ScaleA = Transforms[Index].GetScale3D().GetAbs();
			const FVector ScaleB = Transforms[Next].GetScale3D().GetAbs();

			// Both chords pointing forward along the path.
			const FVector ForwardA = GetAxis(RotationA, AxesA.Primary) * (AxesA.bReversed ? -1.0 : 1.0);
			const FVector ForwardB = GetAxis(RotationB, AxesB.Primary) * (AxesB.bReversed ? -1.0 : 1.0);

			// The next chord, seen from the first box: its heading change turns sideways, its pitch bends vertically.
			const double Forward = ForwardB | ForwardA;
			const double Side = ForwardB | GetAxis(RotationA, AxesA.Lateral);
			const double Up = ForwardB | GetAxis(RotationA, AxesA.Normal);
			const double Horizontal = FMath::Sqrt(Forward * Forward + Side * Side);

			// Each box finds the outer side on its own axes (the second looks back at the first chord); the wider wins.
			const double LateralExtent = FMath::Max(
				GetOuterExtent(Index, ScaleA, AxesA.Lateral, Side),
				GetOuterExtent(Next, ScaleB, AxesB.Lateral, -(ForwardA | GetAxis(RotationB, AxesB.Lateral))));
			const double NormalExtent = FMath::Max(
				GetOuterExtent(Index, ScaleA, AxesA.Normal, Up),
				GetOuterExtent(Next, ScaleB, AxesB.Normal, -(ForwardA | GetAxis(RotationB, AxesB.Normal))));

			const double Extension = FMath::Max(LateralExtent * HalfBendTan(Forward, Side), NormalExtent * HalfBendTan(Horizontal, Up));
			if (Extension <= 0) { continue; }

			// The first box grows at its forward end, the second at its backward end, each in its own units.
			const double ExtensionA = Extension / FMath::Max(ScaleA[AxesA.Primary], UE_SMALL_NUMBER);
			const double ExtensionB = Extension / FMath::Max(ScaleB[AxesB.Primary], UE_SMALL_NUMBER);

			if (AxesA.bReversed) { BoundsMin[Index][AxesA.Primary] -= ExtensionA; }
			else { BoundsMax[Index][AxesA.Primary] += ExtensionA; }

			if (AxesB.bReversed) { BoundsMax[Next][AxesB.Primary] += ExtensionB; }
			else { BoundsMin[Next][AxesB.Primary] -= ExtensionB; }
		}
	}
}
