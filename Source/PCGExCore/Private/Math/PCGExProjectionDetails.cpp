// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExProjectionDetails.h"

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/PCGExBestFitPlane.h"

FPCGExGeo2DProjectionDetails::FPCGExGeo2DProjectionDetails(const bool InSupportLocalTangent)
	: bSupportLocalTangent(InSupportLocalTangent)
{
	WorldUp = PCGEX_CORE_SETTINGS.WorldUp;
	WorldFwd = PCGEX_CORE_SETTINGS.WorldForward;

	InitInternal(WorldUp);
	ProjectionVector.Constant = Normal;
}

#define PCGEX_SANITIZE_PROJECTION_METHOD if (!bSupportLocalTangent && Method == EPCGExProjectionMethod::LocalTangent) { Method = EPCGExProjectionMethod::BestFit; }

bool FPCGExGeo2DProjectionDetails::Init(const TSharedPtr<PCGExData::FFacade>& PointDataFacade)
{
	PCGEX_SANITIZE_PROJECTION_METHOD

	if (Method != EPCGExProjectionMethod::Normal)
	{
		Init(PCGExMath::FBestFitPlane(PointDataFacade->GetIn()->GetConstTransformValueRange()));
		return true;
	}

	FPCGExContext* Context = PointDataFacade->GetContext();
	if (!Context) { return false; }

	if (ProjectionVector.Input == EPCGExInputValueType::Attribute && !PCGExMetaHelpers::IsDataDomainAttribute(ProjectionVector.Attribute))
	{
		UE_LOG(LogTemp, Warning, TEXT("Only @Data domain attributes are supported for local projection."));
		ProjectionVector.Input = EPCGExInputValueType::Constant;
	}

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> NormalGetter = ProjectionVector.GetValueSetting(false);
	if (!NormalGetter->Init(PointDataFacade)) { return false; }

	InitInternal(NormalGetter->Read(0));

	return true;
}

bool FPCGExGeo2DProjectionDetails::Init(const TSharedPtr<PCGExData::FPointIO>& PointIO)
{
	PCGEX_SANITIZE_PROJECTION_METHOD
	
	if (Method != EPCGExProjectionMethod::Normal)
	{
		Init(PCGExMath::FBestFitPlane(PointIO->GetIn()->GetConstTransformValueRange()));
		return true;
	}

	FPCGExContext* Context = PointIO->GetContext();
	if (!Context) { return false; }

	if (ProjectionVector.Input == EPCGExInputValueType::Attribute && !PCGExMetaHelpers::IsDataDomainAttribute(ProjectionVector.Attribute))
	{
		UE_LOG(LogTemp, Warning, TEXT("Only @Data domain attributes are supported for local projection."));
		ProjectionVector.Input = EPCGExInputValueType::Constant;
	}

	if (!PCGExData::Helpers::TryGetSettingDataValue(Context, PointIO->GetIn(), ProjectionVector.Input, ProjectionVector.Attribute, ProjectionVector.Constant, Normal)) { return false; }

	InitInternal(Normal);

	return true;
}

bool FPCGExGeo2DProjectionDetails::Init(const UPCGData* InData)
{
	PCGEX_SANITIZE_PROJECTION_METHOD
	
	if (ProjectionVector.Input == EPCGExInputValueType::Attribute && !PCGExMetaHelpers::IsDataDomainAttribute(ProjectionVector.Attribute))
	{
		UE_LOG(LogTemp, Warning, TEXT("Only @Data domain attributes are supported for local projection."));
		ProjectionVector.Input = EPCGExInputValueType::Constant;
	}

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> NormalGetter = PCGExDetails::MakeSettingValue<FVector>(nullptr, InData, ProjectionVector.Input, ProjectionVector.Attribute, ProjectionVector.Constant);
	if (!NormalGetter) { return false; }

	InitInternal(NormalGetter->Read(0));

	return true;
}

void FPCGExGeo2DProjectionDetails::Init(const PCGExMath::FBestFitPlane& InFitPlane)
{
	InitInternal(InFitPlane.Normal());
}

void FPCGExGeo2DProjectionDetails::InitInternal(const FVector& InNormal)
{
	// Build a quaternion whose Z axis aligns with the projection normal.
	// Projecting a 3D point to 2D is then simply "unrotating" by this quaternion:
	// the result's X,Y are the 2D coordinates on the projection plane, Z is the depth.
	// The inverse quaternion is used for unprojection (2D → 3D).
	Normal = InNormal.GetSafeNormal(1E-08, WorldUp);

	// Adaptive X hint: when Normal ≈ WorldFwd, MakeFromZX degenerates.
	// Fall back to WorldUp which is guaranteed perpendicular in that case.
	const FVector XHint = FMath::Abs(FVector::DotProduct(Normal, WorldFwd)) < 0.95
		                      ? WorldFwd
		                      : WorldUp;
	ProjectionQuat = FRotationMatrix::MakeFromZX(Normal, XHint).ToQuat();
	ProjectionQuatInv = ProjectionQuat.Inverse();
}

#define PCGEX_READ_QUAT(_INDEX) FRotationMatrix::MakeFromZX(NormalGetter->Read(_INDEX).GetSafeNormal(1E-08, FVector::UpVector), WorldFwd).ToQuat()

template <typename T>
void FPCGExGeo2DProjectionDetails::ProjectFlat(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<T>& OutPositions) const
{
	const TConstPCGValueRange<FTransform> Transforms = InFacade->Source->GetInOut()->GetConstTransformValueRange();
	const int32 NumVectors = Transforms.Num();
	PCGExArrayHelpers::InitArray(OutPositions, NumVectors);
	PCGEX_PARALLEL_FOR(
		NumVectors,
		OutPositions[i] = T(ProjectFlat(Transforms[i].GetLocation()));
	)
}

template PCGEXCORE_API void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector2D>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector2D>& OutPositions) const;
template PCGEXCORE_API void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector>& OutPositions) const;
template PCGEXCORE_API void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector4>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector4>& OutPositions) const;

void FPCGExGeo2DProjectionDetails::Project(const TArray<FVector>& InPositions, TArray<FVector>& OutPositions) const
{
	const int32 NumVectors = InPositions.Num();
	PCGExArrayHelpers::InitArray(OutPositions, NumVectors);

	PCGEX_PARALLEL_FOR(
		NumVectors,
		OutPositions[i] = ProjectionQuat.UnrotateVector(InPositions[i]);
	)
}

void FPCGExGeo2DProjectionDetails::Project(const TArrayView<FVector>& InPositions, TArray<FVector2D>& OutPositions) const
{
	const int32 NumVectors = InPositions.Num();
	PCGExArrayHelpers::InitArray(OutPositions, NumVectors);
	PCGEX_PARALLEL_FOR(
		NumVectors,
		OutPositions[i] = FVector2D(ProjectionQuat.UnrotateVector(InPositions[i]));
	)
}

void FPCGExGeo2DProjectionDetails::Project(const TConstPCGValueRange<FTransform>& InTransforms, TArray<FVector2D>& OutPositions) const
{
	const int32 NumVectors = InTransforms.Num();
	PCGExArrayHelpers::InitArray(OutPositions, NumVectors);
	PCGEX_PARALLEL_FOR(
		NumVectors,
		OutPositions[i] = FVector2D(ProjectionQuat.UnrotateVector(InTransforms[i].GetLocation()));
	)
}

void FPCGExGeo2DProjectionDetails::Project(const TArrayView<FVector>& InPositions, std::vector<double>& OutPositions) const
{
	PCGEX_PARALLEL_FOR(
		InPositions.Num(),
		const FVector PP = ProjectionQuat.UnrotateVector(InPositions[i]);
		const int32 ii = i * 2;
		OutPositions[ii] = PP.X;
		OutPositions[ii+1] = PP.Y;
	)
}

void FPCGExGeo2DProjectionDetails::Project(const TConstPCGValueRange<FTransform>& InTransforms, std::vector<double>& OutPositions) const
{
	PCGEX_PARALLEL_FOR(
		InTransforms.Num(),
		const FVector PP = ProjectionQuat.UnrotateVector(InTransforms[i].GetLocation());
		const int32 ii = i * 2;
		OutPositions[ii] = PP.X;
		OutPositions[ii+1] = PP.Y;
	)
}

#if WITH_EDITOR
void FPCGExGeo2DProjectionDetails::ApplyDeprecation()
{
	ProjectionVector.Constant = ProjectionNormal_DEPRECATED;
	ProjectionVector.Attribute = LocalNormal_DEPRECATED;
	ProjectionVector.Input = bLocalProjectionNormal_DEPRECATED ? EPCGExInputValueType::Attribute : EPCGExInputValueType::Constant;
}
#endif
