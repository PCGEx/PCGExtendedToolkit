// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

#include "PCGCommon.h"
#include "PCGExCommon.h"
#include "PCGExDataCommon.h"
#include "PCGExDataMacros.h"
#include "PCGExPointElements.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Types/PCGExTypes.h"


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

template <typename T>
class FPCGMetadataAttribute;

class FPCGMetadataAttributeBase;
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
	class IBufferProxyPool;

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

		bool IsEnabled() const
		{
			return bIsEnabled.load(std::memory_order_acquire);
		}

		void Disable()
		{
			bIsEnabled.store(false, std::memory_order_release);
		}

		void Enable()
		{
			bIsEnabled.store(true, std::memory_order_release);
		}

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

		FORCEINLINE uint64 GetUID() const
		{
			return UID;
		}

		FORCEINLINE EPCGMetadataTypes GetTypeId() const
		{
			return Type;
		}

		FORCEINLINE EDomainType GetUnderlyingDomain() const
		{
			return UnderlyingDomain;
		}

		template <typename T>
		bool IsA() const;

		virtual ~IBuffer();

		virtual bool EnsureReadable() = 0;
		virtual void Write(const bool bEnsureValidKeys = true) = 0;

		virtual void Fetch(const PCGExMT::FScope& Scope)
		{
		}

		virtual bool IsSparse() const
		{
			return false;
		}

		virtual bool IsWritable() = 0;
		virtual bool IsReadable() = 0;
		virtual bool ReadsFromOutput() = 0;

		virtual void ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) const = 0;
		virtual void SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value) = 0;
		virtual void GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) = 0;

		// Creates a FScopedTypedValue appropriately sized & constructed for this buffer's type.
		// Callers don't need to know EPCGMetadataTypes -- the buffer knows its own type.
		// TBuffer<T> uses compile-time TTraits<T>::Type; FPropertyBuffer uses its cached FProperty.
		virtual PCGExTypes::FScopedTypedValue MakeScopedValue() const = 0;

		// Raw-pointer value transport. Src/Dst MUST point to a live value of this buffer's value type
		// (e.g. built by MakeScopedValue) -- these are assignments, not byte copies. Defaults route
		// through a scoped intermediate; property buffers override with a single direct deep copy.
		virtual void ReadRawValue(const int32 Index, void* Dst) const
		{
			PCGExTypes::FScopedTypedValue Wrapped = MakeScopedValue();
			ReadVoid(Index, Wrapped);
			Wrapped.ExportTo(Dst);
		}

		virtual void GetRawValue(const int32 Index, void* Dst)
		{
			PCGExTypes::FScopedTypedValue Wrapped = MakeScopedValue();
			GetVoid(Index, Wrapped);
			Wrapped.ExportTo(Dst);
		}

		virtual void SetRawValue(const int32 Index, const void* Src)
		{
			PCGExTypes::FScopedTypedValue Wrapped = MakeScopedValue();
			Wrapped.ImportFrom(Src);
			SetVoid(Index, Wrapped);
		}

		// Source FProperty backing this buffer. nullptr for typed TBuffer<T>; non-null for
		// FPropertyBuffer post-InitProperty. Use for FProperty-aware operations (deep copy via
		// CopyCompleteValue, sized scoped values). Lifetime tied to this buffer.
		virtual const FProperty* GetSourceProperty() const { return nullptr; }

		// Element size/alignment of one stored value, in bytes. TBuffer<T> reports sizeof(T)/alignof(T);
		// FPropertyBuffer reports its cached element size + the inner property's minimum alignment
		// (correctly sized for container wrappers and struct/enum/object types).
		virtual int32 GetValueSize() const = 0;
		virtual int32 GetValueAlignment() const = 0;

		// Attribute descriptor for this buffer. Always non-null -- synthetic Desc on TBuffer<T>
		// (Name + TTraits<T>::Type), real Desc on FPropertyBuffer (carries ContainerTypes,
		// ValueTypeObject, KeyType). Use for shape introspection without downcasting.
		// Read-only pointer to internal state -- do not mutate.
		FORCEINLINE const FPCGMetadataAttributeDesc* GetDesc() const { return &Desc; }

		// True iff this buffer is property-backed (FPropertyArrayBuffer / FPropertySingleValueBuffer
		// fallback path for extended/container types). Derived from GetSourceProperty -- typed
		// TBuffer<T> never carries an FProperty.
		FORCEINLINE bool IsPropertyBacked() const { return GetSourceProperty() != nullptr; }

		virtual void Flush()
		{
		}

	protected:
		void SetType(const EPCGMetadataTypes InType);

		// Attribute descriptor cache -- populated by subclass constructors / Init paths.
		// Exposed read-only via GetDesc(); see comment there.
		FPCGMetadataAttributeDesc Desc;
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

		bool bMinMaxCaptured = false;

		TBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual void ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) const override;
		virtual void SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value) override;
		virtual void GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) override;

		// Compile-time typed: PCGExTypes::TTraits<T>::Type is resolved at template instantiation,
		// so no EPCGMetadataTypes enum value ever surfaces at the caller.
		virtual PCGExTypes::FScopedTypedValue MakeScopedValue() const override
		{
			return PCGExTypes::FScopedTypedValue(PCGExTypes::TTraits<T>::Type);
		}

		virtual int32 GetValueSize() const override { return sizeof(T); }
		virtual int32 GetValueAlignment() const override { return alignof(T); }

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

	// Forward declarations for buffer leaf classes (defined in Buffers/ headers)
	template <typename T>
	class TArrayBuffer;
	template <typename T>
	class TSingleValueBuffer;
	class FPropertyBuffer;
	class FPropertyArrayBuffer;
	class FPropertySingleValueBuffer;

	class PCGEXCORE_API FFacade : public TSharedFromThis<FFacade>
	{
		mutable FRWLock BufferLock;

		// Lazily-created proxy cache, scoped to this facade's lifetime. Only facades that take
		// part in proxy-based blending ever allocate it. See IBufferProxyPool / GetProxyPool.
		mutable FRWLock ProxyPoolLock;
		TSharedPtr<IBufferProxyPool> ProxyPool;

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

		// Facade-local proxy cache (lazily created, thread-safe). Used by GetProxyBuffer when a
		// Shared descriptor resolves to this facade.
		IBufferProxyPool& GetProxyPool();

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
		TSharedPtr<TBuffer<T>> GetWritable(const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init);

		template <typename T>
		TSharedPtr<TBuffer<T>> GetWritable(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init);

		TSharedPtr<IBuffer> GetWritable(EPCGMetadataTypes Type, const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init);
		TSharedPtr<IBuffer> GetWritable(EPCGMetadataTypes Type, const FName InName, EBufferInit Init);
		// Identifier-driven: preserves an explicit MetadataDomain (e.g. forcing Elements). Unlike the FName overload,
		// it does NOT sanitize the domain against existing attributes -- so it won't resolve a same-named @Data attribute.
		TSharedPtr<IBuffer> GetWritable(EPCGMetadataTypes Type, const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init);

		// Attribute-driven writable lookup -- callers don't need to extract EPCGMetadataTypes.
		// Internally delegates to GetWritable(EPCGMetadataTypes, FPCGMetadataAttributeBase*, EBufferInit).
		TSharedPtr<IBuffer> GetWritableFromAttribute(const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init);

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
		FPCGMetadataAttributeBase* FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		template <typename T>
		const FPCGMetadataAttributeBase* FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide = EIOSide::In) const;

		const UPCGBasePointData* GetData(const EIOSide InSide) const;
		const UPCGBasePointData* GetIn() const;
		UPCGBasePointData* GetOut() const;

		void CreateReadables(const TArray<FAttributeIdentity>& Identities, const bool bWantsScoped = true);
		void MarkCurrentBuffersReadAsComplete();

		void Flush();

		void Write(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const bool bEnsureValidKeys = true);

		// Returns one write callback per writable buffer. Empty if validation fails or there are
		// no writable buffers. Caller owns the dispatch decision -- in particular, this exists so
		// callers can decide whether to spin up a TaskGroup at all (creating one without queuing
		// work registers an orphan token that the task manager waits on indefinitely).
		TArray<PCGExMT::FSimpleCallback> GetWriteBufferCallbacks();

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
				if (!Buffer.IsValid() || !Buffer->IsWritable() || !Buffer->IsEnabled())
				{
					continue;
				}
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
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGMetadataAttributeBase* InAttribute, EBufferInit Init); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetWritable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, EBufferInit Init); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetReadable<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide, const bool bSupportScoped); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FPCGAttributePropertyInputSelector& InSelector, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
extern template TSharedPtr<TBuffer<_TYPE>> FFacade::GetBroadcaster<_TYPE>(const FName InName, const bool bSupportScoped, const bool bCaptureMinMax, const bool bQuiet); \
extern template FPCGMetadataAttributeBase* FFacade::FindMutableAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const; \
extern template const FPCGMetadataAttributeBase* FFacade::FindConstAttribute<_TYPE>(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

#pragma region Data Marking

	template <typename T>
	FPCGMetadataAttributeBase* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, T MarkValue);

	template <typename T>
	FPCGMetadataAttributeBase* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T MarkValue);

	template <typename T>
	bool TryReadMark(UPCGMetadata* Metadata, const FPCGAttributeIdentifier& MarkID, T& OutMark);

	template <typename T>
	bool TryReadMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, T& OutMark);

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template FPCGMetadataAttributeBase* WriteMark(UPCGData* InData, const FPCGAttributeIdentifier& MarkID, _TYPE MarkValue); \
extern template FPCGMetadataAttributeBase* WriteMark(const TSharedRef<FPointIO>& PointIO, const FName MarkID, _TYPE MarkValue); \
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

// Buffer leaf class definitions -- must come after FFacade and base buffer declarations
#include "Data/Buffers/PCGExBuffer.h"
#include "Data/Buffers/PCGExBufferProperty.h"
