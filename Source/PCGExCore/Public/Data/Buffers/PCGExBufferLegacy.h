// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// NOTE: This header is included at the bottom of PCGExData.h — do NOT include PCGExData.h here.
// All base types (IBuffer, TBuffer, TLegacyBuffer, FFacade, etc.) are already visible.

//
// TArrayBuffer<T> / TSingleValueBuffer<T>
//
// Tier 1 (Legacy) buffers for standard FPCGMetadataAttribute<T> typed attributes.
// This is the existing, well-tested path used by the vast majority of PCGEx nodes.
//
// ── UE 5.8 migration notes ──
//
// When Epic unifies typed→generic internally, these buffers continue to work:
// FPCGMetadataAttribute<T> becomes a thin wrapper over FPCGMetadataAttributeGeneric,
// and CreateAttribute<T>() routes through the generic backend.
//
// No changes needed here unless Epic removes FPCGMetadataAttribute<T> entirely,
// which is unlikely given the public API surface. If that happens, migrate callers
// to TGenericBuffer<T> (Tier 2) which already uses the generic API directly.
//

namespace PCGExData
{
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
}
