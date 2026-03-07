// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.h"
#include "PCGExProxyData.h"
#include "Types/PCGExTypes.h"
#include "Data/PCGExSubSelection.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Types/PCGExTypeOps.h"
#include "UObject/Object.h"
#include "Data/PCGExCachedSubSelection.h"

struct FPCGExContext;
class UPCGBasePointData;

template <typename T>
class FPCGMetadataAttribute;

namespace PCGExData
{
	class IBuffer;

	template <typename T>
	class TBuffer;

	//
	// TRawBufferProxy - Proxy that owns raw data
	// Single template parameter (T_REAL) - working type handled via runtime conversion
	//
	template <typename T_REAL>
	class TRawBufferProxy : public IBufferProxy
	{
	public:
		TSharedPtr<TArray<T_REAL>> Buffer;

		explicit TRawBufferProxy(EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;
	};

#pragma region externalization TAttributeBufferProxy

#define PCGEX_TPL(_TYPE, _NAME, ...) extern template class TRawBufferProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// TAttributeBufferProxy - Proxy for attribute buffers
	// Single template parameter (T_REAL) - working type handled via runtime conversion
	//
	template <typename T_REAL>
	class TAttributeBufferProxy : public IBufferProxy
	{
	public:
		TSharedPtr<TBuffer<T_REAL>> Buffer;

		explicit TAttributeBufferProxy(EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;
		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const override;

		virtual TSharedPtr<IBuffer> GetBuffer() const override;
		virtual bool EnsureReadable() const override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;
	};

#pragma region externalization TAttributeBufferProxy

#define PCGEX_TPL(_TYPE, _NAME, ...) extern template class TAttributeBufferProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// FPointPropertyProxy - Runtime property dispatch proxy
	//
	class PCGEXCORE_API FPointPropertyProxy : public IBufferProxy
	{
	protected:
		EPCGPointProperties Property = EPCGPointProperties::Density;
		EPCGMetadataTypes PropertyRealType = EPCGMetadataTypes::Unknown;

	public:
		FPointPropertyProxy(EPCGPointProperties InProperty, EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;
		virtual void InitForRole(EProxyRole InRole) override;
		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;

	protected:
		void GetPropertyValue(const int32 Index, void* OutValue) const;
		void SetPropertyValue(const int32 Index, const void* Value) const;
	};

	//
	// FPointExtraPropertyProxy - Extra properties like Index
	//
	class PCGEXCORE_API FPointExtraPropertyProxy : public IBufferProxy
	{
	protected:
		EPCGExtraProperties Property = EPCGExtraProperties::Index;

	public:
		FPointExtraPropertyProxy(EPCGExtraProperties InProperty, EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;

		virtual void SetVoid(const int32 Index, const void* Value) const override
		{
		}

		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;

		static EPCGMetadataTypes GetPropertyType(EPCGExtraProperties InProperty);
	};

	//
	// TConstantProxy - Constant value proxy
	//
	template <typename T_CONST>
	class TConstantProxy : public IBufferProxy
	{
		T_CONST Constant = T_CONST{};

	public:
		explicit TConstantProxy(EPCGMetadataTypes InWorkingType);

		template <typename T>
		void SetConstant(const T& InValue);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override { check(false); }
		virtual bool Validate(const FProxyDescriptor& InDescriptor) const override;
		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;
	};

#pragma region externalization TConstantProxy

#define PCGEX_TPL(_TYPE, _NAME, ...) extern template class TConstantProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#define PCGEX_TPL(_TYPE_A, _NAME_A, _TYPE_B, _NAME_B, ...) \
	extern template void TConstantProxy<_TYPE_A>::SetConstant<_TYPE_B>(const _TYPE_B&);
	PCGEX_FOREACH_SUPPORTEDTYPES_PAIRS(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// TDirectAttributeProxy - Direct attribute access (bypasses buffer)
	//
	template <typename T_REAL>
	class TDirectAttributeProxy : public IBufferProxy
	{
	public:
		const FPCGMetadataAttribute<T_REAL>* InAttribute = nullptr;
		FPCGMetadataAttribute<T_REAL>* OutAttribute = nullptr;

		explicit TDirectAttributeProxy(EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;
		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;
	};

#pragma region externalization TDirectAttributeProxy

#define PCGEX_TPL(_TYPE, _NAME, ...) extern template class TDirectAttributeProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// FPropertyBufferProxy - Non-template proxy for generic/unknown attribute types (Tier 3).
	//
	// Wraps an IBuffer (typically FPropertyArrayBuffer or FPropertySingleValueBuffer) and
	// delegates all access through the void* API (ReadVoid/SetVoid/GetVoid). No type
	// conversion is performed — RealType and WorkingType are both EPCGMetadataTypes::Unknown.
	//
	// This enables blending/data-forwarding code to carry generic attribute data even when
	// the underlying type isn't in PCGEX_FOREACH_SUPPORTEDTYPES. The blend system's
	// FCopyOnlyBlendOperation handles the actual memcpy-based accumulation.
	//
	class PCGEXCORE_API FPropertyBufferProxy : public IBufferProxy
	{
	public:
		TSharedPtr<IBuffer> Buffer;
		int32 ElementSize = 0;
		int32 ElementAlignment = 1;

		FPropertyBufferProxy(int32 InElementSize, int32 InElementAlignment);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;
		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const override;

		virtual TSharedPtr<IBuffer> GetBuffer() const override;
		virtual bool EnsureReadable() const override;

		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;

		virtual int32 GetValueSize() const override;
		virtual int32 GetValueAlignment() const override;
	};

	//
	// TDirectDataAttributeProxy - Direct data-domain attribute access
	//
	template <typename T_REAL>
	class TDirectDataAttributeProxy : public IBufferProxy
	{
	public:
		const FPCGMetadataAttribute<T_REAL>* InAttribute = nullptr;
		FPCGMetadataAttribute<T_REAL>* OutAttribute = nullptr;

		explicit TDirectDataAttributeProxy(EPCGMetadataTypes InWorkingType);

		virtual void GetVoid(const int32 Index, void* OutValue) const override;
		virtual void GetCurrentVoid(const int32 Index, void* OutValue) const override;
		virtual void SetVoid(const int32 Index, const void* Value) const override;
		virtual PCGExValueHash ReadValueHash(const int32 Index) const override;
	};

#pragma region externalization TDirectDataAttributeProxy

#define PCGEX_TPL(_TYPE, _NAME, ...) extern template class TDirectDataAttributeProxy<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion
}
