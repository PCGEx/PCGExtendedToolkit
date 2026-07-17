// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExSubdivisionDetails.h"

#include "Details/PCGExSettingsDetails.h"
#include "Types/PCGExTypes.h"


bool FPCGExManhattanDetails::IsValid() const
{
	return bInitialized;
}

bool FPCGExManhattanDetails::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (bSupportAttribute)
	{
		GridSizeBuffer = GridSizeValue.GetValueSetting();
		if (!GridSizeBuffer->Init(InDataFacade))
		{
			return false;
		}

		if (SpaceAlign == EPCGExManhattanAlign::Custom)
		{
			OrientBuffer = OrientValue.GetValueSetting();
		}
		else if (SpaceAlign == EPCGExManhattanAlign::World)
		{
			OrientBuffer = PCGExDetails::MakeSettingValue(FRotator::ZeroRotator);
		}

		if (OrientBuffer && !OrientBuffer->Init(InDataFacade))
		{
			return false;
		}
	}
	else
	{
		// Attribute support disabled for this embedder: force the constant path regardless of the shorthand's Input toggle.
		GridSizeValue.Constant = PCGExTypes::Abs(GridSizeValue.Constant);
		GridSizeBuffer = PCGExDetails::MakeSettingValue(GridSizeValue.Constant);
		if (SpaceAlign == EPCGExManhattanAlign::Custom)
		{
			OrientBuffer = PCGExDetails::MakeSettingValue(OrientValue.Constant);
		}
		else if (SpaceAlign == EPCGExManhattanAlign::World)
		{
			OrientBuffer = PCGExDetails::MakeSettingValue(FRotator::ZeroRotator);
		}
	}

	PCGExMath::GetAxesOrder(Order, Comps);

	bInitialized = true;
	return true;
}

int32 FPCGExManhattanDetails::ComputeSubdivisions(const FVector& A, const FVector& B, const int32 Index, TArray<FVector>& OutSubdivisions, double& OutDist) const
{
	FVector DirectionAndSize = B - A;
	const int32 StartIndex = OutSubdivisions.Num();

	FQuat Rotation = FQuat::Identity;

	switch (SpaceAlign)
	{
	case EPCGExManhattanAlign::World:
	case EPCGExManhattanAlign::Custom:
		Rotation = OrientBuffer->Read(Index).Quaternion();
		break;
	case EPCGExManhattanAlign::SegmentX:
		Rotation = FRotationMatrix::MakeFromX(DirectionAndSize).ToQuat();
		break;
	case EPCGExManhattanAlign::SegmentY:
		Rotation = FRotationMatrix::MakeFromY(DirectionAndSize).ToQuat();
		break;
	case EPCGExManhattanAlign::SegmentZ:
		Rotation = FRotationMatrix::MakeFromZ(DirectionAndSize).ToQuat();
		break;
	}

	DirectionAndSize = Rotation.RotateVector(DirectionAndSize);

	if (Method == EPCGExManhattanMethod::Simple)
	{
		OutSubdivisions.Reserve(OutSubdivisions.Num() + 3);

		FVector Sub = FVector::ZeroVector;
		for (int i = 0; i < 3; ++i)
		{
			const int32 Axis = Comps[i];
			const double Dist = DirectionAndSize[Axis];

			if (FMath::IsNearlyZero(Dist))
			{
				continue;
			}

			OutDist += Dist;
			Sub[Axis] = Dist;

			if (Sub == B)
			{
				break;
			}

			OutSubdivisions.Emplace(Sub);
		}
	}
	else
	{
		FVector Subdivs = PCGExTypes::Abs(GridSizeBuffer->Read(Index));
		FVector Maxes = PCGExTypes::Abs(DirectionAndSize);
		if (Method == EPCGExManhattanMethod::GridCount)
		{
			Subdivs = FVector(FMath::Floor(Maxes.X / Subdivs.X), FMath::Floor(Maxes.Y / Subdivs.Y), FMath::Floor(Maxes.Z / Subdivs.Z));
		}

		const FVector StepSize = FVector::Min(Subdivs, Maxes);
		const FVector Sign = FVector(FMath::Sign(DirectionAndSize.X), FMath::Sign(DirectionAndSize.Y), FMath::Sign(DirectionAndSize.Z));

		FVector Sub = FVector::ZeroVector;

		bool bAdvance = true;
		while (bAdvance)
		{
			double DistBefore = OutDist;
			for (int i = 0; i < 3; ++i)
			{
				const int32 Axis = Comps[i];
				double Dist = StepSize[Axis];

				if (const double SubAbs = FMath::Abs(Sub[Axis]);
					SubAbs + Dist > Maxes[Axis])
				{
					Dist = Maxes[Axis] - SubAbs;
				}
				if (FMath::IsNearlyZero(Dist))
				{
					continue;
				}

				OutDist += Dist;
				Sub[Axis] += Dist * Sign[Axis];

				if (Sub == B)
				{
					bAdvance = false;
					break;
				}

				OutSubdivisions.Emplace(Sub);
			}

			if (DistBefore == OutDist)
			{
				bAdvance = false;
			}
		}
	}

	for (int i = StartIndex; i < OutSubdivisions.Num(); i++)
	{
		OutSubdivisions[i] = A + Rotation.UnrotateVector(OutSubdivisions[i]);
	}

	return OutSubdivisions.Num() - StartIndex;
}

#if WITH_EDITOR
void FPCGExManhattanDetails::ApplyDeprecation()
{
	GridSizeValue.Update(GridSizeInput_DEPRECATED, GridSizeAttribute_DEPRECATED, GridSize_DEPRECATED);
	// FQuat constant forwarded as a rotator (see header: Orient migrated FQuat -> FRotator).
	OrientValue.Update(OrientInput_DEPRECATED, OrientAttribute_DEPRECATED, OrientConstant_DEPRECATED.Rotator());
}

void FPCGExManhattanDetails::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	// GridSize: only the constant was PCG_Overridable (Input/Attribute were NotOverridable -> no pins).
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("GridSize")), FName(TEXT("GridSizeValue")), FName(TEXT("Constant")), FName(TEXT("Grid Size")));
	// Orient: attribute + constant pins existed.
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OrientAttribute")), FName(TEXT("OrientValue")), FName(TEXT("Attribute")), FName(TEXT("Orient (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OrientConstant")), FName(TEXT("OrientValue")), FName(TEXT("Constant")), FName(TEXT("Orient")));
}
#endif
