// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "UObject/SoftObjectPath.h"

struct FPCGExContext;
class FPCGMetadataAttributeBase;

namespace PCGExData
{
	class FFacade;
	class IBufferProxy;

	template <typename T_REAL>
	class TBuffer;

	struct FProxyDescriptor;

	//
	// Buffer acquisition helpers
	//

	template <typename T_REAL>
	TSharedPtr<TBuffer<T_REAL>> TryGetBuffer(
		FPCGExContext* InContext,
		const FProxyDescriptor& InDescriptor,
		const TSharedPtr<FFacade>& InDataFacade);

	template <typename T_REAL>
	void TryGetInOutAttr(
		const FProxyDescriptor& InDescriptor,
		const TSharedPtr<FFacade>& InDataFacade,
		const FPCGMetadataAttributeBase*& OutInAttribute,
		FPCGMetadataAttributeBase*& OutOutAttribute);

#pragma region externalization TryGetInOutAttr / TryGetBuffer

#define PCGEX_TPL(_TYPE, _NAME, ...) \
	extern template void TryGetInOutAttr<_TYPE>( \
		const FProxyDescriptor& InDescriptor, \
		const TSharedPtr<FFacade>& InDataFacade, \
		const FPCGMetadataAttributeBase*& OutInAttribute, \
		FPCGMetadataAttributeBase*& OutOutAttribute); \
	extern template TSharedPtr<TBuffer<_TYPE>> TryGetBuffer<_TYPE>( \
		FPCGExContext* InContext, \
		const FProxyDescriptor& InDescriptor, \
		const TSharedPtr<FFacade>& InDataFacade);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	// What the consumer does with the proxy it asks GetProxyBuffer for.
	enum class EProxyAccess : uint8
	{
		Typed,      // Get<T> / Set<T>: every value is WorkingType
		TypeErased, // void, scoped-value and hash accessors only: container and extended-type attributes are served too
	};

	//
	// Main proxy creation function - fully type-erased
	// Null, with an error, when InAccess is Typed and the attribute is a container or an extended type.
	//
	PCGEXCORE_API TSharedPtr<IBufferProxy> GetProxyBuffer(
		FPCGExContext* InContext,
		const FProxyDescriptor& InDescriptor,
		const EProxyAccess InAccess = EProxyAccess::Typed);

	//
	// Constant proxy creation
	//
	template <typename T>
	TSharedPtr<IBufferProxy> GetConstantProxyBuffer(const T& Constant, EPCGMetadataTypes InWorkingType);

#pragma region externalization GetConstantProxyBuffer

#define PCGEX_TPL(_TYPE, _NAME, ...) \
	extern template TSharedPtr<IBufferProxy> GetConstantProxyBuffer<_TYPE>(const _TYPE& Constant, EPCGMetadataTypes InWorkingType);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// Per-field proxy creation for multi-component types
	//
	PCGEXCORE_API bool GetPerFieldProxyBuffers(
		FPCGExContext* InContext,
		const FProxyDescriptor& InBaseDescriptor,
		const int32 NumDesiredFields,
		TArray<TSharedPtr<IBufferProxy>>& OutProxies);
}
