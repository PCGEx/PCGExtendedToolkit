// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// NOTE: This header is included at the bottom of PCGExData.h — do NOT include PCGExData.h here.
// All base types (IBuffer, FFacade, etc.) are already visible.

//
// FPropertyBuffer hierarchy — Tier 3: truly opaque attribute types.
//
// Use this ONLY when T is unknowable at compile time (arbitrary UStructs, UEnums, UObjects).
// If T is known, prefer TGenericBuffer<T> (Tier 2) — it's type-safe, faster, and supports
// the accessor API for bulk reads/writes.
//
// Container types (TArray, TSet, TMap) with KNOWN element types should use
// TGenericBuffer<ContainerT> (Tier 2), not FPropertyBuffer. FPropertyBuffer handles
// containers only when the element type is also unknown.
//
// Read path:  GetReadAddressFromEntryKey_Unsafe() → void* memcpy per element
// Write path: SetValueFromProperty(EntryKey, void*, FProperty*) per element
//
// The per-element write is sequential — acceptable since truly opaque types are rare
// in hot paths. For bulk operations on known types, Tier 2's accessor-based path is faster.
//
// ── UE 5.8 migration notes ──
//
// FPropertyBuffer is migration-safe: it uses FPCGMetadataAttributeGeneric's public
// void* API which is independent of the typed→generic unification. No changes expected.
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

		const FPCGMetadataAttributeGeneric* GenericInAttribute = nullptr;
		FPCGMetadataAttributeGeneric* GenericOutAttribute = nullptr;

	public:
		FPropertyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);
		virtual ~FPropertyBuffer() override;

		FORCEINLINE int32 GetElementSize() const { return ElementSize; }

		bool InitProperty(const FPCGMetadataAttributeGeneric* InGenericAttribute);

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
}
