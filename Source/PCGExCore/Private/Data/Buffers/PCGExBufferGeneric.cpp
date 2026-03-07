// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExData.h"

#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeGeneric.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Types/PCGExTypes.h"

namespace PCGExData
{
#pragma region TGenericArrayBuffer

	template <typename T>
	TGenericArrayBuffer<T>::TGenericArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TGenericBuffer<T>(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag != EPCGMetadataDomainFlag::Data)
		this->UnderlyingDomain = EDomainType::Elements;
	}

	template <typename T>
	int32 TGenericArrayBuffer<T>::GetNumValues(const EIOSide InSide)
	{
		if (InSide == EIOSide::In) { return InValues ? InValues->Num() : -1; }
		return OutValues ? OutValues->Num() : -1;
	}

	template <typename T>
	bool TGenericArrayBuffer<T>::IsWritable() { return OutValues ? true : false; }

	template <typename T>
	bool TGenericArrayBuffer<T>::IsReadable() { return InValues ? true : false; }

	template <typename T>
	bool TGenericArrayBuffer<T>::ReadsFromOutput() { return InValues == OutValues; }

	template <typename T>
	bool TGenericArrayBuffer<T>::EnsureReadable()
	{
		if (InValues) { return true; }
		InValues = OutValues;
		return InValues ? true : false;
	}

	template <typename T>
	const T& TGenericArrayBuffer<T>::Read(const int32 Index) const { return *(InValues->GetData() + Index); }

	template <typename T>
	const void TGenericArrayBuffer<T>::Read(const int32 Start, TArrayView<T> OutResults) const
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = *(InValues->GetData() + (Start + i)); }
	}

	template <typename T>
	const T& TGenericArrayBuffer<T>::GetValue(const int32 Index) { return *(OutValues->GetData() + Index); }

	template <typename T>
	const void TGenericArrayBuffer<T>::GetValues(const int32 Start, TArrayView<T> OutResults)
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = *(OutValues->GetData() + (Start + i)); }
	}

	template <typename T>
	void TGenericArrayBuffer<T>::SetValue(const int32 Index, const T& Value) { *(OutValues->GetData() + Index) = Value; }

	template <typename T>
	PCGExValueHash TGenericArrayBuffer<T>::ReadValueHash(const int32 Index) { return PCGExTypes::ComputeHash(Read(Index)); }

	template <typename T>
	PCGExValueHash TGenericArrayBuffer<T>::GetValueHash(const int32 Index) { return PCGExTypes::ComputeHash(GetValue(Index)); }

	template <typename T>
	bool TGenericArrayBuffer<T>::InitForRead(const EIOSide InSide, const bool bScoped)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (InValues) { return true; }

		if (InSide == EIOSide::Out)
		{
			check(OutValues)
			InValues = OutValues;
			return true;
		}

		const UPCGBasePointData* PointData = Source->GetIn();
		if (!PointData) { return false; }

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr || !Attr->IsGeneric()) { return false; }

		GenericInAttribute = static_cast<const FPCGMetadataAttributeGeneric*>(Attr);
		InAttribute = Attr;

		const int32 NumPoints = PointData->GetNumPoints();
		InValues = MakeShared<TArray<T>>();
		PCGExArrayHelpers::InitArray(InValues, NumPoints);

		TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(GenericInAttribute, GenericInAttribute->GetMetadataDomain());
		if (InAccessor.IsValid())
		{
			TArrayView<T> InRange = MakeArrayView(InValues->GetData(), InValues->Num());
			InAccessor->GetRange<T>(InRange, 0, *Source->GetInKeys());
		}

		bReadComplete = true;
		return true;
	}

	template <typename T>
	bool TGenericArrayBuffer<T>::InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax, const bool bScoped, const bool bQuiet)
	{
		return false; // Not supported for generic attributes
	}

	template <typename T>
	bool TGenericArrayBuffer<T>::InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (OutValues) { return true; }

		UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData) { return false; }

		FPCGMetadataDomain* Domain = OutData->Metadata->GetMetadataDomain(Identifier.MetadataDomain);
		if (!Domain) { Domain = OutData->Metadata->GetDefaultMetadataDomain(); }

		this->bIsNewOutput = !PCGExMetaHelpers::HasAttribute(OutData, Identifier);

		// UE 5.8 migration: CreateGenericAttribute<T> through FPCGMetadataDomain is the
		// sanctioned creation path. When typed attrs unify with generic internally,
		// this call should remain stable. If Epic deprecates it, switch to
		// Domain->CreateAttribute<T>() which will route to the same generic backend.
		GenericOutAttribute = this->bIsNewOutput
			? Domain->template CreateGenericAttribute<T>(Identifier.Name, DefaultValue, bAllowInterpolation)
			: Domain->GetMutableGenericAttribute(Identifier.Name);

		if (!GenericOutAttribute)
		{
			GenericOutAttribute = Domain->template CreateGenericAttribute<T>(Identifier.Name, DefaultValue, bAllowInterpolation);
		}

		if (!GenericOutAttribute) { return false; }

		OutAttribute = GenericOutAttribute;

		OutValues = MakeShared<TArray<T>>();
		OutValues->Init(DefaultValue, OutData->GetNumPoints());

		if (Init == EBufferInit::Inherit)
		{
			// UE 5.8 migration: accessor API will be updated to match generic unification.
			// CreateAccessor already handles IsGeneric() internally — should adapt automatically.
			TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(GenericOutAttribute, const_cast<FPCGMetadataDomain*>(GenericOutAttribute->GetMetadataDomain()));
			if (OutAccessor.IsValid())
			{
				TUniquePtr<FPCGAttributeAccessorKeysPointIndices> TempOutKeys = MakeUnique<FPCGAttributeAccessorKeysPointIndices>(OutData, false);
				TArrayView<T> OutRange = MakeArrayView(OutValues->GetData(), OutValues->Num());
				OutAccessor->template GetRange<T>(OutRange, 0, *TempOutKeys.Get());
			}
		}

		return true;
	}

	template <typename T>
	bool TGenericArrayBuffer<T>::InitForWrite(const EBufferInit Init)
	{
		return InitForWrite(T{}, true, Init);
	}

	template <typename T>
	void TGenericArrayBuffer<T>::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TGenericArrayBuffer::Write);

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())

		if (!IsWritable() || !OutValues || !IsEnabled()) { return; }
		if (!Source->GetOut() || !GenericOutAttribute) { return; }

		// UE 5.8 migration: accessor SetRange is the bulk write path.
		// When Epic updates the accessor API, this should work as-is since
		// CreateAccessor already handles generic attributes.
		TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(GenericOutAttribute, const_cast<FPCGMetadataDomain*>(GenericOutAttribute->GetMetadataDomain()));
		if (!OutAccessor.IsValid()) { return; }

		SharedContext.Get()->AddProtectedAttributeName(GenericOutAttribute->Name);

		TArrayView<const T> View = MakeArrayView(OutValues->GetData(), OutValues->Num());
		OutAccessor->SetRange<T>(View, 0, *Source->GetOutKeys(bEnsureValidKeys).Get());
	}

	template <typename T>
	void TGenericArrayBuffer<T>::Flush()
	{
		InValues.Reset();
		OutValues.Reset();
	}

#pragma endregion

#pragma region TGenericSingleValueBuffer

	template <typename T>
	TGenericSingleValueBuffer<T>::TGenericSingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TGenericBuffer<T>(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
		this->UnderlyingDomain = EDomainType::Data;
	}

	template <typename T>
	int32 TGenericSingleValueBuffer<T>::GetNumValues(const EIOSide InSide) { return 1; }

	template <typename T>
	bool TGenericSingleValueBuffer<T>::IsWritable() { return bWriteInitialized; }

	template <typename T>
	bool TGenericSingleValueBuffer<T>::IsReadable() { return bReadInitialized; }

	template <typename T>
	bool TGenericSingleValueBuffer<T>::ReadsFromOutput() { return bReadFromOutput; }

	template <typename T>
	bool TGenericSingleValueBuffer<T>::EnsureReadable()
	{
		if (bReadInitialized) { return true; }
		InValue = OutValue;
		bReadFromOutput = true;
		bReadInitialized = bWriteInitialized;
		return bReadInitialized;
	}

	template <typename T>
	const T& TGenericSingleValueBuffer<T>::Read(const int32 Index) const { return InValue; }

	template <typename T>
	const void TGenericSingleValueBuffer<T>::Read(const int32 Start, TArrayView<T> OutResults) const
	{
		for (int i = 0; i < OutResults.Num(); i++) { OutResults[i] = InValue; }
	}

	template <typename T>
	const T& TGenericSingleValueBuffer<T>::GetValue(const int32 Index)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		return OutValue;
	}

	template <typename T>
	const void TGenericSingleValueBuffer<T>::GetValues(const int32 Start, TArrayView<T> OutResults)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		for (int i = 0; i < OutResults.Num(); i++) { OutResults[i] = OutValue; }
	}

	template <typename T>
	void TGenericSingleValueBuffer<T>::SetValue(const int32 Index, const T& Value)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);
		OutValue = Value;
		if (bReadFromOutput) { InValue = Value; }
	}

	template <typename T>
	bool TGenericSingleValueBuffer<T>::InitForRead(const EIOSide InSide, const bool bScoped)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized) { return true; }

		if (InSide == EIOSide::Out)
		{
			check(bWriteInitialized)
			bReadInitialized = bReadFromOutput = true;
			InValue = OutValue;
			return true;
		}

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr || !Attr->IsGeneric()) { return false; }

		GenericInAttribute = static_cast<const FPCGMetadataAttributeGeneric*>(Attr);
		InAttribute = Attr;

		InValue = GenericInAttribute->template GetValueFromItemKey<T>(PCGDefaultValueKey);
		bReadInitialized = true;

		return true;
	}

	template <typename T>
	bool TGenericSingleValueBuffer<T>::InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax, const bool bScoped, const bool bQuiet)
	{
		return false; // Not supported for generic attributes
	}

	template <typename T>
	bool TGenericSingleValueBuffer<T>::InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bWriteInitialized) { return true; }

		UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData) { return false; }

		FPCGMetadataDomain* Domain = OutData->Metadata->GetMetadataDomain(Identifier.MetadataDomain);
		if (!Domain) { Domain = OutData->Metadata->GetDefaultMetadataDomain(); }

		this->bIsNewOutput = !PCGExMetaHelpers::HasAttribute(OutData, Identifier);

		// UE 5.8 migration: same note as TGenericArrayBuffer — CreateGenericAttribute<T>
		// through FPCGMetadataDomain is the sanctioned path. Stable across the unification.
		GenericOutAttribute = this->bIsNewOutput
			? Domain->template CreateGenericAttribute<T>(Identifier.Name, DefaultValue, bAllowInterpolation)
			: Domain->GetMutableGenericAttribute(Identifier.Name);

		if (!GenericOutAttribute)
		{
			GenericOutAttribute = Domain->template CreateGenericAttribute<T>(Identifier.Name, DefaultValue, bAllowInterpolation);
		}

		if (!GenericOutAttribute) { return false; }

		OutAttribute = GenericOutAttribute;
		bWriteInitialized = true;
		OutValue = DefaultValue;

		if (Init == EBufferInit::Inherit)
		{
			// UE 5.8 migration: GetValueFromItemKey<T> is the template read path on
			// FPCGMetadataAttributeGeneric — stable across the unification.
			OutValue = GenericOutAttribute->template GetValueFromItemKey<T>(PCGDefaultValueKey);
		}

		return true;
	}

	template <typename T>
	bool TGenericSingleValueBuffer<T>::InitForWrite(const EBufferInit Init)
	{
		return InitForWrite(T{}, true, Init);
	}

	template <typename T>
	void TGenericSingleValueBuffer<T>::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TGenericSingleValueBuffer::Write);

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())

		if (!IsWritable() || !IsEnabled()) { return; }
		if (!Source->GetOut() || !GenericOutAttribute) { return; }

		// UE 5.8 migration: SetValue<T> is the template write path on
		// FPCGMetadataAttributeGeneric. This is the future canonical write API.
		GenericOutAttribute->template SetValue<T>(PCGDefaultValueKey, OutValue);
	}

#pragma endregion

#pragma region Externalization

	// Standard type instantiations — matches PCGEX_FOREACH_SUPPORTEDTYPES
#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TGenericArrayBuffer<_TYPE>;\
template class PCGEXCORE_API TGenericSingleValueBuffer<_TYPE>;

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

	// ── Container type instantiations ──
	// Add explicit instantiations here when callers need container buffers.
	// Example:
	//   template class PCGEXCORE_API TGenericArrayBuffer<TArray<int32>>;
	//   template class PCGEXCORE_API TGenericSingleValueBuffer<TArray<int32>>;
	//
	// Remember: container read types differ from write types.
	// Callers should use GetGenericInAttribute()->GetValuesFromItemKeys<ReadType>()
	// for bulk reads. See PCGExBufferGeneric.h header comments for details.

#pragma endregion
}
