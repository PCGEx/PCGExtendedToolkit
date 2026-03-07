// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExData.h"

#include "PCGExCoreSettingsCache.h"

#include "PCGExH.h"
#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Types/PCGExTypes.h"

namespace PCGExData
{
#pragma region TArrayBuffer

	template <typename T>
	TArrayBuffer<T>::TArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TLegacyBuffer<T>(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag != EPCGMetadataDomainFlag::Data)
		this->UnderlyingDomain = EDomainType::Elements;
	}

	template <typename T>
	TSharedPtr<TArray<T>> TArrayBuffer<T>::GetInValues() { return InValues; }

	template <typename T>
	TSharedPtr<TArray<T>> TArrayBuffer<T>::GetOutValues() { return OutValues; }

	template <typename T>
	int32 TArrayBuffer<T>::GetNumValues(const EIOSide InSide)
	{
		if (InSide == EIOSide::In)
		{
			return InValues ? InValues->Num() : -1;
		}
		return OutValues ? OutValues->Num() : -1;
	}

	template <typename T>
	bool TArrayBuffer<T>::IsWritable()
	{
		return OutValues ? true : false;
	}

	template <typename T>
	bool TArrayBuffer<T>::IsReadable()
	{
		return InValues ? true : false;
	}

	template <typename T>
	bool TArrayBuffer<T>::ReadsFromOutput() { return InValues == OutValues; }

	template <typename T>
	const T& TArrayBuffer<T>::Read(const int32 Index) const { return *(InValues->GetData() + Index); }

	template <typename T>
	const void TArrayBuffer<T>::Read(const int32 Start, TArrayView<T> OutResults) const
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = *(InValues->GetData() + (Start + i)); }
	}

	template <typename T>
	const T& TArrayBuffer<T>::GetValue(const int32 Index) { return *(OutValues->GetData() + Index); }

	template <typename T>
	const void TArrayBuffer<T>::GetValues(const int32 Start, TArrayView<T> OutResults)
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = *(OutValues->GetData() + (Start + i)); }
	}

	template <typename T>
	void TArrayBuffer<T>::SetValue(const int32 Index, const T& Value) { *(OutValues->GetData() + Index) = Value; }

	template <typename T>
	PCGExValueHash TArrayBuffer<T>::ReadValueHash(const int32 Index)
	{
		if (bCacheValueHashes) { return InHashes[Index]; }
		return PCGExTypes::ComputeHash(Read(Index));
	}

	template <typename T>
	void TArrayBuffer<T>::ComputeValueHashes(const PCGExMT::FScope& Scope)
	{
		const TArray<T>& InValuesRef = *InValues.Get();
		PCGEX_SCOPE_LOOP(Index) { InHashes[Index] = PCGExTypes::ComputeHash(InValuesRef[Index]); }
	}

	template <typename T>
	void TArrayBuffer<T>::InitForReadInternal(const bool bScoped, const FPCGMetadataAttributeBase* Attribute)
	{
		if (InValues) { return; }

		const int32 NumReadValue = Source->GetIn()->GetNumPoints();
		InValues = MakeShared<TArray<T>>();
		PCGExArrayHelpers::InitArray(InValues, NumReadValue);

		if (bCacheValueHashes) { InHashes.Init(0, NumReadValue); }

		InAttribute = Attribute;
		TypedInAttribute = Attribute ? static_cast<const FPCGMetadataAttribute<T>*>(Attribute) : nullptr;

		bSparseBuffer = bScoped;
	}

	template <typename T>
	void TArrayBuffer<T>::InitForWriteInternal(FPCGMetadataAttributeBase* Attribute, const T& InDefaultValue, const EBufferInit Init)
	{
		if (OutValues) { return; }

		OutValues = MakeShared<TArray<T>>();
		OutValues->Init(InDefaultValue, Source->GetOut()->GetNumPoints());

		OutAttribute = Attribute;
		TypedOutAttribute = Attribute ? static_cast<FPCGMetadataAttribute<T>*>(Attribute) : nullptr;
	}

	template <typename T>
	bool TArrayBuffer<T>::EnsureReadable()
	{
		if (InValues) { return true; }
		InValues = OutValues;
		return InValues ? true : false;
	}

	template <typename T>
	void TArrayBuffer<T>::EnableValueHashCache()
	{
		if (bCacheValueHashes) { return; }
		bCacheValueHashes = true;

		if (bReadComplete)
		{
			if (InHashes.Num() != InValues->Num()) { InHashes.Init(0, InValues->Num()); }
			Fetch(PCGExMT::FScope(0, InValues->Num()));
		}
	}

	template <typename T>
	bool TArrayBuffer<T>::InitForRead(const EIOSide InSide, const bool bScoped)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (InValues)
		{
			// "Scoped" buffers defer reading until Fetch() is called with a specific range.
			// If a non-scoped read is requested on an existing sparse buffer, backfill all values now.
			if (bSparseBuffer && !bScoped)
			{
				Fetch(PCGExMT::FScope(0, InValues->Num()));
				bReadComplete = true;
				bSparseBuffer = false;
			}

			if (InSide == EIOSide::In && OutValues && InValues == OutValues)
			{
				check(false)
				// Out-source Reader was created before writer, this is bad?
				InValues = nullptr;
			}
			else
			{
				return true;
			}
		}

		if (InSide == EIOSide::Out)
		{
			// Reading from the output side aliases the output array as the read source,
			// so reads reflect in-progress writes (used for read-modify-write patterns).
			check(OutValues)
			InValues = OutValues;
			return true;
		}

		TypedInAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(Source->GetIn(), Identifier);
		if (!TypedInAttribute)
		{
			return false;
		}

		TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(TypedInAttribute, TypedInAttribute->GetMetadataDomain());

		if (!InAccessor.IsValid())
		{
			TypedInAttribute = nullptr;
			InAccessor = nullptr;
			return false;
		}

		InitForReadInternal(bScoped, TypedInAttribute);

		// Non-scoped buffers bulk-read all values upfront. Scoped buffers leave
		// the array allocated but empty; values are fetched on-demand per scope.
		if (!bSparseBuffer && !bReadComplete)
		{
			TArrayView<T> InRange = MakeArrayView(InValues->GetData(), InValues->Num());
			InAccessor->GetRange<T>(InRange, 0, *Source->GetInKeys());
			bReadComplete = true;
		}

		return true;
	}

	template <typename T>
	bool TArrayBuffer<T>::InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax, const bool bScoped, const bool bQuiet)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (InValues)
		{
			if (bSparseBuffer && !bScoped)
			{
				// Un-scoping reader.
				if (!InternalBroadcaster) { InternalBroadcaster = MakeShared<TAttributeBroadcaster<T>>(); }
				if (!InternalBroadcaster->Prepare(InSelector, Source)) { return false; }

				InternalBroadcaster->GrabAndDump(*InValues, bCaptureMinMax, this->Min, this->Max);
				bReadComplete = true;
				bSparseBuffer = false;
				InternalBroadcaster.Reset();
			}

			if (OutValues && InValues == OutValues)
			{
				check(false)
				// Out-source broadcaster was created before writer, this is bad?
				InValues = nullptr;
			}
			else
			{
				return true;
			}
		}

		InternalBroadcaster = MakeShared<TAttributeBroadcaster<T>>();
		if (!InternalBroadcaster->Prepare(InSelector, Source))
		{
			TypedInAttribute = nullptr;
			return false;
		}

		InitForReadInternal(bScoped, InternalBroadcaster->GetAttribute());

		if (!bSparseBuffer && !bReadComplete)
		{
			InternalBroadcaster->GrabAndDump(*InValues, bCaptureMinMax, this->Min, this->Max);
			bReadComplete = true;
			InternalBroadcaster.Reset();
		}

		return true;
	}

	template <typename T>
	bool TArrayBuffer<T>::InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (OutValues)
		{
			check(OutValues->Num() == Source->GetOut()->GetNumPoints())
			return true;
		}

		this->bIsNewOutput = !PCGExMetaHelpers::HasAttribute(Source->GetOut(), Identifier);

		if (this->bIsNewOutput) { TypedOutAttribute = Source->CreateAttribute(Identifier, DefaultValue, bAllowInterpolation); }
		else { TypedOutAttribute = Source->FindOrCreateAttribute(Identifier, DefaultValue, bAllowInterpolation); }

		if (!TypedOutAttribute) { return false; }

		TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(TypedOutAttribute, const_cast<FPCGMetadataDomain*>(TypedOutAttribute->GetMetadataDomain()));

		if (!TypedOutAttribute || !OutAccessor.IsValid())
		{
			TypedOutAttribute = nullptr;
			return false;
		}

		InitForWriteInternal(TypedOutAttribute, DefaultValue, Init);

		const int32 ExistingEntryCount = TypedOutAttribute->GetNumberOfEntriesWithParents();
		const bool bHasIn = Source->GetIn() ? true : false;

		auto GrabExistingValues = [&]()
		{
			TUniquePtr<FPCGAttributeAccessorKeysPointIndices> TempOutKeys = MakeUnique<FPCGAttributeAccessorKeysPointIndices>(Source->GetOut(), false);
			TArrayView<T> OutRange = MakeArrayView(OutValues->GetData(), OutValues->Num());
			if (!OutAccessor->GetRange<T>(OutRange, 0, *TempOutKeys.Get()))
			{
				// TODO : Log
			}
		};

		if (Init == EBufferInit::Inherit) { GrabExistingValues(); }
		else if (!bHasIn && ExistingEntryCount != 0) { GrabExistingValues(); }

		return true;
	}

	template <typename T>
	bool TArrayBuffer<T>::InitForWrite(const EBufferInit Init)
	{
		{
			FWriteScopeLock WriteScopeLock(BufferLock);
			if (OutValues) { return true; }
		}

		if (const FPCGMetadataAttribute<T>* ExistingAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(Source->GetIn(), Identifier))
		{
			return InitForWrite(ExistingAttribute->GetValue(PCGDefaultValueKey), ExistingAttribute->AllowsInterpolation(), Init);
		}

		return InitForWrite(T{}, true, Init);
	}

	template <typename T>
	void TArrayBuffer<T>::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TBuffer::Write);

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())

		if (!IsWritable() || !OutValues || !IsEnabled()) { return; }

		if (!Source->GetOut())
		{
			UE_LOG(LogPCGEx, Error, TEXT("Attempting to write data to an output that's not initialized!"));
			return;
		}

		if (!TypedOutAttribute) { return; }

		// bResetWithFirstValue: collapse the entire attribute to a single default value.
		// Used for @Data-domain attributes that should carry one value for the whole dataset.
		if (this->bResetWithFirstValue)
		{
			TypedOutAttribute->Reset();
			TypedOutAttribute->SetDefaultValue(*OutValues->GetData());
			return;
		}

		TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(TypedOutAttribute, const_cast<FPCGMetadataDomain*>(TypedOutAttribute->GetMetadataDomain()));
		if (!OutAccessor.IsValid()) { return; }

		// Mark this attribute as protected so the consumable-attributes cleanup
		// in StageOutput won't delete data we just wrote.
		SharedContext.Get()->AddProtectedAttributeName(TypedOutAttribute->Name);

		TArrayView<const T> View = MakeArrayView(OutValues->GetData(), OutValues->Num());
		OutAccessor->SetRange<T>(View, 0, *Source->GetOutKeys(bEnsureValidKeys).Get());
	}

	template <typename T>
	void TArrayBuffer<T>::Fetch(const PCGExMT::FScope& Scope)
	{
		if (!IsSparse() || bReadComplete || !IsEnabled()) { return; }
		if (InternalBroadcaster) { InternalBroadcaster->Fetch(*InValues, Scope); }

		if (TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(TypedInAttribute, TypedInAttribute->GetMetadataDomain()); InAccessor.IsValid())
		{
			TArrayView<T> ReadRange = MakeArrayView(InValues->GetData() + Scope.Start, Scope.Count);
			InAccessor->GetRange<T>(ReadRange, Scope.Start, *Source->GetInKeys());
		}

		if (bCacheValueHashes) { ComputeValueHashes(Scope); }
	}

	template <typename T>
	void TArrayBuffer<T>::Flush()
	{
		InValues.Reset();
		OutValues.Reset();
		InternalBroadcaster.Reset();
	}

#pragma endregion

#pragma region TSingleValueBuffer

	template <typename T>
	int32 TSingleValueBuffer<T>::GetNumValues(const EIOSide InSide)
	{
		return 1;
	}

	template <typename T>
	bool TSingleValueBuffer<T>::EnsureReadable()
	{
		if (bReadInitialized) { return true; }

		InValue = OutValue;

		bReadFromOutput = true;
		bReadInitialized = bWriteInitialized;

		return bReadInitialized;
	}

	template <typename T>
	TSingleValueBuffer<T>::TSingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TLegacyBuffer<T>(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
		this->UnderlyingDomain = EDomainType::Data;
	}

	template <typename T>
	bool TSingleValueBuffer<T>::IsWritable() { return bWriteInitialized; }

	template <typename T>
	bool TSingleValueBuffer<T>::IsReadable() { return bReadInitialized; }

	template <typename T>
	bool TSingleValueBuffer<T>::ReadsFromOutput() { return bReadFromOutput; }

	template <typename T>
	const T& TSingleValueBuffer<T>::Read(const int32 Index) const
	{
		return InValue;
	}

	template <typename T>
	const void TSingleValueBuffer<T>::Read(const int32 Start, TArrayView<T> OutResults) const
	{
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = InValue; }
	}

	template <typename T>
	const T& TSingleValueBuffer<T>::GetValue(const int32 Index)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		return OutValue;
	}

	template <typename T>
	const void TSingleValueBuffer<T>::GetValues(const int32 Start, TArrayView<T> OutResults)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		const int32 Count = OutResults.Num();
		for (int i = 0; i < Count; i++) { OutResults[i] = OutValue; }
	}

	template <typename T>
	void TSingleValueBuffer<T>::SetValue(const int32 Index, const T& Value)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);
		OutValue = Value;
		if (bReadFromOutput) { InValue = Value; }
	}

	template <typename T>
	bool TSingleValueBuffer<T>::InitForRead(const EIOSide InSide, const bool bScoped)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized)
		{
			if (InSide == EIOSide::In && bWriteInitialized && bReadFromOutput)
			{
				check(false)
				// Out-source Reader was created before writer, this is bad?
			}
			else
			{
				return true;
			}
		}

		if (InSide == EIOSide::Out)
		{
			// Reading from output
			check(bWriteInitialized)

			bReadInitialized = bReadFromOutput = true;
			InValue = OutValue;

			return true;
		}

		TypedInAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(Source->GetIn(), Identifier);
		if (TypedInAttribute)
		{
			bReadInitialized = true;

			InAttribute = TypedInAttribute;
			InValue = Helpers::ReadDataValue(TypedInAttribute);
		}

		return bReadInitialized;
	}

	template <typename T>
	bool TSingleValueBuffer<T>::InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax, const bool bScoped, const bool bQuiet)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized)
		{
			if (bWriteInitialized && bReadFromOutput)
			{
				check(false)
				// Out-source broadcaster was created before writer, this is bad?
			}
			else
			{
				return true;
			}
		}

		PCGEX_SHARED_CONTEXT(Source->GetContextHandle())
		bReadInitialized = Helpers::TryReadDataValue(SharedContext.Get(), Source->GetIn(), InSelector, InValue, bQuiet);

		return bReadInitialized;
	}

	template <typename T>
	bool TSingleValueBuffer<T>::InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bWriteInitialized) { return true; }

		this->bIsNewOutput = !PCGExMetaHelpers::HasAttribute(Source->GetOut(), Identifier);

		if (this->bIsNewOutput) { TypedOutAttribute = Source->CreateAttribute(Identifier, DefaultValue, bAllowInterpolation); }
		else { TypedOutAttribute = Source->FindOrCreateAttribute(Identifier, DefaultValue, bAllowInterpolation); }

		if (!TypedOutAttribute) { return false; }

		OutAttribute = TypedOutAttribute;
		bWriteInitialized = true;

		OutValue = DefaultValue;

		const int32 ExistingEntryCount = TypedOutAttribute->GetNumberOfEntriesWithParents();
		const bool bHasIn = Source->GetIn() ? true : false;

		auto GrabExistingValues = [&]()
		{
			OutValue = Helpers::ReadDataValue(TypedOutAttribute);
		};

		if (Init == EBufferInit::Inherit) { GrabExistingValues(); }
		else if (!bHasIn && ExistingEntryCount != 0) { GrabExistingValues(); }

		return bWriteInitialized;
	}

	template <typename T>
	bool TSingleValueBuffer<T>::InitForWrite(const EBufferInit Init)
	{
		{
			FWriteScopeLock WriteScopeLock(BufferLock);
			if (bWriteInitialized) { return true; }
		}

		if (const FPCGMetadataAttribute<T>* ExistingAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(Source->GetIn(), Identifier))
		{
			return InitForWrite(Helpers::ReadDataValue(ExistingAttribute), ExistingAttribute->AllowsInterpolation(), Init);
		}

		return InitForWrite(T{}, true, Init);
	}

	template <typename T>
	void TSingleValueBuffer<T>::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TBuffer::Write);

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())

		if (!IsWritable() || !IsEnabled()) { return; }

		if (!Source->GetOut())
		{
			UE_LOG(LogPCGEx, Error, TEXT("Attempting to write data to an output that's not initialized!"));
			return;
		}

		if (!TypedOutAttribute) { return; }

		Helpers::SetDataValue(TypedOutAttribute, OutValue);
	}

#pragma endregion

#pragma region Externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TArrayBuffer<_TYPE>;\
template class PCGEXCORE_API TSingleValueBuffer<_TYPE>;

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion
}
