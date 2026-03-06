// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

#include "PCGExPointElements.h"
#include "PCGExCommon.h"
#include "PCGExDataCommon.h"
#include "PCGCommon.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

#pragma region DATA MACROS

#ifndef PCGEX_DATA_MACROS
#define PCGEX_DATA_MACROS

#define PCGEX_INIT_IO_VOID(_IO, _INIT) if (!_IO->InitializeOutput(_INIT)) { return; }
#define PCGEX_INIT_IO(_IO, _INIT) if (!_IO->InitializeOutput(_INIT)) { return false; }

#define PCGEX_CLEAR_IO_VOID(_IO) if (!_IO->InitializeOutput(PCGExData::EIOInit::NoInit)) { return; } _IO->Disable();
#define PCGEX_CLEAR_IO(_IO) if (!_IO->InitializeOutput(PCGExData::EIOInit::NoInit)) { return false; } _IO->Disable();

#endif
#pragma endregion

struct FPCGContext;
class UPCGMetadata;
struct FPCGAttributePropertyInputSelector;
struct FPCGAttributeIdentifier;
class FPCGMetadataAttributeBase;
struct FPCGExContext;

namespace PCGExMT
{
	class FTaskGroup;
	class FTaskManager;
}

template<typename T>
class FPCGMetadataAttribute;

class FPCGMetadataAttributeGeneric;
struct FPCGMetadataAttributeDesc;

namespace PCGExData
{
	struct FAttributeIdentity;

	template <typename T>
	class TAttributeBroadcaster;
}

namespace PCGExData
{
	enum class EBufferInit : uint8
	{
		Inherit = 0,
		New,
	};

	enum class EDomainType : uint8
	{
		Unknown  = 0,
		Data     = 1,
		Elements = 2,
	};

#pragma region Buffers

	class FFacade;

	PCGEXCORE_API uint64 BufferUID(const FPCGAttributeIdentifier& Identifier, const EPCGMetadataTypes Type);

	PCGEXCORE_API FPCGAttributeIdentifier GetBufferIdentifierFromSelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData);

	class PCGEXCORE_API IBuffer : public TSharedFromThis<IBuffer>
	{
		friend class FFacade;

	protected:
		mutable FRWLock BufferLock;

		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
		EDomainType UnderlyingDomain = EDomainType::Unknown;

		uint64 UID = 0;

		bool bIsNewOutput = false;
		std::atomic<bool> bIsEnabled{true}; // BUG : Need to have a better look at why we hang when this is false
		bool bReadComplete = false;

		bool bCacheValueHashes = false;

	public:
		FPCGAttributeIdentifier Identifier;
		bool bResetWithFirstValue = false;

		bool IsEnabled() const { return bIsEnabled.load(std::memory_order_acquire); }
		void Disable() { bIsEnabled.store(false, std::memory_order_release); }
		void Enable() { bIsEnabled.store(true, std::memory_order_release); }
		virtual void EnableValueHashCache();

		// Unsafe read value hash from input
		virtual PCGExValueHash ReadValueHash(const int32 Index) = 0;

		// Unsafe read value hash from output
		virtual PCGExValueHash GetValueHash(const int32 Index) = 0;

		// Unsafe read value hash from output
		virtual int32 GetNumValues(const EIOSide InSide = EIOSide::In) = 0;

		const FPCGMetadataAttributeBase* InAttribute = nullptr;
		FPCGMetadataAttributeBase* OutAttribute = nullptr;

		int32 BufferIndex = -1;
		const TSharedRef<FPointIO> Source;

		IBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		FORCEINLINE uint64 GetUID() const { return UID; }
		FORCEINLINE EPCGMetadataTypes GetTypeId() const { return Type; }
		FORCEINLINE EDomainType GetUnderlyingDomain() const { return UnderlyingDomain; }

		template <typename T>
		bool IsA() const;

		virtual ~IBuffer();

		virtual bool EnsureReadable() = 0;
		virtual void Write(const bool bEnsureValidKeys = true) = 0;

		virtual void Fetch(const PCGExMT::FScope& Scope)
		{
		}

		virtual bool IsSparse() const { return false; }

		virtual bool IsWritable() = 0;
		virtual bool IsReadable() = 0;
		virtual bool ReadsFromOutput() = 0;

		virtual void ReadVoid(const int32 Index, void* OutValue) const = 0;
		virtual void SetVoid(const int32 Index, const void* Value) = 0;
		virtual void GetVoid(const int32 Index, void* OutValue) = 0;

		virtual void Flush()
		{
		}

	protected:
		void SetType(const EPCGMetadataTypes InType);
	};

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template bool IBuffer::IsA<_TYPE>() const;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	template <typename T>
	class PCGEXCORE_API TBuffer : public IBuffer
	{
		friend class FFacade;

	public:
		T Min = T{};
		T Max = T{};

		TBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual const FPCGMetadataAttribute<T>* GetTypedInAttribute() const { return nullptr; }
		virtual FPCGMetadataAttribute<T>* GetTypedOutAttribute() const { return nullptr; }

		virtual void ReadVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) override;
		virtual void GetVoid(const int32 Index, void* OutValue) override;

		// Unsafe read from input
		virtual const T& Read(const int32 Index) const = 0;
		virtual const void Read(const int32 Start, TArrayView<T> OutResults) const = 0;

		// Unsafe read from output
		virtual const T& GetValue(const int32 Index) = 0;
		virtual const void GetValues(const int32 Start, TArrayView<T> OutResults) = 0;

		// Unsafe read value hash from input
		virtual PCGExValueHash ReadValueHash(const int32 Index) override;

		// Unsafe read value hash from output
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		// Unsafe set value in output
		virtual void SetValue(const int32 Index, const T& Value) = 0;

		virtual bool InitForRead(const EIOSide InSide = EIOSide::In, const bool bScoped = false) = 0;
		virtual bool InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax = false, const bool bScoped = false, const bool bQuiet = false) = 0;
		virtual bool InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init = EBufferInit::Inherit) = 0;
		virtual bool InitForWrite(const EBufferInit Init = EBufferInit::Inherit) = 0;

		void DumpValues(TArray<T>& OutValues) const;
		void DumpValues(const TSharedPtr<TArray<T>>& OutValues) const;

		static const void* ReadRawImpl(const IBuffer* Self, int32 Index)
		{
			const TBuffer* Buffer = static_cast<const TBuffer*>(Self);
			return &Buffer->Read(Index);
		}

		static const void* GetValueRawImpl(IBuffer* Self, int32 Index)
		{
			TBuffer* Buffer = static_cast<TBuffer*>(Self);
			return &Buffer->GetValue(Index);
		}

		static void SetValueRawImpl(IBuffer* Self, int32 Index, const void* Value)
		{
			TBuffer* Buffer = static_cast<TBuffer*>(Self);
			Buffer->SetValue(Index, *static_cast<const T*>(Value));
		}
	};

#define PCGEX_USING_TBUFFER \
	friend class FFacade;\
	using TBuffer<T>::BufferLock;\
	using TBuffer<T>::Source;\
	using TBuffer<T>::Identifier;\
	using TBuffer<T>::InAttribute;\
	using TBuffer<T>::OutAttribute;\
	using TBuffer<T>::bReadComplete;\
	using TBuffer<T>::IsEnabled;\
	using TBuffer<T>::bCacheValueHashes;

	template <typename T>
	class PCGEXCORE_API TLegacyBuffer : public TBuffer<T>
	{
		friend class FFacade;

	protected:
		const FPCGMetadataAttribute<T>* TypedInAttribute = nullptr;
		FPCGMetadataAttribute<T>* TypedOutAttribute = nullptr;

	public:
		TLegacyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual const FPCGMetadataAttribute<T>* GetTypedInAttribute() const override { return TypedInAttribute; }
		virtual FPCGMetadataAttribute<T>* GetTypedOutAttribute() const override { return TypedOutAttribute; }
	};

#define PCGEX_USING_TLEGACYBUFFER \
	PCGEX_USING_TBUFFER \
	using TLegacyBuffer<T>::TypedInAttribute;\
	using TLegacyBuffer<T>::TypedOutAttribute;

	template <typename T>
	class PCGEXCORE_API TArrayBuffer : public TLegacyBuffer<T>
	{
		PCGEX_USING_TLEGACYBUFFER

	protected:
		// Used to read from an attribute as another type
		TSharedPtr<TAttributeBroadcaster<T>> InternalBroadcaster;
		bool bSparseBuffer = false;

		TSharedPtr<TArray<T>> InValues;
		TSharedPtr<TArray<T>> OutValues;
		TArray<PCGExValueHash> InHashes;

	public:
		TArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual bool IsSparse() const override { return bSparseBuffer || InternalBroadcaster; }

		TSharedPtr<TArray<T>> GetInValues();
		TSharedPtr<TArray<T>> GetOutValues();

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;

		virtual const T& Read(const int32 Index) const override;
		virtual const void Read(const int32 Start, TArrayView<T> OutResults) const override;

		virtual const T& GetValue(const int32 Index) override;
		virtual const void GetValues(const int32 Start, TArrayView<T> OutResults) override;

		virtual void SetValue(const int32 Index, const T& Value) override;
		virtual PCGExValueHash ReadValueHash(const int32 Index) override;

	protected:
		virtual void ComputeValueHashes(const PCGExMT::FScope& Scope);

		virtual void InitForReadInternal(const bool bScoped, const FPCGMetadataAttributeBase* Attribute);
		virtual void InitForWriteInternal(FPCGMetadataAttributeBase* Attribute, const T& InDefaultValue, const EBufferInit Init);

	public:
		virtual bool EnsureReadable() override;
		virtual void EnableValueHashCache() override;

		virtual bool InitForRead(const EIOSide InSide = EIOSide::In, const bool bScoped = false) override;
		virtual bool InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax = false, const bool bScoped = false, const bool bQuiet = false) override;

		virtual bool InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init = EBufferInit::Inherit) override;
		virtual bool InitForWrite(const EBufferInit Init = EBufferInit::Inherit) override;
		virtual void Write(const bool bEnsureValidKeys = true) override;

		virtual void Fetch(const PCGExMT::FScope& Scope) override;

		virtual void Flush() override;
	};

	template <typename T>
	class PCGEXCORE_API TSingleValueBuffer : public TLegacyBuffer<T>
	{
		PCGEX_USING_TLEGACYBUFFER

	protected:
		bool bReadInitialized = false;
		bool bWriteInitialized = false;
		bool bReadFromOutput = false;

		T InValue = T{};
		T OutValue = T{};
		PCGExValueHash Hash = 0;

	public:
		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool EnsureReadable() override;

		TSingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;

		virtual const T& Read(const int32 Index) const override;
		virtual const void Read(const int32 Start, TArrayView<T> OutResults) const override;

		virtual const T& GetValue(const int32 Index) override;
		virtual const void GetValues(const int32 Start, TArrayView<T> OutResults) override;

		virtual void SetValue(const int32 Index, const T& Value) override;

		virtual bool InitForRead(const EIOSide InSide = EIOSide::In, const bool bScoped = false) override;
		virtual bool InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax = false, const bool bScoped = false, const bool bQuiet = false) override;

		virtual bool InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init = EBufferInit::Inherit) override;
		virtual bool InitForWrite(const EBufferInit Init = EBufferInit::Inherit) override;
		virtual void Write(const bool bEnsureValidKeys = true) override;
	};

	//
	// TGenericBuffer<T> - Buffer for known T through FPCGMetadataAttributeGeneric template API.
	// Used when the attribute is generic but the C++ type T is known at compile time.
	//
	template <typename T>
	class PCGEXCORE_API TGenericBuffer : public TBuffer<T>
	{
		PCGEX_USING_TBUFFER

	protected:
		const FPCGMetadataAttributeGeneric* GenericInAttribute = nullptr;
		FPCGMetadataAttributeGeneric* GenericOutAttribute = nullptr;

	public:
		TGenericBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		const FPCGMetadataAttributeGeneric* GetGenericInAttribute() const { return GenericInAttribute; }
		FPCGMetadataAttributeGeneric* GetGenericOutAttribute() const { return GenericOutAttribute; }
	};

#define PCGEX_USING_TGENERICBUFFER \
	PCGEX_USING_TBUFFER \
	using TGenericBuffer<T>::GenericInAttribute;\
	using TGenericBuffer<T>::GenericOutAttribute;

	template <typename T>
	class PCGEXCORE_API TGenericArrayBuffer : public TGenericBuffer<T>
	{
		PCGEX_USING_TGENERICBUFFER

	protected:
		TSharedPtr<TArray<T>> InValues;
		TSharedPtr<TArray<T>> OutValues;

	public:
		TGenericArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual const T& Read(const int32 Index) const override;
		virtual const void Read(const int32 Start, TArrayView<T> OutResults) const override;

		virtual const T& GetValue(const int32 Index) override;
		virtual const void GetValues(const int32 Start, TArrayView<T> OutResults) override;

		virtual void SetValue(const int32 Index, const T& Value) override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) override;
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		virtual bool InitForRead(const EIOSide InSide = EIOSide::In, const bool bScoped = false) override;
		virtual bool InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax = false, const bool bScoped = false, const bool bQuiet = false) override;
		virtual bool InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init = EBufferInit::Inherit) override;
		virtual bool InitForWrite(const EBufferInit Init = EBufferInit::Inherit) override;
		virtual void Write(const bool bEnsureValidKeys = true) override;

		virtual void Flush() override;
	};

	template <typename T>
	class PCGEXCORE_API TGenericSingleValueBuffer : public TGenericBuffer<T>
	{
		PCGEX_USING_TGENERICBUFFER

	protected:
		bool bReadInitialized = false;
		bool bWriteInitialized = false;
		bool bReadFromOutput = false;

		T InValue = T{};
		T OutValue = T{};

	public:
		TGenericSingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual const T& Read(const int32 Index) const override;
		virtual const void Read(const int32 Start, TArrayView<T> OutResults) const override;

		virtual const T& GetValue(const int32 Index) override;
		virtual const void GetValues(const int32 Start, TArrayView<T> OutResults) override;

		virtual void SetValue(const int32 Index, const T& Value) override;

		virtual bool InitForRead(const EIOSide InSide = EIOSide::In, const bool bScoped = false) override;
		virtual bool InitForBroadcast(const FPCGAttributePropertyInputSelector& InSelector, const bool bCaptureMinMax = false, const bool bScoped = false, const bool bQuiet = false) override;
		virtual bool InitForWrite(const T& DefaultValue, bool bAllowInterpolation, EBufferInit Init = EBufferInit::Inherit) override;
		virtual bool InitForWrite(const EBufferInit Init = EBufferInit::Inherit) override;
		virtual void Write(const bool bEnsureValidKeys = true) override;
	};

	//
	// FPropertyBuffer - Non-template buffer for types where T is unknowable at compile time.
	// Uses void* read (GetReadAddressFromEntryKey_Unsafe) + FProperty-based write (SetValueFromProperty).
	// Handles: arbitrary UStructs, UEnums, UObjects, ALL container types (TArray, TSet, TMap).
	//
	class PCGEXCORE_API FPropertyBuffer : public IBuffer
	{
		friend class FFacade;

	protected:
		int32 ElementSize = 0;
		FProperty* CachedInnerProperty = nullptr; // Owned by us, constructed from attribute desc

		const FPCGMetadataAttributeGeneric* GenericInAttribute = nullptr;
		FPCGMetadataAttributeGeneric* GenericOutAttribute = nullptr;

	public:
		FPropertyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);
		virtual ~FPropertyBuffer() override;

		FORCEINLINE int32 GetElementSize() const { return ElementSize; }

		bool InitProperty(const FPCGMetadataAttributeGeneric* InAttribute);

		const FPCGMetadataAttributeGeneric* GetGenericInAttribute() const { return GenericInAttribute; }
		FPCGMetadataAttributeGeneric* GetGenericOutAttribute() const { return GenericOutAttribute; }

	private:
		static FProperty* CreateInnerPropertyFromDesc(const FPCGMetadataAttributeDesc& Desc);
	};

	class PCGEXCORE_API FPropertyArrayBuffer : public FPropertyBuffer
	{
		friend class FFacade;

	protected:
		TSharedPtr<TArray<uint8>> InBytes;
		TSharedPtr<TArray<uint8>> OutBytes;

	public:
		FPropertyArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual void ReadVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) override;
		virtual void GetVoid(const int32 Index, void* OutValue) override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) override;
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		virtual void Write(const bool bEnsureValidKeys = true) override;

		bool InitForRead(const EIOSide InSide = EIOSide::In);
		bool InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init = EBufferInit::Inherit);

		virtual void Flush() override;
	};

	class PCGEXCORE_API FPropertySingleValueBuffer : public FPropertyBuffer
	{
		friend class FFacade;

	protected:
		TArray<uint8> InValue;
		TArray<uint8> OutValue;

		bool bReadInitialized = false;
		bool bWriteInitialized = false;
		bool bReadFromOutput = false;

	public:
		FPropertySingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual void ReadVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) override;
		virtual void GetVoid(const int32 Index, void* OutValue) override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) override;
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		virtual void Write(const bool bEnsureValidKeys = true) override;

		bool InitForRead(const EIOSide InSide = EIOSide::In);
		bool InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init = EBufferInit::Inherit);
	};

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template class TBuffer<_TYPE>;\
extern template class TLegacyBuffer<_TYPE>;\
extern template class TArrayBuffer<_TYPE>;\
extern template class TSingleValueBuffer<_TYPE>;\
extern template class TGenericBuffer<_TYPE>;\
extern template class TGenericArrayBuffer<_TYPE>;\
extern template class TGenericSingleValueBuffer<_TYPE>;

	//PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

	class PCGEXCORE_API FFacade : public TSharedFromThis<FFacade>
	{
		mutable FRWLock BufferLock;

	public:
		TSharedRef<FPointIO> Source;
		int32 Idx = -1;

		TArray<TSharedPtr<IBuffer>> Buffers;
		TMap<uint64, TSharedPtr<IBuffer>> BufferMap;

		TMap<FName, FName> WritableRemap; // TODO : Manage remapping in the facade directly to remove the need for dummy attributes

		bool bSupportsScopedGet = false;

		int32 GetNum(const EIOSide InSide = EIOSide::In) const;

		TSharedPtr<IBuffer> FindBuffer_Unsafe(const uint64 UID);
		TSharedPtr<IBuffer> FindBuffer(const uint64 UID);
		TSharedPtr<IBuffer> FindReadableAttributeBuffer(const FPCGAttributeIdentifier& InIdentifier);
		TSharedPtr<IBuffer> FindWritableAttributeBuffer(const FPCGAttributeIdentifier& InIdentifier);

		EPCGPointNativeProperties GetAllocations() const;

		FPCGExContext* GetContext() const;

		explicit FFacade(const TSharedRef<FPointIO>& InSource);
		~FFacade() = default;

		bool IsDataValid(const EIOSide InSide) const;
		bool ShareSource(const FFacade* OtherManager) const;

		template <typename T>
		TSharedPtr<TBuffer<T>> FindBuffer_Unsafe(const FPCGAttributeIdentifier& InIdentifier);

		template <typename T>
		TSharedPtr<TBuffer<T>> FindBuffer(const FPCGAttributeIdentifier& InIdentifier);

		template <typename T>
		TSharedPtr<TBuffer<T>> GetBuffer(const FPCGAttributeIdentifier& InIdentifier);


#pragma region Writable

		template <typename T>
		TSharedPtr<TBuffer<T>> GetWritable(const FPCGAttributeIdentifier& InIdentifier, T DefaultValue, bool bAllowInterpolation, EBufferInit Init);

		template <typename T>
		TSharedPtr<TBuffer<T>> GetWritable(const FPCGMetadataAttribute<T>* InAttribute, EBufferInit Init);

		template <typename T>
		TSharedPtr<TBuffer<T>> GetWritable(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init);

		TSharedPtr<IBuffer> GetWritable(EPCGMetadataTypes Type, const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init);
		TSharedPtr<IBuffer> GetWritable(EPCGMetadataTypes Type, const FName InName, EBufferInit Init);

#pragma endregion

#pragma region Readable

		template <typename T>
		TSharedPtr<TBuffer<T>> GetReadable(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In, const bool bSupportScoped = false);

		TSharedPtr<IBuffer> GetReadable(const FAttributeIdentity& Identity, const EIOSide InSide = EIOSide::In, const bool bSupportScoped = false);

		TSharedPtr<IBuffer> GetDefaultReadable(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In, const bool bSupportScoped = false);

#pragma endregion

#pragma region Broadcasters

		template <typename T>
		TSharedPtr<TBuffer<T>> GetBroadcaster(const FPCGAttributePropertyInputSelector& InSelector, const bool bSupportScoped = false, const bool bCaptureMinMax = false, const bool bQuiet = false);

		template <typename T>
		TSharedPtr<TBuffer<T>> GetBroadcaster(const FName InName, const bool bSupportScoped = false, const bool bCaptureMinMax = false, const bool bQuiet = false);

#pragma endregion

		FPCGMetadataAttributeBase* FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		const FPCGMetadataAttributeBase* FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		template <typename T>
		FPCGMetadataAttribute<T>* FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		template <typename T>
		const FPCGMetadataAttribute<T>* FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		const UPCGBasePointData* GetData(const EIOSide InSide) const;
		const UPCGBasePointData* GetIn() const;
		UPCGBasePointData* GetOut() const;

		void CreateReadables(const TArray<FAttributeIdentity>& Identities, const bool bWantsScoped = true);
		void MarkCurrentBuffersReadAsComplete();

		void Flush();

		void Write(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const bool bEnsureValidKeys = true);
		FPlatformTypes::int32 WriteBuffersAsCallbacks(const TSharedPtr<PCGExMT::FTaskGroup>& TaskGroup);
		void WriteBuffers(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, PCGExMT::FCompletionCallback&& Callback);
		int32 WriteSynchronous(const bool bEnsureValidKeys = true);
		void WriteFastest(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const bool bEnsureValidKeys = true);

		void Fetch(const PCGExMT::FScope& Scope);

		FConstPoint GetInPoint(const int32 Index) const;
		FMutablePoint GetOutPoint(const int32 Index) const;

		FScope GetInScope(const int32 Start, const int32 Count, const bool bInclusive = true) const;
		FScope GetInScope(const PCGExMT::FScope& Scope) const;
		FScope GetInFullScope() const;
		FScope GetInRange(const int32 Start, const int32 End, const bool bInclusive = true) const;

		FScope GetOutScope(const int32 Start, const int32 Count, const bool bInclusive = true) const;
		FScope GetOutScope(const PCGExMT::FScope& Scope) const;
		FScope GetOutFullScope() const;
		FScope GetOutRange(const int32 Start, const int32 End, const bool bInclusive = true) const;

	protected:
		template <typename Func>
		void ForEachWritable(Func&& Callback)
		{
			for (int i = 0; i < Buffers.Num(); i++)
			{
				const TSharedPtr<IBuffer> Buffer = Buffers[i];
				if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled()) { continue; }
				Callback(Buffer);
			}
		}

		bool ValidateOutputsBeforeWriting() const;
		void Flush(const TSharedPtr<IBuffer>& Buffer);
	};

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::FindBuffer_Unsafe<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::FindBuffer<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetBuffer<_TYPE>(const FPCGAttributeIdentifier& InIdentifier); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, _TYPE DefaultValue, bool bAllowInterpolation, EBufferInit Init); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGMetadataAttribute<_TYPE>* InAttribute, EBufferInit Init); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetReadable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide, const bool bSupportScoped); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FPCGAttributePropertyInputSelector& InSelector, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FName InName, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
extern template FPCGMetadataAttribute<_TYPE>* FFacade::FindMutableAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const; \
extern template const FPCGMetadataAttribute<_TYPE>* FFacade::FindConstAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region Data Marking

	template <typename T>
	FPCGMetadataAttribute<T>* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, T MarkValue);

	template <typename T>
	FPCGMetadataAttribute<T>* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T MarkValue);

	template <typename T>
	bool TryReadMark(UPCGMetadata* Metadata, const FPCGAttributeIdentifier& MarkID, T& OutMark);

	template <typename T>
	bool TryReadMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T& OutMark);

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template FPCGMetadataAttribute<_TYPE>* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, _TYPE MarkValue); \
extern template FPCGMetadataAttribute<_TYPE>* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, _TYPE MarkValue); \
extern template bool TryReadMark<_TYPE>(UPCGMetadata* Metadata, const FPCGAttributeIdentifier& MarkID, _TYPE& OutMark); \
extern template bool TryReadMark<_TYPE>(const TSharedRef<FPointIO>& PointIO, const FName MarkID, _TYPE& OutMark);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

	PCGEXCORE_API
	void WriteId(const TSharedRef<FPointIO>& PointIO, const FName IdName, const int64 Id);

	PCGEXCORE_API
	UPCGBasePointData* GetMutablePointData(FPCGContext* Context, const FPCGTaggedData& Source);

#pragma endregion

	PCGEXCORE_API
	TSharedPtr<FFacade> TryGetSingleFacade(FPCGExContext* InContext, const FName InputPinLabel, bool bTransactional, const bool bRequired);

	PCGEXCORE_API
	bool TryGetFacades(FPCGExContext* InContext, const FName InputPinLabel, TArray<TSharedPtr<FFacade>>& OutFacades, const bool bRequired, const bool bIsTransactional = false);

	PCGEXCORE_API
	void WriteBuffer(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<IBuffer>& InBuffer, const bool InEnsureValidKeys = true);
}
