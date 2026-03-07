// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExProxyDataImpl.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGPointArrayData.h"

namespace PCGExData
{
#pragma region TRawBufferProxy

	template <typename T_REAL>
	TRawBufferProxy<T_REAL>::TRawBufferProxy(EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExTypes::TTraits<T_REAL>::Type, InWorkingType)
	{
	}

	template <typename T_REAL>
	void TRawBufferProxy<T_REAL>::GetVoid(const int32 Index, void* OutValue) const
	{
		check(Buffer);

		T_REAL* RealValue = (Buffer->GetData() + Index);

		// Use direct conversion function
		if (RealType != WorkingType) { RealToWorking(RealValue, OutValue); }
		// Same type - direct get
		else { *static_cast<T_REAL*>(OutValue) = *RealValue; }
	}

	template <typename T_REAL>
	void TRawBufferProxy<T_REAL>::SetVoid(const int32 Index, const void* Value) const
	{
		check(Buffer);

		// Use direct conversion function
		if (RealType != WorkingType) { WorkingToReal(Value, (Buffer->GetData() + Index)); }
		// Same type - direct set
		else { *(Buffer->GetData() + Index) = *static_cast<const T_REAL*>(Value); }
	}

	template <typename T_REAL>
	PCGExValueHash TRawBufferProxy<T_REAL>::ReadValueHash(const int32 Index) const
	{
		return PCGExTypeOps::FTypeOps<T_REAL>::Hash(*(Buffer->GetData() + Index));
	}

	// Explicit instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) template class TRawBufferProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region TAttributeBufferProxy

	template <typename T_REAL>
	TAttributeBufferProxy<T_REAL>::TAttributeBufferProxy(EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExTypes::TTraits<T_REAL>::Type, InWorkingType)
	{
	}

	template <typename T_REAL>
	void TAttributeBufferProxy<T_REAL>::GetVoid(const int32 Index, void* OutValue) const
	{
		check(Buffer);
		const T_REAL& RealValue = Buffer->Read(Index);

		if (bWantsSubSelection)
		{
			// Use type-erased sub-selection extraction
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			// Use direct conversion function
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	void TAttributeBufferProxy<T_REAL>::SetVoid(const int32 Index, const void* Value) const
	{
		check(Buffer);

		if (bWantsSubSelection)
		{
			// Read current value, apply sub-selection, write back
			T_REAL RealValue = Buffer->GetValue(Index);
			CachedSubSelection.ApplySet(&RealValue, Value);
			Buffer->SetValue(Index, RealValue);
		}
		else if (RealType != WorkingType)
		{
			// Use direct conversion function
			T_REAL RealValue{};
			WorkingToReal(Value, &RealValue);
			Buffer->SetValue(Index, RealValue);
		}
		else
		{
			// Same type - direct set
			Buffer->SetValue(Index, *static_cast<const T_REAL*>(Value));
		}
	}

	template <typename T_REAL>
	void TAttributeBufferProxy<T_REAL>::GetCurrentVoid(const int32 Index, void* OutValue) const
	{
		check(Buffer);
		const T_REAL& RealValue = Buffer->GetValue(Index);

		if (bWantsSubSelection)
		{
			// Use type-erased sub-selection extraction
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	TSharedPtr<IBuffer> TAttributeBufferProxy<T_REAL>::GetBuffer() const
	{
		return Buffer;
	}

	template <typename T_REAL>
	bool TAttributeBufferProxy<T_REAL>::EnsureReadable() const
	{
		return Buffer && Buffer->EnsureReadable();
	}

	template <typename T_REAL>
	PCGExValueHash TAttributeBufferProxy<T_REAL>::ReadValueHash(const int32 Index) const
	{
		check(Buffer);
		return Buffer->ReadValueHash(Index);
	}

	// Explicit instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) template class TAttributeBufferProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region FPointPropertyProxy

	FPointPropertyProxy::FPointPropertyProxy(EPCGPointProperties InProperty, EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExMetaHelpers::GetPropertyType(InProperty), InWorkingType)
		  , Property(InProperty)
		  , PropertyRealType(PCGExMetaHelpers::GetPropertyType(InProperty))
	{
	}

	void FPointPropertyProxy::GetPropertyValue(const int32 Index, void* OutValue) const
	{
		check(Data);
		const FConstPoint Point(Data, Index);

		switch (Property)
		{
		case EPCGPointProperties::Density: *static_cast<float*>(OutValue) = Point.GetDensity();
			break;
		case EPCGPointProperties::BoundsMin: *static_cast<FVector*>(OutValue) = Point.GetBoundsMin();
			break;
		case EPCGPointProperties::BoundsMax: *static_cast<FVector*>(OutValue) = Point.GetBoundsMax();
			break;
		case EPCGPointProperties::Extents: *static_cast<FVector*>(OutValue) = Point.GetExtents();
			break;
		case EPCGPointProperties::Color: *static_cast<FVector4*>(OutValue) = Point.GetColor();
			break;
		case EPCGPointProperties::Position: *static_cast<FVector*>(OutValue) = Point.GetLocation();
			break;
		case EPCGPointProperties::Rotation: *static_cast<FQuat*>(OutValue) = Point.GetRotation();
			break;
		case EPCGPointProperties::Scale: *static_cast<FVector*>(OutValue) = Point.GetScale3D();
			break;
		case EPCGPointProperties::Transform: *static_cast<FTransform*>(OutValue) = Point.GetTransform();
			break;
		case EPCGPointProperties::Steepness: *static_cast<float*>(OutValue) = Point.GetSteepness();
			break;
		case EPCGPointProperties::LocalCenter: *static_cast<FVector*>(OutValue) = Point.GetLocalCenter();
			break;
		case EPCGPointProperties::LocalSize: *static_cast<FVector*>(OutValue) = Point.GetLocalSize();
			break;
		case EPCGPointProperties::ScaledLocalSize: *static_cast<FVector*>(OutValue) = Point.GetScaledLocalSize();
			break;
		case EPCGPointProperties::Seed: *static_cast<int32*>(OutValue) = Point.GetSeed();
			break;
		default: break;
		}
	}

	void FPointPropertyProxy::SetPropertyValue(const int32 Index, const void* Value) const
	{
		check(Data);
		FMutablePoint Point(Data, Index);

		switch (Property)
		{
		case EPCGPointProperties::Density: Point.SetDensity(*static_cast<const float*>(Value));
			break;
		case EPCGPointProperties::BoundsMin: Point.SetBoundsMin(*static_cast<const FVector*>(Value));
			break;
		case EPCGPointProperties::BoundsMax: Point.SetBoundsMax(*static_cast<const FVector*>(Value));
			break;
		case EPCGPointProperties::Extents: Point.SetExtents(*static_cast<const FVector*>(Value));
			break;
		case EPCGPointProperties::Color: Point.SetColor(*static_cast<const FVector4*>(Value));
			break;
		case EPCGPointProperties::Position: Point.SetLocation(*static_cast<const FVector*>(Value));
			break;
		case EPCGPointProperties::Rotation: Point.SetRotation(*static_cast<const FQuat*>(Value));
			break;
		case EPCGPointProperties::Scale: Point.SetScale3D(*static_cast<const FVector*>(Value));
			break;
		case EPCGPointProperties::Transform: Point.SetTransform(*static_cast<const FTransform*>(Value));
			break;
		case EPCGPointProperties::Steepness: Point.SetSteepness(*static_cast<const float*>(Value));
			break;
		case EPCGPointProperties::Seed: Point.SetSeed(*static_cast<const int32*>(Value));
			break;
		default: break;
		}
	}

	void FPointPropertyProxy::GetVoid(const int32 Index, void* OutValue) const
	{
		if (bWantsSubSelection)
		{
			// Need temporary storage for the property value
			PCGExTypes::FScopedTypedValue PropValue(PropertyRealType);
			GetPropertyValue(Index, PropValue.GetRaw());
			CachedSubSelection.ApplyGet(PropValue.GetRaw(), OutValue);
		}
		else if (PropertyRealType != WorkingType)
		{
			PCGExTypes::FScopedTypedValue PropValue(PropertyRealType);
			GetPropertyValue(Index, PropValue.GetRaw());
			RealToWorking(PropValue.GetRaw(), OutValue);
		}
		else
		{
			GetPropertyValue(Index, OutValue);
		}
	}

	void FPointPropertyProxy::SetVoid(const int32 Index, const void* Value) const
	{
		if (bWantsSubSelection)
		{
			PCGExTypes::FScopedTypedValue PropValue(PropertyRealType);
			GetPropertyValue(Index, PropValue.GetRaw());
			CachedSubSelection.ApplySet(PropValue.GetRaw(), Value);
			SetPropertyValue(Index, PropValue.GetRaw());
		}
		else if (PropertyRealType != WorkingType)
		{
			PCGExTypes::FScopedTypedValue PropValue(PropertyRealType);
			WorkingToReal(Value, PropValue.GetRaw());
			SetPropertyValue(Index, PropValue.GetRaw());
		}
		else
		{
			SetPropertyValue(Index, Value);
		}
	}

	void FPointPropertyProxy::InitForRole(EProxyRole InRole)
	{
		if (InRole == EProxyRole::Write && Data)
		{
			// Allocate property for writing
			if (UPCGPointArrayData* PointData = Cast<UPCGPointArrayData>(Data))
			{
				PointData->AllocateProperties(PCGExMetaHelpers::GetPropertyNativeTypes(Property));
			}
		}
	}

	PCGExValueHash FPointPropertyProxy::ReadValueHash(const int32 Index) const
	{
		PCGExTypes::FScopedTypedValue PropValue(PropertyRealType);
		GetPropertyValue(Index, PropValue.GetRaw());
		return RealOps->ComputeHash(PropValue.GetRaw());
	}

#pragma endregion

#pragma region FPointExtraPropertyProxy

	FPointExtraPropertyProxy::FPointExtraPropertyProxy(EPCGExtraProperties InProperty, EPCGMetadataTypes InWorkingType)
		: IBufferProxy(GetPropertyType(InProperty), InWorkingType)
		  , Property(InProperty)
	{
	}

	EPCGMetadataTypes FPointExtraPropertyProxy::GetPropertyType(EPCGExtraProperties InProperty)
	{
		switch (InProperty)
		{
		case EPCGExtraProperties::Index: return EPCGMetadataTypes::Integer32;
		default: return EPCGMetadataTypes::Unknown;
		}
	}

	void FPointExtraPropertyProxy::GetVoid(const int32 Index, void* OutValue) const
	{
		int32 Value = 0;
		switch (Property)
		{
		case EPCGExtraProperties::Index: Value = Index;
			break;
		default: break;
		}

		if (WorkingType == EPCGMetadataTypes::Integer32)
		{
			*static_cast<int32*>(OutValue) = Value;
		}
		else
		{
			// Convert int32 to working type
			const PCGExTypeOps::ITypeOpsBase* Int32Ops = PCGExTypeOps::FTypeOpsRegistry::Get<int32>();
			if (Int32Ops)
			{
				Int32Ops->ConvertTo(&Value, WorkingType, OutValue);
			}
		}
	}

	PCGExValueHash FPointExtraPropertyProxy::ReadValueHash(const int32 Index) const
	{
		int32 Value = 0;
		switch (Property)
		{
		case EPCGExtraProperties::Index: Value = Index;
			break;
		default: break;
		}
		return PCGExTypes::ComputeHash(Value);
	}

#pragma endregion

#pragma region FPropertyBufferProxy

	FPropertyBufferProxy::FPropertyBufferProxy(int32 InElementSize, int32 InElementAlignment)
		: IBufferProxy(EPCGMetadataTypes::Unknown, EPCGMetadataTypes::Unknown)
		  , ElementSize(InElementSize)
		  , ElementAlignment(InElementAlignment)
	{
	}

	void FPropertyBufferProxy::GetVoid(const int32 Index, void* OutValue) const
	{
		check(Buffer);
		Buffer->ReadVoid(Index, OutValue);
	}

	void FPropertyBufferProxy::SetVoid(const int32 Index, const void* Value) const
	{
		check(Buffer);
		// const_cast needed: IBuffer::SetVoid is non-const, but IBufferProxy::SetVoid is const.
		const_cast<IBuffer*>(Buffer.Get())->SetVoid(Index, Value);
	}

	void FPropertyBufferProxy::GetCurrentVoid(const int32 Index, void* OutValue) const
	{
		check(Buffer);
		// GetVoid reads from the output side (current working value)
		const_cast<IBuffer*>(Buffer.Get())->GetVoid(Index, OutValue);
	}

	TSharedPtr<IBuffer> FPropertyBufferProxy::GetBuffer() const
	{
		return Buffer;
	}

	bool FPropertyBufferProxy::EnsureReadable() const
	{
		return Buffer && Buffer->EnsureReadable();
	}

	PCGExValueHash FPropertyBufferProxy::ReadValueHash(const int32 Index) const
	{
		check(Buffer);
		return Buffer->ReadValueHash(Index);
	}

	int32 FPropertyBufferProxy::GetValueSize() const
	{
		return ElementSize;
	}

	int32 FPropertyBufferProxy::GetValueAlignment() const
	{
		return ElementAlignment;
	}

#pragma endregion

#pragma region TConstantProxy

	template <typename T_CONST>
	TConstantProxy<T_CONST>::TConstantProxy(EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExTypes::TTraits<T_CONST>::Type, InWorkingType)
	{
	}

	template <typename T_CONST>
	template <typename T>
	void TConstantProxy<T_CONST>::SetConstant(const T& InValue)
	{
		constexpr EPCGMetadataTypes SourceType = PCGExTypes::TTraits<T>::Type;
		constexpr EPCGMetadataTypes ConstType = PCGExTypes::TTraits<T_CONST>::Type;

		if constexpr (std::is_same_v<T, T_CONST>)
		{
			Constant = InValue;
		}
		else
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, &InValue, ConstType, &Constant);
		}
	}

	template <typename T_CONST>
	void TConstantProxy<T_CONST>::GetVoid(const int32 Index, void* OutValue) const
	{
		if (bWantsSubSelection)
		{
			// Use type-erased sub-selection extraction
			CachedSubSelection.ApplyGet(&Constant, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&Constant, OutValue);
		}
		else
		{
			*static_cast<T_CONST*>(OutValue) = Constant;
		}
	}

	template <typename T_CONST>
	bool TConstantProxy<T_CONST>::Validate(const FProxyDescriptor& InDescriptor) const
	{
		// Constants are more flexible with type matching
		return true;
	}

	template <typename T_CONST>
	PCGExValueHash TConstantProxy<T_CONST>::ReadValueHash(const int32 Index) const
	{
		return PCGExTypes::ComputeHash(Constant);
	}

	// Explicit instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) template class TConstantProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	// SetConstant cross-type instantiations
#define PCGEX_TPL(_TYPE_A, _NAME_A, _TYPE_B, _NAME_B, ...) \
	template void TConstantProxy<_TYPE_A>::SetConstant<_TYPE_B>(const _TYPE_B&);
	PCGEX_FOREACH_SUPPORTEDTYPES_PAIRS(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region TDirectAttributeProxy

	template <typename T_REAL>
	TDirectAttributeProxy<T_REAL>::TDirectAttributeProxy(EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExTypes::TTraits<T_REAL>::Type, InWorkingType)
	{
	}

	template <typename T_REAL>
	void TDirectAttributeProxy<T_REAL>::GetVoid(const int32 Index, void* OutValue) const
	{
		check(InAttribute && Data);

		const T_REAL& RealValue = InAttribute->GetValueFromItemKey(Data->GetMetadataEntry(Index));

		if (bWantsSubSelection)
		{
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	void TDirectAttributeProxy<T_REAL>::GetCurrentVoid(const int32 Index, void* OutValue) const
	{
		check(OutAttribute && Data);

		const T_REAL& RealValue = OutAttribute->GetValueFromItemKey(Data->GetMetadataEntry(Index));

		if (bWantsSubSelection)
		{
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	void TDirectAttributeProxy<T_REAL>::SetVoid(const int32 Index, const void* Value) const
	{
		check(OutAttribute && Data);

		const int64 MetadataEntry = Data->GetMetadataEntry(Index);

		if (bWantsSubSelection)
		{
			T_REAL RealValue = OutAttribute->GetValueFromItemKey(MetadataEntry);
			CachedSubSelection.ApplySet(&RealValue, Value);
			OutAttribute->SetValue(MetadataEntry, RealValue);
		}
		else if (RealType != WorkingType)
		{
			T_REAL RealValue{};
			WorkingToReal(Value, &RealValue);
			OutAttribute->SetValue(MetadataEntry, RealValue);
		}
		else
		{
			OutAttribute->SetValue(MetadataEntry, *static_cast<const T_REAL*>(Value));
		}
	}

	template <typename T_REAL>
	PCGExValueHash TDirectAttributeProxy<T_REAL>::ReadValueHash(const int32 Index) const
	{
		check(InAttribute && Data);
		return PCGExTypes::ComputeHash(InAttribute->GetValueFromItemKey(Data->GetMetadataEntry(Index)));
	}

	// Explicit instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) template class TDirectAttributeProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region TDirectDataAttributeProxy

	template <typename T_REAL>
	TDirectDataAttributeProxy<T_REAL>::TDirectDataAttributeProxy(EPCGMetadataTypes InWorkingType)
		: IBufferProxy(PCGExTypes::TTraits<T_REAL>::Type, InWorkingType)
	{
	}

	template <typename T_REAL>
	void TDirectDataAttributeProxy<T_REAL>::GetVoid(const int32 Index, void* OutValue) const
	{
		check(InAttribute);

		// Data-domain: always use default entry key
		const T_REAL& RealValue = InAttribute->GetValueFromItemKey(PCGDefaultValueKey);

		if (bWantsSubSelection)
		{
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	void TDirectDataAttributeProxy<T_REAL>::GetCurrentVoid(const int32 Index, void* OutValue) const
	{
		check(OutAttribute);

		const T_REAL& RealValue = OutAttribute->GetValueFromItemKey(PCGDefaultValueKey);

		if (bWantsSubSelection)
		{
			CachedSubSelection.ApplyGet(&RealValue, OutValue);
		}
		else if (RealType != WorkingType)
		{
			RealToWorking(&RealValue, OutValue);
		}
		else
		{
			*static_cast<T_REAL*>(OutValue) = RealValue;
		}
	}

	template <typename T_REAL>
	void TDirectDataAttributeProxy<T_REAL>::SetVoid(const int32 Index, const void* Value) const
	{
		check(OutAttribute);

		if (bWantsSubSelection)
		{
			T_REAL RealValue = OutAttribute->GetValueFromItemKey(PCGDefaultValueKey);
			CachedSubSelection.ApplySet(&RealValue, Value);
			OutAttribute->SetDefaultValue(RealValue);
		}
		else if (RealType != WorkingType)
		{
			T_REAL RealValue{};
			WorkingToReal(Value, &RealValue);
			OutAttribute->SetDefaultValue(RealValue);
		}
		else
		{
			OutAttribute->SetDefaultValue(*static_cast<const T_REAL*>(Value));
		}
	}

	template <typename T_REAL>
	PCGExValueHash TDirectDataAttributeProxy<T_REAL>::ReadValueHash(const int32 Index) const
	{
		check(InAttribute);
		return PCGExTypes::ComputeHash(InAttribute->GetValueFromItemKey(PCGDefaultValueKey));
	}

	// Explicit instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) template class TDirectDataAttributeProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion
}
