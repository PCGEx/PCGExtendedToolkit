// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExStagingBoundsModifier.h"

#pragma region FPCGExStagingBoundsModifier

FBox FPCGExStagingBoundsModifier::ComputeAlteredBounds(const FBox& InBounds) const
{
	// Nothing meaningful to modify if the source box was never computed; consumers gate on the
	// original Bounds.IsValid, so mirror it rather than fabricate a box from zero corners.
	if (!InBounds.IsValid)
	{
		return InBounds;
	}

	const FBox Result = Modify(InBounds);

	// FBox(Min, Max) always flags IsValid=1, so the inversion test -- not the flag -- is what
	// guards a degenerate transform. Reject inverted boxes (negative size on any axis) and fall
	// back to the original bounds. Planar boxes (zero size on an axis) are valid fitting inputs.
	const FVector Size = Result.Max - Result.Min;
	if (!Result.IsValid || Size.X < 0.0 || Size.Y < 0.0 || Size.Z < 0.0)
	{
		return InBounds;
	}

	return Result;
}

FBox FPCGExStagingBoundsModifier::Modify(const FBox& InBounds) const
{
	return InBounds;
}

#pragma endregion

#pragma region FPCGExStagingBoundsModifierOffset

FBox FPCGExStagingBoundsModifierOffset::Modify(const FBox& InBounds) const
{
	return FBox(InBounds.Min + OffsetMin, InBounds.Max + OffsetMax);
}

#pragma endregion

#pragma region FPCGExStagingBoundsModifierScale

FBox FPCGExStagingBoundsModifierScale::Modify(const FBox& InBounds) const
{
	const FVector Center = InBounds.GetCenter();
	const FVector Extent = InBounds.GetExtent() * Scale;
	return FBox(Center - Extent, Center + Extent);
}

#pragma endregion

#pragma region FPCGExStagingBoundsModifierPad

FBox FPCGExStagingBoundsModifierPad::Modify(const FBox& InBounds) const
{
	return InBounds.ExpandBy(Amount);
}

#pragma endregion
