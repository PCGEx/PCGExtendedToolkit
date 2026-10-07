// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExFuseDetails.h"

#include "Data/PCGExPointElements.h"
#include "Details/PCGExDistancesDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMathDistances.h"


FPCGExFuseDetailsBase::FPCGExFuseDetailsBase()
{
	ToleranceInput = EPCGExInputValueType::Constant;
}

FPCGExFuseDetailsBase::FPCGExFuseDetailsBase(const bool InSupportLocalTolerance)
	: bSupportLocalTolerance(InSupportLocalTolerance)
{
	if (!bSupportLocalTolerance)
	{
		ToleranceInput = EPCGExInputValueType::Constant;
	}
}

FPCGExFuseDetailsBase::FPCGExFuseDetailsBase(const bool InSupportLocalTolerance, const double InTolerance)
	: bSupportLocalTolerance(InSupportLocalTolerance)
	  , Tolerance(InTolerance)
{
	if (!bSupportLocalTolerance)
	{
		ToleranceInput = EPCGExInputValueType::Constant;
	}
}

bool FPCGExFuseDetailsBase::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (!bComponentWiseTolerance)
	{
		Tolerances = FVector(Tolerance);
	}

	if (!InDataFacade)
	{
		ToleranceGetter = PCGExDetails::MakeSettingValue<FVector>(Tolerances);
	}
	else
	{
		ToleranceGetter = PCGExDetails::MakeSettingValue<FVector>(ToleranceInput, ToleranceAttribute, Tolerances);
	}

	if (!ToleranceGetter->Init(InDataFacade))
	{
		return false;
	}

	return true;
}

bool FPCGExFuseDetailsBase::IsWithinTolerance(const double DistSquared, const int32 PointIndex) const
{
	return FMath::IsWithin<double, double>(DistSquared, 0, FMath::Square(ToleranceGetter->Read(PointIndex).X));
}

bool FPCGExFuseDetailsBase::IsWithinTolerance(const FVector& Source, const FVector& Target, const int32 PointIndex) const
{
	return FMath::IsWithin<double, double>(FVector::DistSquared(Source, Target), 0, FMath::Square(ToleranceGetter->Read(PointIndex).X));
}

bool FPCGExFuseDetailsBase::IsWithinToleranceComponentWise(const FVector& Source, const FVector& Target, const int32 PointIndex) const
{
	const FVector CWTolerance = ToleranceGetter->Read(PointIndex);
	return FMath::IsWithin<double, double>(abs(Source.X - Target.X), 0, CWTolerance.X) && FMath::IsWithin<double, double>(abs(Source.Y - Target.Y), 0, CWTolerance.Y) && FMath::IsWithin<double, double>(abs(Source.Z - Target.Z), 0, CWTolerance.Z);
}

FVector FPCGExFuseDetailsBase::GetToleranceExtent(const int32 PointIndex) const
{
	const FVector Tol = ToleranceGetter->Read(PointIndex);
	return bComponentWiseTolerance ? Tol : FVector(Tol.X);
}

FPCGExSourceFuseDetails::FPCGExSourceFuseDetails()
	: FPCGExFuseDetailsBase(false)
{
}

FPCGExSourceFuseDetails::FPCGExSourceFuseDetails(const bool InSupportLocalTolerance)
	: FPCGExFuseDetailsBase(InSupportLocalTolerance)
{
}

FPCGExSourceFuseDetails::FPCGExSourceFuseDetails(const bool InSupportLocalTolerance, const double InTolerance)
	: FPCGExFuseDetailsBase(InSupportLocalTolerance, InTolerance)
{
}

FPCGExSourceFuseDetails::FPCGExSourceFuseDetails(const bool InSupportLocalTolerance, const double InTolerance, const EPCGExDistance SourceMethod)
	: FPCGExFuseDetailsBase(InSupportLocalTolerance, InTolerance)
	  , SourceDistance(SourceMethod)
{
}

FPCGExFuseDetails::FPCGExFuseDetails()
	: FPCGExSourceFuseDetails(false)
	  , Distances(GetDistances())
{
}

FPCGExFuseDetails::FPCGExFuseDetails(const bool InSupportLocalTolerance)
	: FPCGExSourceFuseDetails(InSupportLocalTolerance)
	  , Distances(GetDistances())
{
}

FPCGExFuseDetails::FPCGExFuseDetails(const bool InSupportLocalTolerance, const double InTolerance)
	: FPCGExSourceFuseDetails(InSupportLocalTolerance, InTolerance)
	  , Distances(GetDistances())
{
}

FPCGExFuseDetails::FPCGExFuseDetails(const bool InSupportLocalTolerance, const double InTolerance, const EPCGExDistance InSourceMethod)
	: FPCGExSourceFuseDetails(InSupportLocalTolerance, InTolerance, InSourceMethod)
	  , Distances(GetDistances())
{
}

FPCGExFuseDetails::FPCGExFuseDetails(const bool InSupportLocalTolerance, const double InTolerance, const EPCGExDistance InSourceMethod, const EPCGExDistance InTargetMethod)
	: FPCGExSourceFuseDetails(InSupportLocalTolerance, InTolerance, InSourceMethod)
	  , TargetDistance(InTargetMethod)
	  , Distances(GetDistances())
{
}

bool FPCGExFuseDetails::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (!FPCGExFuseDetailsBase::Init(InContext, InDataFacade))
	{
		return false;
	}
	Distances = GetDistances();
	return true;
}

EPCGExFuseMethod FPCGExFuseDetails::GetEffectiveMethod() const
{
	return HasPerPointTolerance() ? EPCGExFuseMethod::Octree : FuseMethod;
}

uint64 FPCGExFuseDetails::GetGridKey(const FVector& Location, const int32 PointIndex) const
{
	return PCGEx::SH3(Location + VoxelGridOffset, PCGEx::SafeTolerance(ToleranceGetter->Read(PointIndex)));
}

FBox FPCGExFuseDetails::GetOctreeBox(const FVector& Location, const int32 PointIndex) const
{
	const FVector Extent = GetToleranceExtent(PointIndex);
	return FBox(Location - Extent, Location + Extent);
}

void FPCGExFuseDetails::GetCenters(const PCGExData::FConstPoint& SourcePoint, const PCGExData::FConstPoint& TargetPoint, FVector& OutSource, FVector& OutTarget) const
{
	const FVector TargetLocation = TargetPoint.GetTransform().GetLocation();
	OutSource = Distances->GetSourceCenter(SourcePoint, SourcePoint.GetTransform().GetLocation(), TargetLocation);
	OutTarget = Distances->GetTargetCenter(TargetPoint, TargetLocation, OutSource);
}

bool FPCGExFuseDetails::IsWithinTolerance(const PCGExData::FConstPoint& SourcePoint, const PCGExData::FConstPoint& TargetPoint) const
{
	FVector A;
	FVector B;
	GetCenters(SourcePoint, TargetPoint, A, B);
	const double Tol = FMath::Max(ToleranceGetter->Read(SourcePoint.Index).X, ToleranceGetter->Read(TargetPoint.Index).X);
	return FMath::IsWithin<double, double>(FVector::DistSquared(A, B), 0, FMath::Square(Tol));
}

bool FPCGExFuseDetails::IsWithinToleranceComponentWise(const PCGExData::FConstPoint& SourcePoint, const PCGExData::FConstPoint& TargetPoint) const
{
	FVector A;
	FVector B;
	GetCenters(SourcePoint, TargetPoint, A, B);
	const FVector CWTolerance = FVector::Max(ToleranceGetter->Read(SourcePoint.Index), ToleranceGetter->Read(TargetPoint.Index));
	return FMath::IsWithin<double, double>(abs(A.X - B.X), 0, CWTolerance.X) && FMath::IsWithin<double, double>(abs(A.Y - B.Y), 0, CWTolerance.Y) && FMath::IsWithin<double, double>(abs(A.Z - B.Z), 0, CWTolerance.Z);
}

const PCGExMath::IDistances* FPCGExFuseDetails::GetDistances() const
{
	return PCGExMath::GetDistances(SourceDistance, TargetDistance);
}
