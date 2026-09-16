// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// PCGExData.h includes this header at its tail; #pragma once keeps the cycle safe from either entry point.
#include "Data/PCGExData.h"

//
// TArrayBuffer<T> / TSingleValueBuffer<T>
//
// Unified buffers using FPCGMetadataAttributeBase* (from IBuffer).
// Attribute creation goes through Domain->FindOrCreateAttribute<T>() -- the UE 5.8 canonical path.
//
// FPCGMetadataAttribute<T> still exists for basic types; use GetTypedInAttribute()/GetTypedOutAttribute()
// on TBuffer<T> for a convenience static_cast when you need the typed pointer.
//

namespace PCGExData
{
	template <typename T>
	class PCGEXCORE_API TArrayBuffer : public TBuffer<T>
	{
		PCGEX_USING_TBUFFER

	protected:
		// Used to read from an attribute as another type
		TSharedPtr<TAttributeBroadcaster<T>> InternalBroadcaster;
		bool bSparseBuffer = false;

		TSharedPtr<TArray<T>> InValues;
		TSharedPtr<TArray<T>> OutValues;
		TArray<PCGExValueHash> InHashes;

	public:
		TArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier);

		virtual bool IsSparse() const override
		{
			return bSparseBuffer || InternalBroadcaster;
		}

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
		// No-op unless the cache is enabled; call once a bulk read completes.
		void ComputeAllValueHashes();
		// Sole path that makes InValues alias OutValues; drops the hash cache since aliased reads are live.
		void AliasInputToOutput();

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
	class PCGEXCORE_API TSingleValueBuffer : public TBuffer<T>
	{
		PCGEX_USING_TBUFFER

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
