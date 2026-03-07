// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// NOTE: This header is included at the bottom of PCGExData.h — do NOT include PCGExData.h here.
// All base types (IBuffer, TBuffer, TGenericBuffer, FFacade, etc.) are already visible.

//
// TGenericArrayBuffer<T> / TGenericSingleValueBuffer<T>
//
// Tier 2 buffers for known T through FPCGMetadataAttributeGeneric template API.
// Use when the attribute is stored as generic but the C++ type T is known at compile time.
//
// These are instantiated via PCGEX_FOREACH_SUPPORTEDTYPES for all standard PCGEx types.
//
// ── Container support (TArray<T>, TSet<T>, TMap<K,V>) ──
//
// Container types work as-is through TGenericBuffer<ContainerT> — e.g. TGenericArrayBuffer<TArray<int32>>.
// No separate container buffer classes are needed. The generic attribute API handles them:
//   Write: GenericAttribute->SetValues<TArray<int32>>(EntryKeys, Values)
//   Read:  GenericAttribute->GetValuesFromItemKeys<TConstArrayView<int32>>(EntryKeys, OutValues)
//
// Key detail: container read types differ from write types:
//   TArray<T>    → reads as TConstArrayView<T>
//   TSet<T>      → reads as PCG::TScriptSetWrapper<T>
//   TMap<K,V>    → reads as PCG::TScriptMapWrapper<K,V>
//
// For callers that need bulk container reads, access the generic attribute directly:
//   buffer->GetGenericInAttribute()->GetValuesFromItemKeys<ReadType>(EntryKeys, OutValues)
//
// Container buffer instantiations are NOT added speculatively — add them on-demand
// to PCGExBufferGeneric.cpp's externalization block when a caller needs them.
//
// ── UE 5.8 API migration notes ──
//
// Adrien (Epic) confirmed the generic API is the future direction:
//   - Typed attributes will internally call the generic API
//   - Accessor API update will follow
//   - Attribute constructors becoming protected (only creatable from metadata/domain)
//
// Our usage of CreateGenericAttribute<T>() through FPCGMetadataDomain is the sanctioned
// creation path and should survive the transition. The @todo_pcg TO BE REMOVED comment
// on CreateGenericAttribute refers to removing the OLD typed creation path, not this API.
//
// When the migration lands (~1 month from March 2026):
//   - TLegacyBuffer<T> continues working (typed API becomes thin wrapper over generic)
//   - TGenericBuffer<T> continues working (already on the target API)
//   - No changes expected in this file
//

namespace PCGExData
{
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
}
