// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// NOTE: This header is included at the bottom of PCGExData.h -- do NOT include PCGExData.h here.
// All base types (IBuffer, FFacade, etc.) are already visible.

//
// FPropertyBuffer hierarchy -- Tier 3: truly opaque attribute types.
//
// Use this ONLY when T is unknowable at compile time (arbitrary UStructs, UEnums, UObjects).
// If T is known, prefer TBuffer<T> (Tier 2) -- it's type-safe, faster, and supports
// the accessor API for bulk reads/writes.
//
// Container types (TArray, TSet, TMap) with KNOWN element types should use
// TBuffer<ContainerT>, not FPropertyBuffer. FPropertyBuffer handles
// containers only when the element type is also unknown.
//
// Read path:  GetReadAddressFromEntryKey_Unsafe() → void* memcpy per element
// Write path: SetValueFromProperty(EntryKey, void*, FProperty*) per element
//
// The per-element write is sequential -- acceptable since truly opaque types are rare
// in hot paths. For bulk operations on known types, Tier 2's accessor-based path is faster.
//

namespace PCGExData
{
	//
	// FPropertyBuffer - Non-template buffer for types where T is unknowable at compile time.
	// Uses void* read (GetReadAddressFromEntryKey_Unsafe) + FProperty-based write (SetValueFromProperty).
	// Handles: arbitrary UStructs, UEnums, UObjects, container types when element type is unknown.
	//
	class PCGEXCORE_API FPropertyBuffer : public IBuffer
	{
		friend class FFacade;

	protected:
		int32 ElementSize = 0;
		FProperty* CachedInnerProperty = nullptr; // Owned by us, constructed from attribute desc

		const FPCGMetadataAttributeBase* GenericInAttribute = nullptr;
		FPCGMetadataAttributeBase* GenericOutAttribute = nullptr;

	public:
		FPropertyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);
		virtual ~FPropertyBuffer() override;

		FORCEINLINE int32 GetElementSize() const
		{
			return ElementSize;
		}

		bool InitProperty(const FPCGMetadataAttributeBase* InGenericAttribute);

		const FPCGMetadataAttributeBase* GetGenericInAttribute() const
		{
			return GenericInAttribute;
		}

		FPCGMetadataAttributeBase* GetGenericOutAttribute() const
		{
			return GenericOutAttribute;
		}

		// Non-owning accessor to the cached property that drives this buffer's reads/writes.
		// Lifetime-tied to the buffer itself -- do not retain past the buffer's lifetime.
		// Prefer IBuffer::GetSourceProperty() at call sites that only hold an IBuffer pointer;
		// this stays for backwards compatibility with code that already has an FPropertyBuffer.
		const FProperty* GetCachedProperty() const
		{
			return CachedInnerProperty;
		}

		virtual const FProperty* GetSourceProperty() const override
		{
			return CachedInnerProperty;
		}

		virtual int32 GetValueSize() const override { return ElementSize; }
		virtual int32 GetValueAlignment() const override { return CachedInnerProperty ? CachedInnerProperty->GetMinAlignment() : 1; }

		// Runtime type via reflection -- FScopedTypedValue(FProperty*) handles arbitrary UStructs/UEnums/etc.
		// Precondition: CachedInnerProperty is valid (i.e. InitForRead or InitForWrite succeeded).
		virtual PCGExTypes::FScopedTypedValue MakeScopedValue() const override
		{
			check(CachedInnerProperty);
			return PCGExTypes::FScopedTypedValue(CachedInnerProperty);
		}

		// Static factory: build an FProperty matching the attribute's full descriptor.
		// Handles container types (TArray/TSet/TMap), scalar legacy types, struct/enum, and
		// the Object family (Object/SoftObject/Class/SoftClass -- transient processing only;
		// values are not GC-tracked, do not persist outputs to UObject-owned storage).
		// Returns nullptr if the desc cannot be mapped to a property.
		// PropertyScope: pass the parent FProperty when constructing nested container inner
		// properties; default (nullptr scope) is correct for top-level use.
		static FProperty* CreateInnerPropertyFromDesc(const FPCGMetadataAttributeDesc& Desc, FFieldVariant PropertyScope = FFieldVariant{nullptr});

		// Desc-aware element size / alignment. Returns the storage footprint of one element of
		// the attribute described by Desc, including container wrapping (sizeof(FScriptArray) etc.).
		// Returns 0/1 if the desc cannot be mapped.
		static int32 GetElementSizeFromDesc(const FPCGMetadataAttributeDesc& Desc);
		static int32 GetElementAlignmentFromDesc(const FPCGMetadataAttributeDesc& Desc);

		// Container-accessor helpers.
		//
		// Returns the size of ONE element inside the outermost container of Desc.
		// For TArray<FVector>: strips the [Array] wrapper and asks for Vector's
		// element size -> 24. Non-container descs return GetElementSizeFromDesc
		// directly. Zero if the desc cannot be mapped.
		static int32 GetInnerElementSizeFromDesc(const FPCGMetadataAttributeDesc& Desc);

		// Read the element count from a container attribute's raw bytes.
		// Works for TArray, TSet, and TMap -- all three UE container types
		// store a layout-compatible element count at the same binary offset
		// (the Num field in the underlying FScriptArray / FScriptSet /
		// FScriptMap). Returns 0 if ContainerBytes is null.
		static int32 GetContainerNum(const void* ContainerBytes);

		// Return a pointer to the Index-th element of a TArray-layout
		// attribute value. ElementSize must match sizeof(T). Returns nullptr
		// for out-of-range Index, negative Index, or null ArrayBytes. The
		// returned pointer is valid only while the containing array value is
		// alive -- callers typically memcpy the element out.
		// NOTE: Only valid for Array containers (TArray). Set and Map do not
		// store elements contiguously by index.
		static const void* GetArrayElementAt(const void* ArrayBytes, int32 Index, int32 ElementSize);

		// Mutable variant of GetArrayElementAt for the write path. Same
		// semantics and safety checks. Used by FContainerIndexAccessor's
		// StepSetFn to obtain a writable destination for CopyCompleteValue.
		static void* GetMutableArrayElementAt(void* ArrayBytes, int32 Index, int32 ElementSize);

	protected:
		// Content hash: FProperty::GetValueTypeHash when supported (CPF_HasGetValueTypeHash), CRC of
		// the raw bytes otherwise (container wrappers and FText fall back to CRC over header bytes).
		PCGExValueHash HashValue(const void* ValuePtr) const;

	private:
		// Unvalidated worker -- all construction, including recursion, re-enters the public wrapper,
		// which gates GetSize() > 0 on each constructed property.
		static FProperty* CreateInnerPropertyFromDescInternal(const FPCGMetadataAttributeDesc& Desc, FFieldVariant PropertyScope);
	};

	class PCGEXCORE_API FPropertyArrayBuffer : public FPropertyBuffer
	{
		friend class FFacade;

	protected:
		TSharedPtr<TArray<uint8>> InBytes;
		TSharedPtr<TArray<uint8>> OutBytes;

	public:
		FPropertyArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);
		virtual ~FPropertyArrayBuffer() override;

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual void ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) const override;
		virtual void SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value) override;
		virtual void GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) override;

		// Single deep copy against the byte slot -- see IBuffer for the live-destination contract.
		virtual void ReadRawValue(const int32 Index, void* Dst) const override;
		virtual void GetRawValue(const int32 Index, void* Dst) override;
		virtual void SetRawValue(const int32 Index, const void* Src) override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) override;
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		virtual void Write(const bool bEnsureValidKeys = true) override;

		bool InitForRead(const EIOSide InSide = EIOSide::In);
		bool InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init = EBufferInit::Inherit);

		// Property-aware write at a specific index. SrcPtr must point to bytes matching
		// CachedInnerProperty's layout (typically obtained from an attribute with the same desc).
		// Performs deep-copy via FProperty::CopyCompleteValue -- correct for containers, FString, etc.
		// No-op if the buffer isn't initialized for write or SrcPtr is null.
		void SetFromVoidProperty(int32 Index, const void* SrcPtr);

		virtual void Flush() override;

	protected:
		// Walk the byte array and call CachedInnerProperty->DestroyValue on each element slot.
		// Required for property-backed buffers because per-element CopyCompleteValue / InitializeValue
		// allocate per-element state (FString chars, FScriptArray storage, etc.) that the byte
		// array's destructor doesn't know how to release.
		void DestroyAllElements(const TSharedPtr<TArray<uint8>>& Bytes) const;
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
		virtual ~FPropertySingleValueBuffer() override;

		virtual int32 GetNumValues(const EIOSide InSide) override;

		virtual bool IsWritable() override;
		virtual bool IsReadable() override;
		virtual bool ReadsFromOutput() override;
		virtual bool EnsureReadable() override;

		virtual void ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) const override;
		virtual void SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value) override;
		virtual void GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) override;

		// Single deep copy against the value slot -- see IBuffer for the live-destination contract.
		virtual void ReadRawValue(const int32 Index, void* Dst) const override;
		virtual void GetRawValue(const int32 Index, void* Dst) override;
		virtual void SetRawValue(const int32 Index, const void* Src) override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) override;
		virtual PCGExValueHash GetValueHash(const int32 Index) override;

		virtual void Write(const bool bEnsureValidKeys = true) override;

		virtual void Flush() override;

		bool InitForRead(const EIOSide InSide = EIOSide::In);
		bool InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init = EBufferInit::Inherit);

	private:
		// Single owner of the read-aliases-write routing: bReadFromOutput serves reads from OutValue
		// (InValue stays empty). All read-side paths must come through here, never InValue directly.
		const TArray<uint8>& GetReadSource() const
		{
			return bReadFromOutput ? OutValue : InValue;
		}
	};
}
