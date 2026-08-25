// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>
#include "CoreMinimal.h"
#include "OrientedBoxTypes.h"

template <typename ValueType, typename ViewType>
class TPCGValueRange;

template <typename ElementType>
using TConstPCGValueRange = TPCGValueRange<const ElementType, TConstStridedView<ElementType>>;

enum class EPCGExAxisOrder : uint8;

namespace PCGExMath
{
	struct PCGEXCORE_API FBestFitPlane
	{
		FBestFitPlane() = default;

		explicit FBestFitPlane(const TConstPCGValueRange<FTransform>& InTransforms, bool bUsePreciseBounds = false);
		explicit FBestFitPlane(const TConstPCGValueRange<FTransform>& InTransforms, TArrayView<int32> InIndices, bool bUsePreciseBounds = false);
		explicit FBestFitPlane(const TArrayView<const FVector> InPositions, bool bUsePreciseBounds = false);
		explicit FBestFitPlane(const TArrayView<const FVector2D> InPositions, bool bUsePreciseBounds = false);

		using FGetElementPositionCallback = std::function<FVector(int32)>;
		FBestFitPlane(const int32 NumElements, FGetElementPositionCallback&& GetPointFunc, bool bUsePreciseBounds = false);
		FBestFitPlane(const int32 NumElements, FGetElementPositionCallback&& GetPointFunc, const FVector& Extra, bool bUsePreciseBounds = false);

		/** Axes-only fit reusing an already-known centroid: skips the centroid AND extents passes
		 *  (2 of ComputePCA's 3 point sweeps). Extents stay at their default -- do not read them. */
		static FBestFitPlane PlaneOnly(int32 NumElements, FGetElementPositionCallback&& GetPointFunc, const FVector& InKnownCentroid);

		FVector Centroid = FVector::ZeroVector;
		FVector Extents = FVector::OneVector;

		int32 Swizzle[3] = {0, 1, 2};
		FVector Axis[3] = {FVector::ForwardVector, FVector::RightVector, FVector::UpVector};

		FORCEINLINE FVector Normal() const
		{
			return Axis[2];
		}

		FTransform GetTransform() const;
		FTransform GetTransform(EPCGExAxisOrder Order) const;

		/** Get extents in default XYZ order */
		FORCEINLINE FVector GetExtents() const
		{
			return Extents;
		}

		/** Get extents reordered to match the specified axis order */
		FVector GetExtents(EPCGExAxisOrder Order) const;

	protected:
		void ProcessBox(const UE::Geometry::FOrientedBox3d& Box);
	};

	struct PCGEXCORE_API FSwizzler
	{
		FSwizzler() = default;
	};
}
