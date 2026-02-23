// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <vector>
#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExProjectionDetails.generated.h"

template <typename ValueType, typename ViewType>
class TPCGValueRange;

template <typename ElementType>
using TConstPCGValueRange = TPCGValueRange<const ElementType, TConstStridedView<ElementType>>;

namespace PCGExMath
{
	struct FBestFitPlane;
}

namespace PCGExData
{
	class FFacade;
	class FPointIO;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

UENUM()
enum class EPCGExProjectionMethod : uint8
{
	Normal       = 0 UMETA(DisplayName = "Normal", ToolTip="Uses a normal to project on a plane."),
	BestFit      = 1 UMETA(DisplayName = "Best Fit", ToolTip="Compute eigen values to find the best-fit plane"),
	LocalTangent = 2 UMETA(DisplayName = "Local Tangent", ToolTip="Per-vertex tangent plane projection for non-planar clusters (spheres, curved surfaces)"),
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExGeo2DProjectionDetails
{
	GENERATED_BODY()

	FPCGExGeo2DProjectionDetails(const bool InSupportLocalTangent = false);

	UPROPERTY()
	bool bSupportLocalTangent = false;
	
	/** How to determine the projection plane. Normal uses explicit vector, BestFit computes from points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExProjectionMethod Method = EPCGExProjectionMethod::Normal;

#pragma region DEPRECATED

	UPROPERTY()
	FVector ProjectionNormal_DEPRECATED = FVector::UpVector;

	UPROPERTY()
	bool bLocalProjectionNormal_DEPRECATED = false;

	UPROPERTY()
	FPCGAttributePropertyInputSelector LocalNormal_DEPRECATED;

#pragma endregion

	/** Normal vector defining the projection plane. Points project perpendicular to this direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Method == EPCGExProjectionMethod::Normal", EditConditionHides))
	FPCGExInputShorthandSelectorVector ProjectionVector = FPCGExInputShorthandSelectorVector(FName("@Data.Projection"), FVector::UpVector, false);

	FVector Normal = FVector::UpVector;
	FQuat ProjectionQuat = FQuat::Identity;
	FQuat ProjectionQuatInv = FQuat::Identity;

	bool Init(const TSharedPtr<PCGExData::FFacade>& PointDataFacade);
	bool Init(const TSharedPtr<PCGExData::FPointIO>& PointIO);
	bool Init(const UPCGData* InData);
	void Init(const PCGExMath::FBestFitPlane& InFitPlane);

protected:
	void InitInternal(const FVector& InNormal);

public:
	~FPCGExGeo2DProjectionDetails() = default;

	FORCEINLINE FTransform Project(const FTransform& InTransform) const
	{
		return FTransform(
			ProjectionQuatInv * InTransform.GetRotation(),
			ProjectionQuatInv.RotateVector(InTransform.GetLocation()),
			InTransform.GetScale3D()
		);
	}

	FORCEINLINE void ProjectInPlace(FTransform& InTransform) const
	{
		InTransform.SetRotation(ProjectionQuatInv * InTransform.GetRotation());
		InTransform.SetLocation(ProjectionQuatInv.RotateVector(InTransform.GetLocation()));
	}

	FORCEINLINE FVector Project(const FVector& InPosition) const
	{
		return ProjectionQuat.UnrotateVector(InPosition);
	}

	FORCEINLINE FVector ProjectFlat(const FVector& InPosition) const
	{
		FVector RotatedPosition = ProjectionQuat.UnrotateVector(InPosition);
		RotatedPosition.Z = 0;
		return RotatedPosition;
	}

	FORCEINLINE FTransform ProjectFlat(const FTransform& InTransform) const
	{
		const FVector P = ProjectionQuat.UnrotateVector(InTransform.GetLocation());
		return FTransform(InTransform.GetRotation() * ProjectionQuat, FVector(P.X, P.Y, 0));
	}

	template <typename T>
	void ProjectFlat(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<T>& OutPositions) const;

	void Project(const TArray<FVector>& InPositions, TArray<FVector>& OutPositions) const;
	void Project(const TArrayView<FVector>& InPositions, TArray<FVector2D>& OutPositions) const;
	void Project(const TConstPCGValueRange<FTransform>& InTransforms, TArray<FVector2D>& OutPositions) const;
	void Project(const TArrayView<FVector>& InPositions, std::vector<double>& OutPositions) const;
	void Project(const TConstPCGValueRange<FTransform>& InTransforms, std::vector<double>& OutPositions) const;

	FORCEINLINE FTransform Unproject(const FTransform& InTransform) const
	{
		return FTransform(
			ProjectionQuat * InTransform.GetRotation(),
			ProjectionQuat.RotateVector(InTransform.GetLocation()),
			InTransform.GetScale3D()
		);
	}

	FORCEINLINE FVector Unproject(const FVector& InPosition) const
	{
		return ProjectionQuat.RotateVector(InPosition);
	}

	FORCEINLINE void UnprojectInPlace(FTransform& InTransform) const
	{
		InTransform.SetRotation(ProjectionQuat * InTransform.GetRotation());
		InTransform.SetLocation(ProjectionQuat.RotateVector(InTransform.GetLocation()));
	}

	FORCEINLINE void UnprojectInPlace(FVector& InPosition) const
	{
		InPosition = ProjectionQuat.RotateVector(InPosition);
	}

#if WITH_EDITOR
	void ApplyDeprecation();
#endif

protected:
	FVector WorldUp = FVector::UpVector;
	FVector WorldFwd = FVector::ForwardVector;
};

extern template void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector2D>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector2D>& OutPositions) const;
extern template void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector>& OutPositions) const;
extern template void FPCGExGeo2DProjectionDetails::ProjectFlat<FVector4>(const TSharedPtr<PCGExData::FFacade>& InFacade, TArray<FVector4>& OutPositions) const;
