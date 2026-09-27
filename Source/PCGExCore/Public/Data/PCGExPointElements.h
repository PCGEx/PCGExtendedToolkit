// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGExH.h"
#include "Core/PCGExMTCommon.h"
#include "UObject/UObjectGlobals.h"

class UPCGBasePointData;

namespace PCGExData
{
	class FPointIO;
}

namespace PCGExData
{
#pragma region FPoint

	struct PCGEXCORE_API FElement
	{
		int32 Index = -1;
		int32 IO = -1;

		FElement() = default;
		explicit FElement(const uint64 Hash);
		explicit FElement(const int32 InIndex, const int32 InIO = -1);
		FElement(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex);

		FORCEINLINE bool IsValid() const
		{
			return Index >= 0;
		}

		FORCEINLINE uint64 H64() const
		{
			return PCGEx::H64U(Index, IO);
		}

		explicit operator int32() const
		{
			return Index;
		}

		bool operator==(const FElement& Other) const
		{
			return Index == Other.Index && IO == Other.IO;
		}

		FORCEINLINE friend uint32 GetTypeHash(const FElement& Key)
		{
			return HashCombineFast(Key.Index, Key.IO);
		}
	};

	// FPoint is used when we only care about point index
	// And possibly IOIndex as well, but without the need to track the actual data object.
	// This makes it a barebone way to track point indices with some additional mapping mechanism with a secondary index
	struct PCGEXCORE_API FPoint : FElement
	{
		FPoint() = default;
		virtual ~FPoint() = default;

		explicit FPoint(const uint64 Hash);
		explicit FPoint(const int32 InIndex, const int32 InIO = -1);
		FPoint(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex);

		virtual const FTransform& GetTransform() const PCGEX_NOT_IMPLEMENTED_RET(GetTransform, FTransform::Identity)
		virtual void GetTransformNoScale(FTransform& OutTransform) const PCGEX_NOT_IMPLEMENTED(GetTransform)
		virtual FVector GetLocation() const PCGEX_NOT_IMPLEMENTED_RET(GetLocation, FVector::OneVector)
		virtual FVector GetScale3D() const PCGEX_NOT_IMPLEMENTED_RET(GetScale3D, FVector::OneVector)
		virtual FQuat GetRotation() const PCGEX_NOT_IMPLEMENTED_RET(GetRotation, FQuat::Identity)
		virtual FVector GetBoundsMin() const PCGEX_NOT_IMPLEMENTED_RET(GetBoundsMin, FVector::OneVector)
		virtual FVector GetBoundsMax() const PCGEX_NOT_IMPLEMENTED_RET(GetBoundsMax, FVector::OneVector)
		virtual FVector GetLocalCenter() const PCGEX_NOT_IMPLEMENTED_RET(GetLocalBounds, FVector::OneVector)
		virtual FVector GetExtents() const PCGEX_NOT_IMPLEMENTED_RET(GetExtents, FVector::OneVector)
		virtual FVector GetScaledExtents() const PCGEX_NOT_IMPLEMENTED_RET(GetScaledExtents, FVector::OneVector)
		virtual FBox GetLocalBounds() const PCGEX_NOT_IMPLEMENTED_RET(GetLocalBounds, FBox(NoInit))
		virtual FBox GetLocalDensityBounds() const PCGEX_NOT_IMPLEMENTED_RET(GetLocalDensityBounds, FBox(NoInit))
		virtual FBox GetScaledBounds() const PCGEX_NOT_IMPLEMENTED_RET(GetScaledBounds, FBox(NoInit))
		virtual float GetSteepness() const PCGEX_NOT_IMPLEMENTED_RET(GetSteepness, 1)
		virtual float GetDensity() const PCGEX_NOT_IMPLEMENTED_RET(GetDensity, 1)
		virtual int64 GetMetadataEntry() const PCGEX_NOT_IMPLEMENTED_RET(GetMetadataEntry, 0)
		virtual FVector4 GetColor() const PCGEX_NOT_IMPLEMENTED_RET(GetColor, FVector4(1))
		virtual FVector GetLocalSize() const PCGEX_NOT_IMPLEMENTED_RET(GetLocalSize, FVector::OneVector)
		virtual FVector GetScaledLocalSize() const PCGEX_NOT_IMPLEMENTED_RET(GetScaledLocalSize, FVector::OneVector)
		virtual int32 GetSeed() const PCGEX_NOT_IMPLEMENTED_RET(GetSeed, 0)
	};

	struct PCGEXCORE_API FWeightedPoint : FPoint
	{
		double Weight = 0;
		// Applied after the blend op's weight curve, so a sample split across several points keeps its shares exact.
		double Split = 1;

		FWeightedPoint() = default;
		virtual ~FWeightedPoint() override = default;

		explicit FWeightedPoint(const uint64 Hash, const double InWeight = 1);
		explicit FWeightedPoint(const int32 InIndex, const double InWeight = 1, const int32 InIO = -1);
		FWeightedPoint(const int32 InIndex, const double InWeight, const int32 InIO, const double InSplit);
		FWeightedPoint(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex, const double InWeight = 1);
	};

#define PCGEX_POINT_PROXY_OVERRIDES \
virtual const FTransform& GetTransform() const override;\
virtual void GetTransformNoScale(FTransform& OutTransform) const override;\
virtual FVector GetLocation() const override;\
virtual FVector GetScale3D() const override;\
virtual FQuat GetRotation() const override;\
virtual FVector GetBoundsMin() const override;\
virtual FVector GetBoundsMax() const override;\
virtual FVector GetLocalCenter() const override;\
virtual FVector GetExtents() const override;\
virtual FVector GetScaledExtents() const override;\
virtual FBox GetLocalBounds() const override;\
virtual FBox GetLocalDensityBounds() const override;\
virtual FBox GetScaledBounds() const override;\
virtual float GetSteepness() const override;\
virtual float GetDensity() const override;\
virtual int64 GetMetadataEntry() const override;\
virtual FVector4 GetColor() const override;\
virtual FVector GetLocalSize() const override;\
virtual FVector GetScaledLocalSize() const override;\
virtual int32 GetSeed() const override;


	// A beefed-up version of FPoint that implement FPoint getters
	// and comes with setters helpers
	// Should be used when the point it references actually exists, otherwise getter will return bad data
	struct PCGEXCORE_API FMutablePoint : FPoint
	{
		UPCGBasePointData* Data = nullptr;

		FMutablePoint() = default;

		FMutablePoint(UPCGBasePointData* InData, const uint64 Hash);
		FMutablePoint(UPCGBasePointData* InData, const int32 InIndex, const int32 InIO = -1);
		FMutablePoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex);

		PCGEX_POINT_PROXY_OVERRIDES

		virtual void SetDensity(const float InValue);
		virtual void SetSteepness(const float InValue);
		virtual void SetTransform(const FTransform& InValue);
		virtual void SetLocation(const FVector& InValue);
		virtual void SetScale3D(const FVector& InValue);
		virtual void SetRotation(const FQuat& InValue);
		virtual void SetBoundsMin(const FVector& InValue);
		virtual void SetBoundsMax(const FVector& InValue);
		virtual void SetLocalCenter(const FVector& InValue);
		virtual void SetExtents(const FVector& InValue, const bool bKeepLocalCenter = false);
		virtual void SetLocalBounds(const FBox& InValue);
		virtual void SetMetadataEntry(const int64 InValue);
		virtual void SetColor(const FVector4& InValue);
		virtual void SetSeed(const int32 InValue);

		bool operator==(const FMutablePoint& Other) const
		{
			return Index == Other.Index && Data == Other.Data;
		}
	};

	// A beefed-up version of FPoint that implement FPoint getters
	// Only for reading data
	struct PCGEXCORE_API FConstPoint : FPoint
	{
		const UPCGBasePointData* Data = nullptr;

		FConstPoint() = default;

		FConstPoint(const FMutablePoint& InPoint);

		FConstPoint(const UPCGBasePointData* InData, const uint64 Hash);
		FConstPoint(const UPCGBasePointData* InData, const int32 InIndex, const int32 InIO = -1);
		FConstPoint(const UPCGBasePointData* InData, const FPoint& InPoint);
		FConstPoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex);

		PCGEX_POINT_PROXY_OVERRIDES

		bool operator==(const FConstPoint& Other) const
		{
			return Index == Other.Index && IO == Other.IO && Data == Other.Data;
		}
	};

	// An oddball struct that store some basic information but exposes the same as API FPoint
	// Mostly to be used by helpers that will mutate transform & bounds in-place
	// We need this because in some cases with want to do modification on points that aren't allocated yet
	// or simply work need a basic, mutable spatial representation of a point in the form of Transform + Local bounds

	struct PCGEXCORE_API FProxyPoint : FPoint
	{
		// A point-like object that makes some maths stuff easy to manipulate
		// Also store indices and stuff in order to ret

		FTransform Transform = FTransform::Identity;
		FVector BoundsMin = FVector::OneVector * -1;
		FVector BoundsMax = FVector::OneVector;
		float Steepness = 1; // Required for local density bounds
		FVector4 Color = FVector4(1);

		FProxyPoint() = default;

		explicit FProxyPoint(const FMutablePoint& InPoint);
		explicit FProxyPoint(const FConstPoint& InPoint);
		FProxyPoint(const UPCGBasePointData* InData, const uint64 Hash);
		FProxyPoint(const UPCGBasePointData* InData, const int32 InIndex, const int32 InIO = -1);
		FProxyPoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex);

		virtual const FTransform& GetTransform() const override;
		virtual FVector GetLocation() const override;
		virtual FVector GetScale3D() const override;
		virtual FQuat GetRotation() const override;
		virtual FVector GetBoundsMin() const override;
		virtual FVector GetBoundsMax() const override;
		virtual FVector GetExtents() const override;
		virtual FVector GetScaledExtents() const override;
		virtual FBox GetLocalBounds() const override;
		virtual FBox GetScaledBounds() const override;
		virtual FBox GetLocalDensityBounds() const override;
		virtual FVector4 GetColor() const override;
		virtual FVector GetLocalSize() const override;
		virtual FVector GetScaledLocalSize() const override;

		virtual void SetTransform(const FTransform& InValue);
		virtual void SetLocation(const FVector& InValue);
		virtual void SetScale3D(const FVector& InValue);
		virtual void SetQuat(const FQuat& InValue);
		virtual void SetBoundsMin(const FVector& InValue);
		virtual void SetBoundsMax(const FVector& InValue);
		virtual void SetExtents(const FVector& InValue, const bool bKeepLocalCenter = false);
		virtual void SetLocalBounds(const FBox& InValue);

		bool operator==(const FProxyPoint& Other) const
		{
			return Index == Other.Index && IO == Other.IO;
		}
	};

#undef PCGEX_POINT_PROXY_OVERRIDES

	PCGEXCORE_API extern const FPoint NONE_Point;
	PCGEXCORE_API extern const FMutablePoint NONE_MutablePoint;
	PCGEXCORE_API extern const FConstPoint NONE_ConstPoint;

#pragma endregion

	struct PCGEXCORE_API FScope : PCGExMT::FScope
	{
		UPCGBasePointData* Data = nullptr;

		FScope() = default;

		FScope(UPCGBasePointData* InData, const int32 InStart, const int32 InCount);
		FScope(const UPCGBasePointData* InData, const int32 InStart, const int32 InCount);

		FConstPoint CFirst() const;
		FConstPoint CLast() const;
		FMutablePoint MFirst() const;
		FMutablePoint MLast() const;

		~FScope() = default;
		bool IsValid() const;
	};
}
