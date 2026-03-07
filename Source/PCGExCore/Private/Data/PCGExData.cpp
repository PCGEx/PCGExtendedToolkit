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
#include "Metadata/PCGMetadataDomain.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

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
	TLegacyBuffer<T>::TLegacyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TBuffer<T>(InSource, InIdentifier)
	{
	}

#pragma region TGenericBuffer

	template <typename T>
	TGenericBuffer<T>::TGenericBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: TBuffer<T>(InSource, InIdentifier)
	{
	}

#pragma endregion

#pragma region Externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TBuffer<_TYPE>;\
template class PCGEXCORE_API TLegacyBuffer<_TYPE>;\
template class PCGEXCORE_API TGenericBuffer<_TYPE>;

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

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
				// Tier 3 fallback: FPropertyBuffer for types not in PCGEX_FOREACH_SUPPORTEDTYPES.
				// If callers know T at compile time, they should use GetWritable<T>() or a
				// TGenericBuffer<T> instantiation instead — it's type-safe and uses bulk accessor writes.
				if (!InAttribute || !InAttribute->IsGeneric()) { return nullptr; }

				const FPCGMetadataDomainID DomainID = InAttribute->GetMetadataDomain()->GetDomainID();
				FPCGAttributeIdentifier AttrIdentifier(InAttribute->Name, DomainID);

				const auto* GenericAttr = static_cast<const FPCGMetadataAttributeGeneric*>(InAttribute);

				if (DomainID.Flag == EPCGMetadataDomainFlag::Data)
				{
					auto PropBuf = MakeShared<FPropertySingleValueBuffer>(Source, AttrIdentifier);
					if (!PropBuf->InitProperty(GenericAttr) || !PropBuf->InitForWrite(InAttribute, Init)) { return nullptr; }
					FWriteScopeLock WriteScopeLock(BufferLock);
					PropBuf->BufferIndex = Buffers.Add(PropBuf);
					BufferMap.Add(PropBuf->GetUID(), PropBuf);
					return PropBuf;
				}
				else
				{
					auto PropBuf = MakeShared<FPropertyArrayBuffer>(Source, AttrIdentifier);
					if (!PropBuf->InitProperty(GenericAttr) || !PropBuf->InitForWrite(InAttribute, Init)) { return nullptr; }
					FWriteScopeLock WriteScopeLock(BufferLock);
					PropBuf->BufferIndex = Buffers.Add(PropBuf);
					BufferMap.Add(PropBuf->GetUID(), PropBuf);
					return PropBuf;
				}
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

		// Tier 3 fallback: FPropertyBuffer for types not in PCGEX_FOREACH_SUPPORTEDTYPES
		if (!Buffer)
		{
			const FPCGMetadataAttributeBase* RawAttribute = Source->FindConstAttribute(Identity.Identifier, InSide);
			if (RawAttribute && RawAttribute->IsGeneric())
			{
				const auto* GenericAttr = static_cast<const FPCGMetadataAttributeGeneric*>(RawAttribute);

				if (Identity.Identifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
				{
					auto PropBuf = MakeShared<FPropertySingleValueBuffer>(Source, Identity.Identifier);
					if (PropBuf->InitProperty(GenericAttr) && PropBuf->InitForRead(InSide))
					{
						FWriteScopeLock WriteScopeLock(BufferLock);
						PropBuf->BufferIndex = Buffers.Add(PropBuf);
						BufferMap.Add(PropBuf->GetUID(), PropBuf);
						Buffer = PropBuf;
					}
				}
				else
				{
					auto PropBuf = MakeShared<FPropertyArrayBuffer>(Source, Identity.Identifier);
					if (PropBuf->InitProperty(GenericAttr) && PropBuf->InitForRead(InSide))
					{
						FWriteScopeLock WriteScopeLock(BufferLock);
						PropBuf->BufferIndex = Buffers.Add(PropBuf);
						BufferMap.Add(PropBuf->GetUID(), PropBuf);
						Buffer = PropBuf;
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

		// Tier 3 fallback: FPropertyBuffer for types not in PCGEX_FOREACH_SUPPORTEDTYPES
		if (!Buffer && RawAttribute->IsGeneric())
		{
			const auto* GenericAttr = static_cast<const FPCGMetadataAttributeGeneric*>(RawAttribute);

			if (InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
			{
				auto PropBuf = MakeShared<FPropertySingleValueBuffer>(Source, InIdentifier);
				if (PropBuf->InitProperty(GenericAttr) && PropBuf->InitForRead(InSide))
				{
					FWriteScopeLock WriteScopeLock(BufferLock);
					PropBuf->BufferIndex = Buffers.Add(PropBuf);
					BufferMap.Add(PropBuf->GetUID(), PropBuf);
					Buffer = PropBuf;
				}
			}
			else
			{
				auto PropBuf = MakeShared<FPropertyArrayBuffer>(Source, InIdentifier);
				if (PropBuf->InitProperty(GenericAttr) && PropBuf->InitForRead(InSide))
				{
					FWriteScopeLock WriteScopeLock(BufferLock);
					PropBuf->BufferIndex = Buffers.Add(PropBuf);
					BufferMap.Add(PropBuf->GetUID(), PropBuf);
					Buffer = PropBuf;
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
