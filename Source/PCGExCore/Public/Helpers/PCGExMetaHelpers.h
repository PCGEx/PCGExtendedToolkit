// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMetaHelpersMacros.h"

#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Types/PCGExAttributeIdentity.h"

#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

template <typename ValueType, typename ViewType>
class TPCGValueRange;

class UPCGMetadata;
class UPCGManagedComponent;
class UPCGData;
class UPCGComponent;
class IPCGAttributeAccessorKeys;

#define PCGEX_VALIDATE_NAME(_NAME) if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){	PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid user-defined attribute name for " #_NAME)); return false;	}
#define PCGEX_VALIDATE_NAME_CONDITIONAL(_IF, _NAME) if(_IF){ PCGEX_VALIDATE_NAME(_NAME) }
#define PCGEX_VALIDATE_NAME_CONSUMABLE(_NAME) if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){	PCGE_LOG(Error, GraphAndLog, FTEXT("Invalid user-defined attribute name for " #_NAME)); return false;	} Context->AddConsumableAttributeName(_NAME);
#define PCGEX_VALIDATE_NAME_C(_CTX, _NAME) if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){	PCGE_LOG_C(Error, GraphAndLog, _CTX, FTEXT("Invalid user-defined attribute name for " #_NAME)); return false;	}
#define PCGEX_VALIDATE_NAME_C_RET(_CTX, _NAME, _RETURN) if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){	PCGE_LOG_C(Error, GraphAndLog, _CTX, FTEXT("Invalid user-defined attribute name for " #_NAME)); return _RETURN;	}
#define PCGEX_VALIDATE_NAME_CONSUMABLE_C(_CTX, _NAME) if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){	PCGE_LOG_C(Error, GraphAndLog, _CTX, FTEXT("Invalid user-defined attribute name for " #_NAME)); return false;	} _CTX->AddConsumableAttributeName(_NAME);
#define PCGEX_SOFT_VALIDATE_NAME(_BOOL, _NAME, _CTX) if(_BOOL){if (!PCGExMetaHelpers::IsWritableAttributeName(_NAME)){ PCGE_LOG_C(Warning, GraphAndLog, _CTX, FTEXT("Invalid user-defined attribute name for " #_NAME)); _BOOL = false; } }

namespace PCGExMetaHelpers
{
	const FName InvalidName = "INVALID_DATA";

	PCGEXCORE_API TSharedPtr<IPCGAttributeAccessorKeys> MakeMutableKeys(UPCGData* InData);
	PCGEXCORE_API TSharedPtr<IPCGAttributeAccessorKeys> MakeConstKeys(const UPCGData* InData);
	
	PCGEXCORE_API void InitializeMetadataEntries(UPCGMetadata* Metadata, const TPCGValueRange<int64>& MetadataEntries, const bool bConservative);
	
	PCGEXCORE_API bool IsPCGExAttribute(const FString& InStr);
	PCGEXCORE_API bool IsPCGExAttribute(const FName InName);
	PCGEXCORE_API bool IsPCGExAttribute(const FText& InText);

	PCGEXCORE_API FName MakePCGExAttributeName(const FString& Str0);
	PCGEXCORE_API FName MakePCGExAttributeName(const FString& Str0, const FString& Str1);

	PCGEXCORE_API bool IsWritableAttributeName(const FName Name);

	/**
	 * Sanitize an arbitrary name into a valid PCG attribute name.
	 * Characters outside [A-Za-z0-9 _-/] are replaced with '_'.
	 * Runs of '_' are collapsed, and leading/trailing '_' are trimmed.
	 * Returns NAME_None if the result is empty.
	 */
	PCGEXCORE_API FName SanitizeAttributeName(const FName InName);

	PCGEXCORE_API FString StringTagFromName(const FName Name);
	PCGEXCORE_API bool IsValidStringTag(const FString& Tag);

	PCGEXCORE_API bool TryGetAttributeName(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, FName& OutName);

	/** Number of addressable items in a data object: point count for point data, metadata entry count otherwise. Returns 0 for null data or data without metadata. */
	PCGEXCORE_API int32 GetElementsCount(const UPCGData* InData);

	PCGEXCORE_API bool IsDataDomainAttribute(const FName& InName);
	PCGEXCORE_API bool IsDataDomainAttribute(const FString& InName);
	PCGEXCORE_API bool IsDataDomainAttribute(const FPCGAttributePropertyInputSelector& InputSelector);

	PCGEXCORE_API void AppendUniqueSelectorsFromCommaSeparatedList(const FString& InCommaSeparatedString, TArray<FPCGAttributePropertyInputSelector>& OutSelectors);

	PCGEXCORE_API FName GetLongNameFromSelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const bool bInitialized = true);

	PCGEXCORE_API FPCGAttributeIdentifier GetAttributeIdentifier(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const bool bInitialized = true);
	PCGEXCORE_API FPCGAttributeIdentifier GetAttributeIdentifier(const FName InName, const UPCGData* InData);
	PCGEXCORE_API FPCGAttributeIdentifier GetAttributeIdentifier(const FName InName);

	PCGEXCORE_API FPCGAttributePropertyInputSelector GetSelectorFromIdentifier(const FPCGAttributeIdentifier& InIdentifier);

	/** Strip @Data. or @Elements. prefix from attribute name if present */
	PCGEXCORE_API FName StripDomainFromName(const FName& InName);

	/** Create a Data-domain attribute identifier from a base name (sanitizes any existing domain prefix) */
	PCGEXCORE_API FPCGAttributeIdentifier MakeDataIdentifier(const FName& BaseName);

	/** Create an Elements-domain attribute identifier from a base name (sanitizes any existing domain prefix) */
	PCGEXCORE_API FPCGAttributeIdentifier MakeElementIdentifier(const FName& BaseName);

	PCGEXCORE_API bool HasAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier);

	static bool HasAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InData)
		{
			return false;
		}
		return HasAttribute(InData->ConstMetadata(), Identifier);
	}

	/**
	 * Domain-safe, type-erased const attribute lookup. UPCGMetadata::GetConstAttribute routes through
	 * WithConstMetadataDomain, which error-logs "Failed to find domain" for domains the data type
	 * supports but never instantiated (domains are created lazily on first mutable access -- e.g.
	 * @Data on point data that never carried @Data attributes). This helper returns null silently
	 * in that case. Use it anywhere the identifier may target a domain the data might not have.
	 */
	PCGEXCORE_API const FPCGMetadataAttributeBase* TryGetConstAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier);

	static const FPCGMetadataAttributeBase* TryGetConstAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		return InData ? TryGetConstAttribute(InData->ConstMetadata(), Identifier) : nullptr;
	}

	template <typename T>
	static const FPCGMetadataAttributeBase* TryGetConstAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata)
		{
			return nullptr;
		}
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain))
		{
			return nullptr;
		}

		// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
		// ReSharper disable once CppRedundantTemplateKeyword
		return InMetadata->template GetConstTypedAttribute<T>(Identifier);
	}

	template <typename T>
	static const FPCGMetadataAttributeBase* TryGetConstAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InData)
		{
			return nullptr;
		}
		return TryGetConstAttribute<T>(InData->ConstMetadata(), Identifier);
	}

	template <typename T>
	static FPCGMetadataAttributeBase* TryGetMutableAttribute(UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata)
		{
			return nullptr;
		}
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain))
		{
			return nullptr;
		}

		// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
		// ReSharper disable once CppRedundantTemplateKeyword
		return InMetadata->template GetMutableTypedAttribute<T>(Identifier);
	}

	template <typename T>
	static FPCGMetadataAttributeBase* TryGetMutableAttribute(UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InData)
		{
			return nullptr;
		}
		return TryGetMutableAttribute<T>(InData->MutableMetadata(), Identifier);
	}

	/**
	 * Returns the attribute's value type. Reads `GetTypeId()` first (set for legacy
	 * templated attributes Float..SoftClassPath); falls back to `Desc.ValueType` for
	 * desc-created attributes (Struct, Object, Class, Soft*, Byte, Text, Enum, containers)
	 * where GetTypeId() is Unknown. For containers the returned type is the *element*
	 * type -- pair with `GetAttributeDesc().IsSingleValue()` to distinguish.
	 * Returns Unknown if InAttribute is null.
	 */
	PCGEXCORE_API EPCGMetadataTypes GetAttributeType(const FPCGMetadataAttributeBase* InAttribute);

	/**
	 * True for legacy templated scalar types (Float..SoftClassPath) -- i.e. those with a
	 * `FPCGMetadataAttribute<T>` specialization. False for Unknown and extended types
	 * (Byte/Text/Enum/Struct/Object/Soft/Class/SoftClass) `static_cast<FPCGMetadataAttribute<T>*>` on those is UB.
	 * Does NOT distinguish containers -- TArray<float> reports ValueType = Float
	 * static_cast<legacy>(but) is property-backed.Pair with `Desc.IsSingleValue()`	for the full check.
	*/
	constexpr static bool IsLegacyScalarType(const EPCGMetadataTypes Type)
	{
		return static_cast<uint8>(Type) < static_cast<uint8>(EPCGMetadataTypes::EndLegacyTypes);
	}

	constexpr static EPCGMetadataTypes GetPropertyType(const EPCGPointProperties Property)
	{
		switch (Property)
		{
		case EPCGPointProperties::Density:
		case EPCGPointProperties::Steepness:
			return EPCGMetadataTypes::Float;
		case EPCGPointProperties::BoundsMin:
		case EPCGPointProperties::BoundsMax:
		case EPCGPointProperties::Extents:
		case EPCGPointProperties::Position:
		case EPCGPointProperties::Scale:
		case EPCGPointProperties::LocalCenter:
		case EPCGPointProperties::LocalSize:
		case EPCGPointProperties::ScaledLocalSize:
			return EPCGMetadataTypes::Vector;
		case EPCGPointProperties::Color:
			return EPCGMetadataTypes::Vector4;
		case EPCGPointProperties::Rotation:
			return EPCGMetadataTypes::Quaternion;
		case EPCGPointProperties::Transform:
			return EPCGMetadataTypes::Transform;
		case EPCGPointProperties::Seed:
			return EPCGMetadataTypes::Integer32;
		default:
			return EPCGMetadataTypes::Unknown;
		}
	}

	constexpr static EPCGPointNativeProperties GetPropertyNativeTypes(const EPCGPointProperties Property)
	{
		switch (Property)
		{
		case EPCGPointProperties::Density:
			return EPCGPointNativeProperties::Density;
		case EPCGPointProperties::BoundsMin:
			return EPCGPointNativeProperties::BoundsMin;
		case EPCGPointProperties::BoundsMax:
			return EPCGPointNativeProperties::BoundsMax;
		case EPCGPointProperties::Color:
			return EPCGPointNativeProperties::Color;
		case EPCGPointProperties::Position:
			return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Rotation:
			return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Scale:
			return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Transform:
			return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Steepness:
			return EPCGPointNativeProperties::Steepness;
		case EPCGPointProperties::Seed:
			return EPCGPointNativeProperties::Seed;
		case EPCGPointProperties::Extents:
		case EPCGPointProperties::LocalCenter:
		case EPCGPointProperties::LocalSize:
			return EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax;
		case EPCGPointProperties::ScaledLocalSize:
			return EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Transform;
		default:
			return EPCGPointNativeProperties::None;
		}
	}

	constexpr static EPCGMetadataTypes GetPropertyType(const EPCGExtraProperties Property)
	{
		switch (Property)
		{
		case EPCGExtraProperties::Index:
			return EPCGMetadataTypes::Integer32;
		default:
			return EPCGMetadataTypes::Unknown;
		}
	}

	constexpr bool DummyBoolean = bool{};
	constexpr int32 DummyInteger32 = int32{};
	constexpr int64 DummyInteger64 = int64{};
	constexpr float DummyFloat = float{};
	constexpr double DummyDouble = double{};
	const FVector2D DummyVector2 = FVector2D::ZeroVector;
	const FVector DummyVector = FVector::ZeroVector;
	const FVector4 DummyVector4 = FVector4::Zero();
	const FQuat DummyQuaternion = FQuat::Identity;
	const FRotator DummyRotator = FRotator::ZeroRotator;
	const FTransform DummyTransform = FTransform::Identity;
	const FString DummyString = TEXT("");
	const FName DummyName = NAME_None;
	const FSoftClassPath DummySoftClassPath = FSoftClassPath{};
	const FSoftObjectPath DummySoftObjectPath = FSoftObjectPath{};
	constexpr uint8 DummyByte = uint8{};
	const FText DummyText = FText::GetEmpty();

	template <typename Func>
	static void ExecuteWithRightType(const EPCGMetadataTypes Type, Func&& Callback)
	{
#define PCGEX_EXECUTE_WITH_TYPE(_TYPE, _ID, ...) case EPCGMetadataTypes::_ID : Callback(Dummy##_ID); break;

		switch (Type)
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_EXECUTE_WITH_TYPE)
		default: ;
		}

#undef PCGEX_EXECUTE_WITH_TYPE
	}

	template <typename Func>
	static void ExecuteWithRightType(const int16 Type, Func&& Callback)
	{
#define PCGEX_EXECUTE_WITH_TYPE(_TYPE, _ID, ...) case EPCGMetadataTypes::_ID : Callback(Dummy##_ID); break;

		switch (static_cast<EPCGMetadataTypes>(Type))
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_EXECUTE_WITH_TYPE)
		default: ;
		}

#undef PCGEX_EXECUTE_WITH_TYPE
	}

	// True iff the desc describes a single-valued attribute of a basic legacy type
	// covered by PCGEX_FOREACH_SUPPORTEDTYPES (Float..SoftClassPath). Container types
	// (TArray/TSet/TMap) and extended 5.8 types (Struct/Enum/Object/SoftObject/Class/SoftClass/Byte/Text)
	// return false -- these need to route through FPropertyBuffer / FPropertyCopyBlendOperation.
	FORCEINLINE bool IsBasicSingleValue(const FPCGMetadataAttributeDesc& Desc)
	{
		return Desc.ContainerTypes.IsEmpty()
			&& static_cast<uint16>(Desc.ValueType) < static_cast<uint16>(EPCGMetadataTypes::EndLegacyTypes);
	}

	// Standard "skip with warning" log for Bucket-A sites where the operation is inherently
	// arithmetic / typed-conversion-only and has no defined semantics for container or extended types.
	// _IDENTITY can be any expression with a `.Name` member (FAttributeIdentity, FPCGMetadataAttributeDesc).
	// _OPERATION is a FText literal naming the operation, e.g. FTEXT("Attribute Stats").
#define PCGEX_LOG_UNSUPPORTED_TYPE(_CONTEXT, _IDENTITY, _OPERATION) \
		PCGE_LOG_C(Warning, GraphAndLog, _CONTEXT, FText::Format( \
			FTEXT("Attribute '{0}' is a container or extended type and cannot be used by {1} -- skipped."), \
			FText::FromName((_IDENTITY).Name), _OPERATION))

	// Identity-aware dispatch with explicit fallback for extended/container types.
	// Returns true if the typed branch ran, false if the fallback ran.
	// Use this everywhere consumers used to call ExecuteWithRightType(Identity.GetType(), ...) --
	// the fallback branch is where you wire up PropertyBuffer-based copy semantics (or a "drop+log"
	// for arithmetic-only sites).
	template <typename TypedFn, typename FallbackFn>
	static bool ExecuteWithRightType(const PCGExData::FAttributeIdentity& Identity, TypedFn&& Typed, FallbackFn&& Fallback)
	{
		if (IsBasicSingleValue(Identity))
		{
			ExecuteWithRightType(Identity.GetType(), std::forward<TypedFn>(Typed));
			return true;
		}
		Fallback();
		return false;
	}

	// Identity-aware dispatch, single callback. Returns true if the typed branch ran. No fallback
	// runs for extended/container types -- useful at sites where the fallback decision was already
	// taken upstream (e.g., Reverse swap nulls the writer at fetch time and skips matching pairs here).
	template <typename TypedFn>
	static bool ExecuteWithRightType(const PCGExData::FAttributeIdentity& Identity, TypedFn&& Typed)
	{
		if (!IsBasicSingleValue(Identity))
		{
			return false;
		}
		ExecuteWithRightType(Identity.GetType(), std::forward<TypedFn>(Typed));
		return true;
	}

	// Attribute-aware dispatch with fallback. Same shape as the Identity overload, but driven by
	// the live attribute's desc. Use at sites where the attribute pointer is what's in scope.
	template <typename TypedFn, typename FallbackFn>
	static bool ExecuteWithRightType(const FPCGMetadataAttributeBase* Attr, TypedFn&& Typed, FallbackFn&& Fallback)
	{
		if (!Attr)
		{
			Fallback();
			return false;
		}
		if (IsBasicSingleValue(Attr->GetAttributeDesc()))
		{
			ExecuteWithRightType(static_cast<EPCGMetadataTypes>(Attr->GetTypeId()), std::forward<TypedFn>(Typed));
			return true;
		}
		Fallback();
		return false;
	}

	// Attribute-aware dispatch, single callback. Returns true if the typed branch ran.
	// Use when the fallback is "do nothing / caller handles it via the bool return".
	template <typename TypedFn>
	static bool ExecuteWithRightType(const FPCGMetadataAttributeBase* Attr, TypedFn&& Typed)
	{
		if (!Attr || !IsBasicSingleValue(Attr->GetAttributeDesc()))
		{
			return false;
		}
		ExecuteWithRightType(static_cast<EPCGMetadataTypes>(Attr->GetTypeId()), std::forward<TypedFn>(Typed));
		return true;
	}

	PCGEXCORE_API FString GetSelectorDisplayName(const FPCGAttributePropertyInputSelector& InSelector);
}
