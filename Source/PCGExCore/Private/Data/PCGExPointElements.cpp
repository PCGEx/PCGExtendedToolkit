// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExPointElements.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExPointIO.h"

namespace PCGExData
{
	const FPoint NONE_Point(1, -1);
	const FMutablePoint NONE_MutablePoint(nullptr, -1, -1);
	const FConstPoint NONE_ConstPoint(nullptr, -1, -1);

#pragma region FScope

	FScope::FScope(UPCGBasePointData* InData, const int32 InStart, const int32 InCount)
		: PCGExMT::FScope(InStart, InCount)
		  , Data(InData)
	{
	}

	FScope::FScope(const UPCGBasePointData* InData, const int32 InStart, const int32 InCount)
		: PCGExMT::FScope(InStart, InCount)
		  , Data(const_cast<UPCGBasePointData*>(InData))
	{
	}

	FConstPoint FScope::CFirst() const
	{
		return FConstPoint(Data, Start);
	}

	FConstPoint FScope::CLast() const
	{
		return FConstPoint(Data, End - 1);
	}

	FMutablePoint FScope::MFirst() const
	{
		return FMutablePoint(Data, Start);
	}

	FMutablePoint FScope::MLast() const
	{
		return FMutablePoint(Data, End - 1);
	}

	bool FScope::IsValid() const
	{
		return Start >= 0 && Count > 0 && Data->GetNumPoints() <= End;
	}

#pragma endregion

#pragma region FPoint

	FElement::FElement(const uint64 Hash)
		: Index(PCGEx::H64A(Hash))
		  , IO(PCGEx::H64B(Hash))
	{
	}

	FElement::FElement(const int32 InIndex, const int32 InIO)
		: Index(InIndex)
		  , IO(InIO)
	{
	}

	FElement::FElement(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex)
		: Index(InIndex)
		  , IO(InIO->IOIndex)
	{
	}

	FPoint::FPoint(const uint64 Hash)
		: FElement(Hash)
	{
	}

	FPoint::FPoint(const int32 InIndex, const int32 InIO)
		: FElement(InIndex, InIO)
	{
	}

	FPoint::FPoint(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex)
		: FElement(InIO, InIndex)
	{
	}

	FWeightedPoint::FWeightedPoint(const uint64 Hash, const double InWeight)
		: FPoint(Hash)
	{
	}

	FWeightedPoint::FWeightedPoint(const int32 InIndex, const double InWeight, const int32 InIO)
		: FPoint(InIndex, InIO)
		  , Weight(InWeight)
	{
	}

	FWeightedPoint::FWeightedPoint(const int32 InIndex, const double InWeight, const int32 InIO, const double InSplit)
		: FPoint(InIndex, InIO)
		  , Weight(InWeight)
		  , Split(InSplit)
	{
	}

	FWeightedPoint::FWeightedPoint(const TSharedPtr<FPointIO>& InIO, const uint32 InIndex, const double InWeight)
		: FPoint(InIO, InIndex)
		  , Weight(InWeight)
	{
	}

	FMutablePoint::FMutablePoint(UPCGBasePointData* InData, const int32 InIndex, const int32 InIO)
		: FPoint(InIndex, InIO)
		  , Data(InData)
	{
	}

	FMutablePoint::FMutablePoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex)
		: FPoint(InFacade, InIndex)
		  , Data(InFacade->GetOut())
	{
	}

	FTransform& FMutablePoint::GetMutableTransform()
	{
		return Data->GetTransformValueRange(false)[Index];
	}

	void FMutablePoint::SetDensity(const float InValue)
	{
		Data->GetDensityValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetSteepness(const float InValue)
	{
		Data->GetSteepnessValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetTransform(const FTransform& InValue)
	{
		Data->GetTransformValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetLocation(const FVector& InValue)
	{
		Data->GetTransformValueRange(false)[Index].SetLocation(InValue);
	}

	void FMutablePoint::SetScale3D(const FVector& InValue)
	{
		Data->GetTransformValueRange(false)[Index].SetScale3D(InValue);
	}

	void FMutablePoint::SetRotation(const FQuat& InValue)
	{
		Data->GetTransformValueRange(false)[Index].SetRotation(InValue);
	}

	void FMutablePoint::SetBoundsMin(const FVector& InValue)
	{
		Data->GetBoundsMinValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetBoundsMax(const FVector& InValue)
	{
		Data->GetBoundsMaxValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetLocalCenter(const FVector& InValue)
	{
		PCGPointHelpers::SetLocalCenter(InValue, Data->GetBoundsMinValueRange(false)[Index], Data->GetBoundsMaxValueRange(false)[Index]);
	}

	void FMutablePoint::SetExtents(const FVector& InValue, const bool bKeepLocalCenter)
	{
		const TPCGValueRange<FVector> BoundsMin = Data->GetBoundsMinValueRange(false);
		const TPCGValueRange<FVector> BoundsMax = Data->GetBoundsMaxValueRange(false);

		if (bKeepLocalCenter)
		{
			const FVector LocalCenter = Data->GetLocalCenter(Index);
			BoundsMin[Index] = LocalCenter - InValue;
			BoundsMax[Index] = LocalCenter + InValue;
		}
		else
		{
			BoundsMin[Index] = -InValue;
			BoundsMax[Index] = InValue;
		}
	}

	void FMutablePoint::SetLocalBounds(const FBox& InValue)
	{
		Data->GetBoundsMinValueRange(false)[Index] = InValue.Min;
		Data->GetBoundsMaxValueRange(false)[Index] = InValue.Max;
	}

	void FMutablePoint::SetMetadataEntry(const int64 InValue)
	{
		Data->GetMetadataEntryValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetColor(const FVector4& InValue)
	{
		Data->GetColorValueRange(false)[Index] = InValue;
	}

	void FMutablePoint::SetSeed(const int32 InValue)
	{
		Data->GetSeedValueRange(false)[Index] = InValue;
	}

	FConstPoint::FConstPoint(const FMutablePoint& InPoint)
		: FConstPoint(InPoint.Data, InPoint.Index)
	{
	}

	FConstPoint::FConstPoint(const UPCGBasePointData* InData, const uint64 Hash)
		: FPoint(Hash)
		  , Data(InData)
	{
	}

	FConstPoint::FConstPoint(const UPCGBasePointData* InData, const int32 InIndex, const int32 InIO)
		: FPoint(InIndex, InIO)
		  , Data(InData)
	{
	}

	FConstPoint::FConstPoint(const UPCGBasePointData* InData, const FPoint& InPoint)
		: FPoint(InPoint.Index, InPoint.IO)
		  , Data(InData)
	{
	}

	FConstPoint::FConstPoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex)
		: FPoint(InFacade, InIndex)
		  , Data(InFacade->GetIn())
	{
	}

	// FConstPoint getters
	const FTransform& FConstPoint::GetTransform() const
	{
		return Data->GetTransform(Index);
	}

	void FConstPoint::GetTransformNoScale(FTransform& OutTransform) const
	{
		OutTransform = Data->GetTransform(Index);
		OutTransform.SetScale3D(FVector::OneVector);
	}

	FVector FConstPoint::GetLocation() const
	{
		return Data->GetTransform(Index).GetLocation();
	}

	FVector FConstPoint::GetScale3D() const
	{
		return Data->GetTransform(Index).GetScale3D();
	}

	FQuat FConstPoint::GetRotation() const
	{
		return Data->GetTransform(Index).GetRotation();
	}

	FVector FConstPoint::GetBoundsMin() const
	{
		return Data->GetBoundsMin(Index);
	}

	FVector FConstPoint::GetBoundsMax() const
	{
		return Data->GetBoundsMax(Index);
	}

	FVector FConstPoint::GetLocalCenter() const
	{
		return Data->GetLocalCenter(Index);
	}

	FVector FConstPoint::GetExtents() const
	{
		return Data->GetExtents(Index);
	}

	FVector FConstPoint::GetScaledExtents() const
	{
		return Data->GetScaledExtents(Index);
	}

	FBox FConstPoint::GetLocalBounds() const
	{
		return Data->GetLocalBounds(Index);
	}

	FBox FConstPoint::GetLocalDensityBounds() const
	{
		return Data->GetLocalDensityBounds(Index);
	}

	FBox FConstPoint::GetScaledBounds() const
	{
		const FVector Scale3D = Data->GetTransform(Index).GetScale3D();
		return FBox(Data->GetBoundsMin(Index) * Scale3D, Data->GetBoundsMax(Index) * Scale3D);
	}

	float FConstPoint::GetSteepness() const
	{
		return Data->GetSteepness(Index);
	}

	float FConstPoint::GetDensity() const
	{
		return Data->GetDensity(Index);
	}

	int64 FConstPoint::GetMetadataEntry() const
	{
		return Data->GetMetadataEntry(Index);
	}

	FVector4 FConstPoint::GetColor() const
	{
		return Data->GetColor(Index);
	}

	FVector FConstPoint::GetLocalSize() const
	{
		return Data->GetLocalSize(Index);
	}

	FVector FConstPoint::GetScaledLocalSize() const
	{
		return Data->GetScaledLocalSize(Index);
	}

	int32 FConstPoint::GetSeed() const
	{
		return Data->GetSeed(Index);
	}

	FMutablePoint::FMutablePoint(UPCGBasePointData* InData, const uint64 Hash)
		: FPoint(Hash)
		  , Data(InData)
	{
	}

	// FMutablePoint getters
	const FTransform& FMutablePoint::GetTransform() const
	{
		return Data->GetTransform(Index);
	}

	void FMutablePoint::GetTransformNoScale(FTransform& OutTransform) const
	{
		OutTransform = Data->GetTransform(Index);
		OutTransform.SetScale3D(FVector::OneVector);
	}

	FVector FMutablePoint::GetLocation() const
	{
		return Data->GetTransform(Index).GetLocation();
	}

	FVector FMutablePoint::GetScale3D() const
	{
		return Data->GetTransform(Index).GetScale3D();
	}

	FQuat FMutablePoint::GetRotation() const
	{
		return Data->GetTransform(Index).GetRotation();
	}

	FVector FMutablePoint::GetBoundsMin() const
	{
		return Data->GetBoundsMin(Index);
	}

	FVector FMutablePoint::GetBoundsMax() const
	{
		return Data->GetBoundsMax(Index);
	}

	FVector FMutablePoint::GetLocalCenter() const
	{
		return Data->GetLocalCenter(Index);
	}

	FVector FMutablePoint::GetExtents() const
	{
		return Data->GetExtents(Index);
	}

	FVector FMutablePoint::GetScaledExtents() const
	{
		return Data->GetScaledExtents(Index);
	}

	FBox FMutablePoint::GetLocalBounds() const
	{
		return Data->GetLocalBounds(Index);
	}

	FBox FMutablePoint::GetLocalDensityBounds() const
	{
		return Data->GetLocalDensityBounds(Index);
	}

	FBox FMutablePoint::GetScaledBounds() const
	{
		const FVector Scale3D = Data->GetTransform(Index).GetScale3D();
		return FBox(Data->GetBoundsMin(Index) * Scale3D, Data->GetBoundsMax(Index) * Scale3D);
	}

	float FMutablePoint::GetSteepness() const
	{
		return Data->GetSteepness(Index);
	}

	float FMutablePoint::GetDensity() const
	{
		return Data->GetDensity(Index);
	}

	int64 FMutablePoint::GetMetadataEntry() const
	{
		return Data->GetMetadataEntry(Index);
	}

	FVector4 FMutablePoint::GetColor() const
	{
		return Data->GetColor(Index);
	}

	FVector FMutablePoint::GetLocalSize() const
	{
		return Data->GetLocalSize(Index);
	}

	FVector FMutablePoint::GetScaledLocalSize() const
	{
		return Data->GetScaledLocalSize(Index);
	}

	int32 FMutablePoint::GetSeed() const
	{
		return Data->GetSeed(Index);
	}

	FProxyPoint::FProxyPoint(const FMutablePoint& InPoint)
		: Transform(InPoint.GetTransform())
		  , BoundsMin(InPoint.GetBoundsMin())
		  , BoundsMax(InPoint.GetBoundsMax())
		  , Steepness(InPoint.GetSteepness())
		  , Color(InPoint.GetColor())
	{
	}

	FProxyPoint::FProxyPoint(const FConstPoint& InPoint)
		: Transform(InPoint.GetTransform())
		  , BoundsMin(InPoint.GetBoundsMin())
		  , BoundsMax(InPoint.GetBoundsMax())
		  , Steepness(InPoint.GetSteepness())
		  , Color(InPoint.GetColor())
	{
	}

	FProxyPoint::FProxyPoint(const UPCGBasePointData* InData, const uint64 Hash)
		: FProxyPoint(FConstPoint(InData, Hash))
	{
	}

	FProxyPoint::FProxyPoint(const UPCGBasePointData* InData, const int32 InIndex, const int32 InIO)
		: FProxyPoint(FConstPoint(InData, InIndex, InIO))
	{
	}

	FProxyPoint::FProxyPoint(const TSharedPtr<FPointIO>& InFacade, const int32 InIndex)
		: FProxyPoint(FConstPoint(InFacade, InIndex))
	{
	}

	// FProxyPoint getters
	const FTransform& FProxyPoint::GetTransform() const
	{
		return Transform;
	}

	FVector FProxyPoint::GetLocation() const
	{
		return Transform.GetLocation();
	}

	FVector FProxyPoint::GetScale3D() const
	{
		return Transform.GetScale3D();
	}

	FQuat FProxyPoint::GetRotation() const
	{
		return Transform.GetRotation();
	}

	FVector FProxyPoint::GetBoundsMin() const
	{
		return BoundsMin;
	}

	FVector FProxyPoint::GetBoundsMax() const
	{
		return BoundsMax;
	}

	FVector FProxyPoint::GetExtents() const
	{
		return PCGPointHelpers::GetExtents(BoundsMin, BoundsMax);
	}

	FVector FProxyPoint::GetScaledExtents() const
	{
		return PCGPointHelpers::GetScaledExtents(Transform, BoundsMin, BoundsMax);
	}

	FBox FProxyPoint::GetLocalBounds() const
	{
		return FBox(BoundsMin, BoundsMax);
	}

	FBox FProxyPoint::GetScaledBounds() const
	{
		return FBox(BoundsMin * Transform.GetScale3D(), BoundsMax * Transform.GetScale3D());
	}

	FBox FProxyPoint::GetLocalDensityBounds() const
	{
		return PCGPointHelpers::GetLocalDensityBounds(Steepness, BoundsMin, BoundsMax);
	}

	FVector4 FProxyPoint::GetColor() const
	{
		return Color;
	}

	FVector FProxyPoint::GetLocalSize() const
	{
		return PCGPointHelpers::GetLocalSize(BoundsMin, BoundsMax);
	}

	FVector FProxyPoint::GetScaledLocalSize() const
	{
		return PCGPointHelpers::GetScaledLocalSize(Transform, BoundsMin, BoundsMax);
	}

	// FProxyPoint setters
	void FProxyPoint::SetTransform(const FTransform& InValue)
	{
		Transform = InValue;
	}

	void FProxyPoint::SetLocation(const FVector& InValue)
	{
		Transform.SetLocation(InValue);
	}

	void FProxyPoint::SetScale3D(const FVector& InValue)
	{
		Transform.SetScale3D(InValue);
	}

	void FProxyPoint::SetQuat(const FQuat& InValue)
	{
		Transform.SetRotation(InValue);
	}

	void FProxyPoint::SetBoundsMin(const FVector& InValue)
	{
		BoundsMin = InValue;
	}

	void FProxyPoint::SetBoundsMax(const FVector& InValue)
	{
		BoundsMax = InValue;
	}

	void FProxyPoint::SetExtents(const FVector& InValue, const bool bKeepLocalCenter)
	{
		if (bKeepLocalCenter)
		{
			const FVector LocalCenter = PCGPointHelpers::GetLocalCenter(BoundsMin, BoundsMax);
			BoundsMin = LocalCenter - InValue;
			BoundsMax = LocalCenter + InValue;
		}
		else
		{
			BoundsMin = -InValue;
			BoundsMax = InValue;
		}
	}

	void FProxyPoint::SetLocalBounds(const FBox& InValue)
	{
		BoundsMin = InValue.Min;
		BoundsMax = InValue.Max;
	}

	void FProxyPoint::CopyTo(UPCGBasePointData* InData) const
	{
		InData->GetTransformValueRange(false)[Index] = Transform;
		InData->GetBoundsMinValueRange(false)[Index] = BoundsMin;
		InData->GetBoundsMaxValueRange(false)[Index] = BoundsMax;
		InData->GetColorValueRange(false)[Index] = Color;
	}

	void FProxyPoint::CopyTo(FMutablePoint& InPoint) const
	{
		InPoint.SetTransform(Transform);
		InPoint.SetBoundsMin(BoundsMin);
		InPoint.SetBoundsMax(BoundsMax);
		InPoint.SetColor(Color);
	}

#pragma endregion
}
