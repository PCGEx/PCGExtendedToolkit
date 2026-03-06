// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExData.h"

#include "PCGExCoreSettingsCache.h"

#include "PCGExH.h"
#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Types/PCGExTypes.h"
#include "Metadata/PCGMetadataAttributeGeneric.h"

namespace PCGExData
{
#pragma region Buffers

	uint64 BufferUID(const FPCGAttributeIdentifier& Identifier, const EPCGMetadataTypes Type)
	{
		// Combine attribute name, domain, and value type into a single 64-bit key for buffer deduplication.
		// "Default" domain is treated as "Elements" so they share the same buffer.
		EPCGMetadataDomainFlag SaneFlagForUID = Identifier.MetadataDomain.Flag;
		if (SaneFlagForUID == EPCGMetadataDomainFlag::Default) { SaneFlagForUID = EPCGMetadataDomainFlag::Elements; }
		return PCGEx::H64(HashCombine(GetTypeHash(Identifier.Name), GetTypeHash(SaneFlagForUID)), static_cast<int32>(Type));
	}

	FPCGAttributeIdentifier GetBufferIdentifierFromSelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData)
	{
		// This return an identifier suitable to be used for data facade

		FPCGAttributeIdentifier Identifier;

		if (!InData) { return FPCGAttributeIdentifier(PCGExMetaHelpers::InvalidName, EPCGMetadataDomainFlag::Invalid); }

		FPCGAttributePropertyInputSelector FixedSelector = InSelector.CopyAndFixLast(InData);

		if (InSelector.GetExtraNames().IsEmpty()) { Identifier.Name = FixedSelector.GetName(); }
		else { Identifier.Name = FName(FixedSelector.GetName().ToString() + TEXT(".") + FString::Join(FixedSelector.GetExtraNames(), TEXT("."))); }

		Identifier.MetadataDomain = InData->GetMetadataDomainIDFromSelector(FixedSelector);

		return Identifier;
	}

	void IBuffer::EnableValueHashCache()
	{
		bCacheValueHashes = true;
	}

	IBuffer::IBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: Identifier(InIdentifier), Source(InSource)
	{
	}

	IBuffer::~IBuffer()
	{
		Flush();
	}

	template <typename T>
	bool IBuffer::IsA() const { return Type == PCGExTypes::TTraits<T>::Type; }

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API bool IBuffer::IsA<_TYPE>() const;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	void IBuffer::SetType(const EPCGMetadataTypes InType)
	{
		Type = InType;
		UID = BufferUID(Identifier, InType);
	}

	template <typename T>
	TBuffer<T>::TBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: IBuffer(InSource, InIdentifier)
	{
		SetType(PCGExTypes::TTraits<T>::Type);
	}

	template <typename T>
	const FPCGMetadataAttribute<T>* TBuffer<T>::GetTypedInAttribute() const { return TypedInAttribute; }

	template <typename T>
	FPCGMetadataAttribute<T>* TBuffer<T>::GetTypedOutAttribute() const { return TypedOutAttribute; }

	template <typename T>
	void TBuffer<T>::ReadVoid(const int32 Index, void* OutValue) const { *static_cast<T*>(OutValue) = Read(Index); }

	template <typename T>
	void TBuffer<T>::SetVoid(const int32 Index, const void* Value) { SetValue(Index, *static_cast<const T*>(Value)); }

	template <typename T>
	void TBuffer<T>::GetVoid(const int32 Index, void* OutValue) { *static_cast<T*>(OutValue) = GetValue(Index); }

	template <typename T>
	PCGExValueHash TBuffer<T>::ReadValueHash(const int32 Index) { return PCGExTypes::ComputeHash(Read(Index)); }

	template <typename T>
	PCGExValueHash TBuffer<T>::GetValueHash(const int32 Index) { return PCGExTypes::ComputeHash(GetValue(Index)); }

	template <typename T>
	void TBuffer<T>::DumpValues(TArray<T>& OutValues) const { for (int i = 0; i < OutValues.Num(); i++) { OutValues[i] = Read(i); } }

	template <typename T>
	void TBuffer<T>::DumpValues(const TSharedPtr<TArray<T>>& OutValues) const { DumpValues(*OutValues.Get()); }

	template <typename T>
	TArrayBuffer<T>::TArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TBuffer<T>(InSource, InIdentifier)
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

		UPCGMetadata* InMetadata = Source->GetIn()->Metadata;
		check(InMetadata)

		TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(TypedInAttribute, InMetadata);

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

		TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(TypedOutAttribute, Source->GetOut()->Metadata);

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

		TUniquePtr<IPCGAttributeAccessor> OutAccessor = PCGAttributeAccessorHelpers::CreateAccessor(TypedOutAttribute, Source->GetOut()->Metadata);
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

		if (TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(TypedInAttribute, Source->GetIn()->Metadata); InAccessor.IsValid())
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
		: TBuffer<T>(InSource, InIdentifier)
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

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TBuffer<_TYPE>;\
template class PCGEXCORE_API TArrayBuffer<_TYPE>;\
template class PCGEXCORE_API TSingleValueBuffer<_TYPE>;

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

#pragma endregion

#pragma region FGenericBuffer

	FGenericBuffer::FGenericBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier, int32 InElementSize, int32 InElementAlignment)
		: IBuffer(InSource, InIdentifier)
		, ElementSize(InElementSize)
		, ElementAlignment(InElementAlignment)
	{
		SetType(EPCGMetadataTypes::Unknown);
	}

	int32 FGenericBuffer::GetNumValues(const EIOSide InSide)
	{
		if (InSide == EIOSide::In)
		{
			return InBytes ? InBytes->Num() / FMath::Max(1, ElementSize) : 0;
		}
		return OutBytes ? OutBytes->Num() / FMath::Max(1, ElementSize) : 0;
	}

	bool FGenericBuffer::IsWritable() { return OutBytes != nullptr; }
	bool FGenericBuffer::IsReadable() { return InBytes != nullptr; }
	bool FGenericBuffer::ReadsFromOutput() { return bReadFromOutput; }

	bool FGenericBuffer::EnsureReadable()
	{
		if (bReadInitialized) { return InBytes != nullptr; }

		// If we have output data, read from it
		if (OutBytes)
		{
			if (!InBytes)
			{
				InBytes = OutBytes;
				bReadFromOutput = true;
			}
			bReadInitialized = true;
			return true;
		}

		return InitForRead(EIOSide::In);
	}

	void FGenericBuffer::ReadVoid(const int32 Index, void* OutValue) const
	{
		check(InBytes && ElementSize > 0);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= InBytes->Num());
		FMemory::Memcpy(OutValue, InBytes->GetData() + Offset, ElementSize);
	}

	void FGenericBuffer::SetVoid(const int32 Index, const void* Value)
	{
		check(OutBytes && ElementSize > 0);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= OutBytes->Num());
		FMemory::Memcpy(OutBytes->GetData() + Offset, Value, ElementSize);
	}

	void FGenericBuffer::GetVoid(const int32 Index, void* OutValue)
	{
		if (OutBytes)
		{
			const int32 Offset = Index * ElementSize;
			check(Offset + ElementSize <= OutBytes->Num());
			FMemory::Memcpy(OutValue, OutBytes->GetData() + Offset, ElementSize);
			return;
		}
		ReadVoid(Index, OutValue);
	}

	PCGExValueHash FGenericBuffer::ReadValueHash(const int32 Index)
	{
		if (!InBytes || ElementSize <= 0) { return 0; }
		const int32 Offset = Index * ElementSize;
		return FCrc::MemCrc32(InBytes->GetData() + Offset, ElementSize);
	}

	PCGExValueHash FGenericBuffer::GetValueHash(const int32 Index)
	{
		if (OutBytes && ElementSize > 0)
		{
			const int32 Offset = Index * ElementSize;
			return FCrc::MemCrc32(OutBytes->GetData() + Offset, ElementSize);
		}
		return ReadValueHash(Index);
	}

	bool FGenericBuffer::InitForRead(const EIOSide InSide)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized) { return InBytes != nullptr; }
		bReadInitialized = true;

		if (ElementSize <= 0) { return false; }

		const UPCGBasePointData* PointData = (InSide == EIOSide::In) ? Source->GetIn() : Source->GetOut();
		if (!PointData) { return false; }

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr || !Attr->IsGeneric()) { return false; }

		InAttribute = Attr;
		const FPCGMetadataAttributeGeneric* GenericAttr = static_cast<const FPCGMetadataAttributeGeneric*>(Attr);
		const int32 NumPoints = PointData->GetNumPoints();

		InBytes = MakeShared<TArray<uint8>>();
		InBytes->SetNumZeroed(NumPoints * ElementSize);

		// Bulk read using metadata entry keys
		auto EntryKeys = PointData->GetConstMetadataEntryValueRange();

		for (int32 i = 0; i < NumPoints; i++)
		{
			const void* ReadAddr = GenericAttr->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]);
			if (ReadAddr)
			{
				FMemory::Memcpy(InBytes->GetData() + i * ElementSize, ReadAddr, ElementSize);
			}
		}

		bReadComplete = true;
		return true;
	}

	bool FGenericBuffer::InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bWriteInitialized) { return OutBytes != nullptr; }
		bWriteInitialized = true;

		if (ElementSize <= 0) { return false; }

		const UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData) { return false; }

		const int32 NumPoints = OutData->GetNumPoints();

		OutBytes = MakeShared<TArray<uint8>>();
		OutBytes->SetNumZeroed(NumPoints * ElementSize);

		// If inheriting, copy input values to output
		if (Init == EBufferInit::Inherit && SourceAttribute && SourceAttribute->IsGeneric())
		{
			const FPCGMetadataAttributeGeneric* GenericSrc = static_cast<const FPCGMetadataAttributeGeneric*>(SourceAttribute);
			auto EntryKeys = OutData->GetConstMetadataEntryValueRange();

			for (int32 i = 0; i < NumPoints; i++)
			{
				const void* ReadAddr = GenericSrc->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]);
				if (ReadAddr)
				{
					FMemory::Memcpy(OutBytes->GetData() + i * ElementSize, ReadAddr, ElementSize);
				}
			}
		}

		bIsNewOutput = (Init == EBufferInit::New);
		return true;
	}

	void FGenericBuffer::Write(const bool bEnsureValidKeys)
	{
		if (!IsWritable() || !IsEnabled() || !OutBytes) { return; }

		// STUB: Write-back to generic attributes is deferred to Phase 3.
		// For now, FGenericBuffer supports in-memory read/modify/get but cannot
		// flush modified bytes back to the underlying FPCGMetadataAttributeGeneric.
		// This is sufficient for copy-only blend operations that only need to
		// read source values and pass them through unchanged.
		ensureMsgf(false, TEXT("FGenericBuffer::Write() is not yet implemented for generic attributes. Data will not be written back."));
	}

#pragma endregion

#pragma region FFacade

	int32 FFacade::GetNum(const EIOSide InSide) const { return Source->GetNum(InSide); }

	TSharedPtr<IBuffer> FFacade::FindBuffer_Unsafe(const uint64 UID)
	{
		TSharedPtr<IBuffer>* Found = BufferMap.Find(UID);
		if (!Found) { return nullptr; }
		return *Found;
	}

	TSharedPtr<IBuffer> FFacade::FindBuffer(const uint64 UID)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		return FindBuffer_Unsafe(UID);
	}

	TSharedPtr<IBuffer> FFacade::FindReadableAttributeBuffer(const FPCGAttributeIdentifier& InIdentifier)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		for (const TSharedPtr<IBuffer>& Buffer : Buffers)
		{
			if (!Buffer->IsReadable()) { continue; }
			if (Buffer->InAttribute && Buffer->InAttribute->Name == InIdentifier.Name) { return Buffer; }
		}
		return nullptr;
	}

	TSharedPtr<IBuffer> FFacade::FindWritableAttributeBuffer(const FPCGAttributeIdentifier& InIdentifier)
	{
		FReadScopeLock ReadScopeLock(BufferLock);
		for (const TSharedPtr<IBuffer>& Buffer : Buffers)
		{
			if (!Buffer->IsWritable()) { continue; }
			if (Buffer->Identifier == InIdentifier) { return Buffer; }
		}
		return nullptr;
	}

	EPCGPointNativeProperties FFacade::GetAllocations() const { return Source->GetAllocations(); }

	FPCGExContext* FFacade::GetContext() const
	{
		const FPCGContext::FSharedContext<FPCGExContext> SharedContext(Source->GetContextHandle());
		return SharedContext.Get();
	}

	FFacade::FFacade(const TSharedRef<FPointIO>& InSource)
		: Source(InSource), Idx(InSource->IOIndex)
	{
	}

	bool FFacade::IsDataValid(const EIOSide InSide) const { return Source->IsDataValid(InSide); }

	bool FFacade::ShareSource(const FFacade* OtherManager) const { return this == OtherManager || OtherManager->Source == Source; }

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::FindBuffer_Unsafe(const FPCGAttributeIdentifier& InIdentifier)
	{
		const TSharedPtr<IBuffer>& Found = FindBuffer_Unsafe(BufferUID(InIdentifier, PCGExTypes::TTraits<T>::Type));
		if (!Found) { return nullptr; }
		return StaticCastSharedPtr<TBuffer<T>>(Found);
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::FindBuffer(const FPCGAttributeIdentifier& InIdentifier)
	{
		const TSharedPtr<IBuffer> Found = FindBuffer(BufferUID(InIdentifier, PCGExTypes::TTraits<T>::Type));
		if (!Found) { return nullptr; }
		return StaticCastSharedPtr<TBuffer<T>>(Found);
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetBuffer(const FPCGAttributeIdentifier& InIdentifier)
	{
		if (InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Invalid)
		{
			UE_LOG(LogPCGEx, Error, TEXT("GetBuffer : Invalid MetadataDomain for : '%s'"), *InIdentifier.Name.ToString());
			return nullptr;
		}

		TSharedPtr<TBuffer<T>> Buffer = FindBuffer<T>(InIdentifier);
		if (Buffer) { return Buffer; }

		{
			FWriteScopeLock WriteScopeLock(BufferLock);

			Buffer = FindBuffer_Unsafe<T>(InIdentifier);
			if (Buffer) { return Buffer; }

			if (InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Default || InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Elements)
			{
				Buffer = MakeShared<TArrayBuffer<T>>(Source, InIdentifier);
			}
			else if (InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
			{
				Buffer = MakeShared<TSingleValueBuffer<T>>(Source, InIdentifier);
			}
			else
			{
				UE_LOG(LogPCGEx, Error, TEXT("Attempting to create a buffer with unsupported domain."));
				return nullptr;
			}

			Buffer->BufferIndex = Buffers.Num();

			Buffers.Add(Buffer);
			BufferMap.Add(Buffer->UID, Buffer);

			return Buffer;
		}
	}


	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetWritable(const FPCGAttributeIdentifier& InIdentifier, T DefaultValue, bool bAllowInterpolation, EBufferInit Init)
	{
		TSharedPtr<TBuffer<T>> Buffer = nullptr;

		if (InIdentifier.MetadataDomain.IsDefault())
		{
			Buffer = GetBuffer<T>(PCGExMetaHelpers::GetAttributeIdentifier(InIdentifier.Name, Source->GetOut()));
		}
		else
		{
			Buffer = GetBuffer<T>(InIdentifier);
		}

		if (!Buffer || !Buffer->InitForWrite(DefaultValue, bAllowInterpolation, Init)) { return nullptr; }
		return Buffer;
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetWritable(const FPCGMetadataAttribute<T>* InAttribute, EBufferInit Init)
	{
		return GetWritable(FPCGAttributeIdentifier(InAttribute->Name, InAttribute->GetMetadataDomain()->GetDomainID()), InAttribute->GetValue(PCGDefaultValueKey), InAttribute->AllowsInterpolation(), Init);
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetWritable(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init)
	{
		TSharedPtr<TBuffer<T>> Buffer = nullptr;

		if (InIdentifier.MetadataDomain.IsDefault())
		{
			// Identifier created from FName, need to sanitize it
			// We'll do so using a selector, this is expensive but quick and future proof
			Buffer = GetBuffer<T>(PCGExMetaHelpers::GetAttributeIdentifier(InIdentifier.Name, Source->GetOut()));
		}
		else
		{
			Buffer = GetBuffer<T>(InIdentifier);
		}

		if (!Buffer || !Buffer->InitForWrite(Init)) { return nullptr; }
		return Buffer;
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetReadable(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide, const bool bSupportScoped)
	{
		TSharedPtr<TBuffer<T>> Buffer = nullptr;

		if (InIdentifier.MetadataDomain.IsDefault())
		{
			// Identifier created from FName, need to sanitize it
			// We'll do so using a selector, this is expensive but quick and future proof
			Buffer = GetBuffer<T>(PCGExMetaHelpers::GetAttributeIdentifier(InIdentifier.Name, Source->GetData(InSide)));
		}
		else
		{
			Buffer = GetBuffer<T>(InIdentifier);
		}

		if (!Buffer || !Buffer->InitForRead(InSide, bSupportsScopedGet ? bSupportScoped : false))
		{
			Flush(Buffer);
			return nullptr;
		}

		return Buffer;
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet)
	{
		// Build a proper identifier from the selector
		// We'll use it to get a unique buffer ID as well as domain, which is conditional to finding the right buffer class to use

		const FPCGAttributeIdentifier Identifier = GetBufferIdentifierFromSelector(InSelector, Source->GetIn());
		if (Identifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Invalid)
		{
			UE_LOG(LogPCGEx, Error, TEXT("GetBroadcaster : Invalid domain with '%s'"), *Identifier.Name.ToString());
			return nullptr;
		}

		TSharedPtr<TBuffer<T>> Buffer = GetBuffer<T>(Identifier);
		if (!Buffer || !Buffer->InitForBroadcast(InSelector, bCaptureMinMax, bCaptureMinMax || !bSupportsScopedGet ? false : bSupportScoped, bQuiet))
		{
			Flush(Buffer);
			return nullptr;
		}

		return Buffer;
	}

	template <typename T>
	TSharedPtr<TBuffer<T>> FFacade::GetBroadcaster(const FName InName, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet)
	{
		// Create a selector from the identifier.
		// This is a bit backward but the user may have added domain prefixes to the name such as @Data.
		FPCGAttributePropertyInputSelector Selector = FPCGAttributePropertyInputSelector();
		Selector.Update(InName.ToString());

		return GetBroadcaster<T>(Selector, bSupportScoped, bCaptureMinMax, bQuiet);
	}

	template <typename T>
	FPCGMetadataAttribute<T>* FFacade::FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		return Source->FindMutableAttribute<T>(InIdentifier, InSide);
	}

	template <typename T>
	const FPCGMetadataAttribute<T>* FFacade::FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		return Source->FindConstAttribute<T>(InIdentifier, InSide);
	}

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::FindBuffer_Unsafe<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::FindBuffer<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetBuffer<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, _TYPE DefaultValue, bool bAllowInterpolation, EBufferInit Init); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGMetadataAttribute<_TYPE>* InAttribute, EBufferInit Init); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetReadable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide, const bool bSupportScoped); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FPCGAttributePropertyInputSelector& InSelector, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FName InName, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
template PCGEXCORE_API FPCGMetadataAttribute<_TYPE>* FFacade::FindMutableAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const; \
template PCGEXCORE_API const FPCGMetadataAttribute<_TYPE>* FFacade::FindConstAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	TSharedPtr<IBuffer> FFacade::GetWritable(const EPCGMetadataTypes Type, const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init)
	{
#define PCGEX_TYPED_WRITABLE(_TYPE, _ID, ...) case EPCGMetadataTypes::_ID: return GetWritable<_TYPE>(static_cast<const FPCGMetadataAttribute<_TYPE>*>(InAttribute), Init);
		switch (Type)
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TYPED_WRITABLE)
		default:
			{
				// Fallback: create FGenericBuffer for unknown/generic types
				if (!InAttribute) { return nullptr; }

				const int32 ElemSize = PCGExTypes::GetElementSizeFromAttribute(InAttribute);
				if (ElemSize <= 0)
				{
					UE_LOG(LogPCGEx, Warning, TEXT("Cannot create writable buffer for attribute '%s' — unknown element size (type %d)."), *InAttribute->Name.ToString(), static_cast<int32>(Type));
					return nullptr;
				}

				const int32 ElemAlign = PCGExTypes::GetElementAlignmentFromType(Type, nullptr);

				FPCGAttributeIdentifier AttrIdentifier;
				AttrIdentifier.Name = InAttribute->Name;

				TSharedPtr<FGenericBuffer> GenericBuf = MakeShared<FGenericBuffer>(Source, AttrIdentifier, ElemSize, ElemAlign);
				if (!GenericBuf->InitForWrite(InAttribute, Init))
				{
					return nullptr;
				}

				// Register in facade
				FWriteScopeLock WriteScopeLock(BufferLock);
				GenericBuf->BufferIndex = Buffers.Add(GenericBuf);
				BufferMap.Add(GenericBuf->GetUID(), GenericBuf);
				return GenericBuf;
			}
		}
#undef PCGEX_TYPED_WRITABLE
	}

	TSharedPtr<IBuffer> FFacade::GetWritable(const EPCGMetadataTypes Type, const FName InName, EBufferInit Init)
	{
#define PCGEX_TYPED_WRITABLE(_TYPE, _ID, ...) case EPCGMetadataTypes::_ID: return GetWritable<_TYPE>(InName, Init);
		switch (Type)
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TYPED_WRITABLE)
		default:
			{
				// For generic types by name, look up the attribute first to get size info
				FPCGAttributeIdentifier AttrId;
				AttrId.Name = InName;

				const FPCGMetadataAttributeBase* RawAttribute = Source->FindConstAttribute(AttrId, EIOSide::In);
				if (!RawAttribute)
				{
					RawAttribute = Source->FindConstAttribute(AttrId, EIOSide::Out);
				}

				if (!RawAttribute) { return nullptr; }

				return GetWritable(Type, RawAttribute, Init);
			}
		}
#undef PCGEX_TYPED_WRITABLE
	}

	TSharedPtr<IBuffer> FFacade::GetReadable(const FAttributeIdentity& Identity, const EIOSide InSide, const bool bSupportScoped)
	{
		TSharedPtr<IBuffer> Buffer = nullptr;

#define PCGEX_TYPED_EXEC(_TYPE, _NAME) Buffer = GetReadable<_TYPE>(Identity.Identifier, InSide, bSupportScoped);
		PCGEX_EXECUTEWITHRIGHTTYPE(Identity.UnderlyingType, PCGEX_TYPED_EXEC)
#undef PCGEX_TYPED_EXEC

		// Fallback: create FGenericBuffer for unknown/generic types
		if (!Buffer)
		{
			const FPCGMetadataAttributeBase* RawAttribute = Source->FindConstAttribute(Identity.Identifier, InSide);
			if (RawAttribute && RawAttribute->IsGeneric())
			{
				const int32 ElemSize = PCGExTypes::GetElementSizeFromAttribute(RawAttribute);
				if (ElemSize > 0)
				{
					const int32 ElemAlign = PCGExTypes::GetElementAlignmentFromType(Identity.UnderlyingType, nullptr);
					TSharedPtr<FGenericBuffer> GenericBuf = MakeShared<FGenericBuffer>(Source, Identity.Identifier, ElemSize, ElemAlign);
					if (GenericBuf->InitForRead(InSide))
					{
						FWriteScopeLock WriteScopeLock(BufferLock);
						GenericBuf->BufferIndex = Buffers.Add(GenericBuf);
						BufferMap.Add(GenericBuf->GetUID(), GenericBuf);
						Buffer = GenericBuf;
					}
				}
			}
		}

		return Buffer;
	}

	TSharedPtr<IBuffer> FFacade::GetDefaultReadable(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide, const bool bSupportScoped)
	{
		TSharedPtr<IBuffer> Buffer = nullptr;
		const FPCGMetadataAttributeBase* RawAttribute = Source->FindConstAttribute(InIdentifier, InSide);

		if (!RawAttribute) { return nullptr; }

		const EPCGMetadataTypes AttrType = static_cast<EPCGMetadataTypes>(RawAttribute->GetTypeId());

#define PCGEX_TYPED_EXEC(_TYPE, _NAME) Buffer = Buffer = GetReadable<_TYPE>(InIdentifier, InSide, bSupportScoped);
		PCGEX_EXECUTEWITHRIGHTTYPE(AttrType, PCGEX_TYPED_EXEC)
#undef PCGEX_TYPED_EXEC

		// Fallback: create FGenericBuffer for unknown/generic types
		if (!Buffer && RawAttribute->IsGeneric())
		{
			const int32 ElemSize = PCGExTypes::GetElementSizeFromAttribute(RawAttribute);
			if (ElemSize > 0)
			{
				const int32 ElemAlign = PCGExTypes::GetElementAlignmentFromType(AttrType, nullptr);
				TSharedPtr<FGenericBuffer> GenericBuf = MakeShared<FGenericBuffer>(Source, InIdentifier, ElemSize, ElemAlign);
				if (GenericBuf->InitForRead(InSide))
				{
					FWriteScopeLock WriteScopeLock(BufferLock);
					GenericBuf->BufferIndex = Buffers.Add(GenericBuf);
					BufferMap.Add(GenericBuf->GetUID(), GenericBuf);
					Buffer = GenericBuf;
				}
			}
		}

		return Buffer;
	}

	FPCGMetadataAttributeBase* FFacade::FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		return Source->FindMutableAttribute(InIdentifier, InSide);
	}

	const FPCGMetadataAttributeBase* FFacade::FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		return Source->FindConstAttribute(InIdentifier, InSide);
	}

	const UPCGBasePointData* FFacade::GetData(const EIOSide InSide) const { return Source->GetData(InSide); }

	const UPCGBasePointData* FFacade::GetIn() const { return Source->GetIn(); }

	UPCGBasePointData* FFacade::GetOut() const { return Source->GetOut(); }

	void FFacade::CreateReadables(const TArray<FAttributeIdentity>& Identities, const bool bWantsScoped)
	{
		for (const FAttributeIdentity& Identity : Identities) { GetReadable(Identity, EIOSide::In, bWantsScoped); }
	}

	void FFacade::MarkCurrentBuffersReadAsComplete()
	{
		for (const TSharedPtr<IBuffer>& Buffer : Buffers)
		{
			if (!Buffer.IsValid() || !Buffer->IsReadable()) { continue; }
			Buffer->bReadComplete = true;
		}
	}

	void FFacade::Flush()
	{
		FWriteScopeLock WriteScopeLock(BufferLock);
		Buffers.Empty();
		BufferMap.Empty();
	}

	void FFacade::Write(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const bool bEnsureValidKeys)
	{
		if (!TaskManager || !TaskManager->IsAvailable() || !Source->GetOut()) { return; }

		if (ValidateOutputsBeforeWriting())
		{
			if (bEnsureValidKeys) { Source->GetOutKeys(true); }

			{
				FWriteScopeLock WriteScopeLock(BufferLock);
				PCGEX_SCHEDULING_SCOPE(TaskManager)

				for (int i = 0; i < Buffers.Num(); i++)
				{
					const TSharedPtr<IBuffer> Buffer = Buffers[i];
					if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled()) { continue; }
					WriteBuffer(TaskManager, Buffer, false);
				}
			}
		}

		Flush();
	}

	int32 FFacade::WriteBuffersAsCallbacks(const TSharedPtr<PCGExMT::FTaskGroup>& TaskGroup)
	{
		// !!! Requires manual flush !!!

		if (!TaskGroup || !ValidateOutputsBeforeWriting())
		{
			Flush();
			return -1;
		}

		int32 WritableCount = 0;
		Source->GetOutKeys(true);

		{
			FWriteScopeLock WriteScopeLock(BufferLock);

			for (int i = 0; i < Buffers.Num(); i++)
			{
				const TSharedPtr<IBuffer> Buffer = Buffers[i];
				if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled()) { continue; }

				TaskGroup->AddSimpleCallback([BufferRef = Buffer]() { BufferRef->Write(); });
				WritableCount++;
			}
		}

		return WritableCount;
	}

	void FFacade::WriteBuffers(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, PCGExMT::FCompletionCallback&& Callback)
	{
		if (!ValidateOutputsBeforeWriting())
		{
			Flush();
			return;
		}

		if (Source->GetNum(EIOSide::Out) < PCGEX_CORE_SETTINGS.SmallPointsSize)
		{
			WriteSynchronous(true);
			Callback();
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, WriteBuffersWithCallback);
		WriteBuffersWithCallback->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE, Callback]()
		{
			PCGEX_ASYNC_THIS
			This->Flush();
			Callback();
		};

		if (const int32 WritableCount = WriteBuffersAsCallbacks(WriteBuffersWithCallback); WritableCount <= 0)
		{
			// -1 is fail so no callback
			if (WritableCount == 0) { Callback(); }
			return;
		}

		WriteBuffersWithCallback->StartSimpleCallbacks();
	}

	int32 FFacade::WriteSynchronous(const bool bEnsureValidKeys)
	{
		if (!Source->GetOut()) { return -1; }

		int32 WritableCount = 0;

		if (ValidateOutputsBeforeWriting())
		{
			if (bEnsureValidKeys) { Source->GetOutKeys(true); }

			{
				FWriteScopeLock WriteScopeLock(BufferLock);

				for (int i = 0; i < Buffers.Num(); i++)
				{
					const TSharedPtr<IBuffer> Buffer = Buffers[i];
					if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled()) { continue; }
					Buffer->Write(false);
					WritableCount++;
				}
			}
		}

		Flush();
		return WritableCount;
	}

	void FFacade::WriteFastest(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const bool bEnsureValidKeys)
	{
		if (!Source->GetOut()) { return; }

		if (Source->GetNum(EIOSide::Out) < PCGEX_CORE_SETTINGS.SmallPointsSize) { WriteSynchronous(bEnsureValidKeys); }
		else { Write(TaskManager, bEnsureValidKeys); }
	}

	void FFacade::Fetch(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FFacade::Fetch);
		for (const TSharedPtr<IBuffer>& Buffer : Buffers) { Buffer->Fetch(Scope); }
	}

	FConstPoint FFacade::GetInPoint(const int32 Index) const { return Source->GetInPoint(Index); }
	FMutablePoint FFacade::GetOutPoint(const int32 Index) const { return Source->GetOutPoint(Index); }

	FScope FFacade::GetInScope(const int32 Start, const int32 Count, const bool bInclusive) const { return Source->GetInScope(Start, Count, bInclusive); }
	FScope FFacade::GetInScope(const PCGExMT::FScope& Scope) const { return Source->GetInScope(Scope); }
	FScope FFacade::GetInFullScope() const { return Source->GetInFullScope(); }
	FScope FFacade::GetInRange(const int32 Start, const int32 End, const bool bInclusive) const { return Source->GetInRange(Start, End, bInclusive); }

	FScope FFacade::GetOutScope(const int32 Start, const int32 Count, const bool bInclusive) const { return Source->GetOutScope(Start, Count, bInclusive); }
	FScope FFacade::GetOutScope(const PCGExMT::FScope& Scope) const { return Source->GetOutScope(Scope); }
	FScope FFacade::GetOutFullScope() const { return Source->GetOutFullScope(); }
	FScope FFacade::GetOutRange(const int32 Start, const int32 End, const bool bInclusive) const { return Source->GetOutRange(Start, End, bInclusive); }

	bool FFacade::ValidateOutputsBeforeWriting() const
	{
		PCGEX_SHARED_CONTEXT(Source->GetContextHandle())
		FPCGExContext* Context = SharedContext.Get();

		{
			FWriteScopeLock WriteScopeLock(BufferLock);

			TSet<FPCGAttributeIdentifier> UniqueOutputs;
			for (int i = 0; i < Buffers.Num(); i++)
			{
				const TSharedPtr<IBuffer> Buffer = Buffers[i];
				if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled()) { continue; }

				FPCGAttributeIdentifier Identifier = Buffer->Identifier;
				bool bAlreadySet = false;
				UniqueOutputs.Add(Identifier, &bAlreadySet);

				if (bAlreadySet)
				{
					PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(FTEXT("Attribute \"{0}\" is written to at least twice by different buffers."), FText::FromName(Identifier.Name)));
					return false;
				}
			}
		}

		return true;
	}

	void FFacade::Flush(const TSharedPtr<IBuffer>& Buffer)
	{
		if (!Buffer || !Buffer.IsValid()) { return; }

		{
			FWriteScopeLock WriteScopeLock(BufferLock);

			if (Buffers.IsValidIndex(Buffer->BufferIndex)) { Buffers.RemoveAt(Buffer->BufferIndex); }
			BufferMap.Remove(Buffer->UID);

			int32 WriteIndex = 0;
			for (int i = 0; i < Buffers.Num(); i++)
			{
				TSharedPtr<IBuffer> TempBuffer = Buffers[i];

				if (!TempBuffer || !TempBuffer.IsValid()) { continue; }
				TempBuffer->BufferIndex = WriteIndex++;
				Buffers[TempBuffer->BufferIndex] = TempBuffer;
			}
		}
	}

	template <typename T>
	FPCGMetadataAttribute<T>* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, T MarkValue)
	{
		UPCGMetadata* Metadata = InData->MutableMetadata();

		if (!Metadata) { return nullptr; }

		Metadata->DeleteAttribute(MarkID);
		FPCGMetadataAttribute<T>* Mark = Metadata->CreateAttribute<T>(MarkID, MarkValue, true, true);
		Helpers::SetDataValue(Mark, MarkValue);
		return Mark;
	}

	template <typename T>
	FPCGMetadataAttribute<T>* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T MarkValue)
	{
		const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(MarkID, PointIO->GetOut());
		return WriteMark<T>(PointIO->GetMutableData(EIOSide::Out), Identifier, MarkValue);
	}

	template <typename T>
	bool TryReadMark(UPCGMetadata* Metadata, const FPCGAttributeIdentifier& MarkID, T& OutMark)
	{
		// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
		// ReSharper disable once CppRedundantTemplateKeyword
		const FPCGMetadataAttribute<T>* Mark = PCGExMetaHelpers::TryGetConstAttribute<T>(Metadata, MarkID);
		if (!Mark) { return false; }
		OutMark = Helpers::ReadDataValue(Mark);
		return true;
	}

	template <typename T>
	bool TryReadMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T& OutMark)
	{
		const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(MarkID, PointIO->GetIn());
		return TryReadMark(PointIO->GetIn() ? PointIO->GetIn()->Metadata : PointIO->GetOut()->Metadata, Identifier, OutMark);
	}

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template PCGEXCORE_API FPCGMetadataAttribute<_TYPE>* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, _TYPE MarkValue); \
template PCGEXCORE_API FPCGMetadataAttribute<_TYPE>* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, _TYPE MarkValue); \
template PCGEXCORE_API bool TryReadMark<_TYPE>(UPCGMetadata* Metadata, const FPCGAttributeIdentifier& MarkID, _TYPE& OutMark); \
template PCGEXCORE_API bool TryReadMark<_TYPE>(const TSharedRef<FPointIO>& PointIO, const FName MarkID, _TYPE& OutMark);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

	void WriteId(const TSharedRef<FPointIO>& PointIO, const FName IdName, const int64 Id)
	{
		PointIO->Tags->Set<int64>(IdName.ToString(), Id);
		if (PointIO->GetOut()) { WriteMark(PointIO, IdName, Id); }
	}

	UPCGBasePointData* GetMutablePointData(FPCGContext* Context, const FPCGTaggedData& Source)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Source.Data);
		if (!SpatialData) { return nullptr; }

		const UPCGBasePointData* PointData = SpatialData->ToPointData(Context);
		if (!PointData) { return nullptr; }

		return const_cast<UPCGBasePointData*>(PointData);
	}

	class FWriteBufferTask final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FWriteTask)

		explicit FWriteBufferTask(const TSharedPtr<IBuffer>& InBuffer, const bool InEnsureValidKeys = true)
			: FTask(), bEnsureValidKeys(InEnsureValidKeys), Buffer(InBuffer)
		{
		}

		bool bEnsureValidKeys = true;
		TSharedPtr<IBuffer> Buffer;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			if (!Buffer) { return; }
			Buffer->Write(bEnsureValidKeys);
		}
	};

	void WriteBuffer(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<IBuffer>& InBuffer, const bool InEnsureValidKeys)
	{
		// @Data domain buffers (single-value) and reset-with-first-value buffers
		// are written synchronously since they're trivially cheap.
		// Element-domain buffers (per-point arrays) are dispatched as async tasks
		// to parallelize the PCG accessor SetRange calls.
		if (InBuffer->GetUnderlyingDomain() == EDomainType::Data || InBuffer->bResetWithFirstValue)
		{
			InBuffer->Write(InEnsureValidKeys);
		}
		else
		{
			if (!TaskManager || !TaskManager->IsAvailable()) { InBuffer->Write(InEnsureValidKeys); }
			PCGEX_LAUNCH(FWriteBufferTask, InBuffer, InEnsureValidKeys)
		}
	}

#pragma endregion

	TSharedPtr<FFacade> TryGetSingleFacade(FPCGExContext* InContext, const FName InputPinLabel, const bool bTransactional, const bool bRequired)
	{
		if (const TSharedPtr<FPointIO> SingleIO = TryGetSingleInput(InContext, InputPinLabel, bTransactional, bRequired))
		{
			return MakeShared<FFacade>(SingleIO.ToSharedRef());
		}

		return nullptr;
	}

	bool TryGetFacades(FPCGExContext* InContext, const FName InputPinLabel, TArray<TSharedPtr<FFacade>>& OutFacades, const bool bRequired, const bool bIsTransactional)
	{
		TSharedPtr<FPointIOCollection> TargetsCollection = MakeShared<FPointIOCollection>(InContext, InputPinLabel, EIOInit::NoInit, bIsTransactional);
		if (TargetsCollection->IsEmpty())
		{
			if (bRequired) { PCGEX_LOG_MISSING_INPUT(InContext, FText::Format(FText::FromString(TEXT("Missing or zero-points '{0}' inputs")), FText::FromName(InputPinLabel))) }
			return false;
		}

		OutFacades.Reserve(OutFacades.Num() + TargetsCollection->Num());
		for (const TSharedPtr<FPointIO>& IO : TargetsCollection->Pairs) { OutFacades.Add(MakeShared<FFacade>(IO.ToSharedRef())); }

		return true;
	}
}
