// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMetaHelpersMacros.h"

// FText lacks native GetTypeHash and operator== — required by engine's DefaultOperationTraits<FText>::Equal()
// and TSet<FText> instantiation. Must be defined BEFORE PCGMetadataAttributeTraits.h is included,
// so the operators are in scope when engine templates are instantiated with T=FText.
#ifndef PCGEX_FTEXT_SHIMS_DEFINED
#define PCGEX_FTEXT_SHIMS_DEFINED
FORCEINLINE uint32 GetTypeHash(const FText& Value) { return GetTypeHash(Value.ToString()); }
FORCEINLINE bool operator==(const FText& A, const FText& B) { return A.EqualTo(B); }
#endif

#include "Metadata/PCGMetadataAttributeTraits.h"

// Engine registers FText via PCGMetadataGenerateDataTypes but provides no MetadataTraits<FText> specialization.
// The default template inherits DefaultWeightedSumTraits which requires operator* — FText doesn't have it.
// CONFLICT GUARD: If Epic adds their own MetadataTraits<FText>, this will trigger a redefinition error.
// When that happens, remove this block and verify their specialization matches our needs.
// To check: search PCGMetadataAttributeTraits.h for "MetadataTraits<FText>" — if it exists, delete this.
namespace PCG
{
	namespace Private
	{
		template<>
		struct MetadataTraits<FText> : DefaultCompareTraits<FText>, DefaultStringTraits<FText>, CompareBasedFindNearestTraits<MetadataTraits<FText>, FText>, DefaultHashTraits<FText>
		{
			enum { CompressData = false };
			enum { CanMinMax = false };
			enum { CanSubAdd = false };
			enum { CanMulDiv = false };
			enum { CanInterpolate = false };
			enum { CanNormalize = false };
			enum { InterpolationNeedsNormalization = false };
			enum { CanSearchString = true };
			enum { NeedsConstruction = true };

			static FString ToString(const FText& A) { return A.ToString(); }
			static bool Equal(const FText& A, const FText& B) { return A.EqualTo(B); }
			static FText ZeroValue() { return FText::GetEmpty(); }

			static bool Substring(const FText& A, const FText& B, const ESearchCase::Type SearchCase = ESearchCase::IgnoreCase)
			{
				return A.ToString().Contains(B.ToString(), SearchCase);
			}

			static bool Matches(const FText& A, const FText& B, const ESearchCase::Type SearchCase = ESearchCase::IgnoreCase)
			{
				return A.ToString().MatchesWildcard(B.ToString(), SearchCase);
			}
		};
	}
}

#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadata.h"

class UPCGManagedComponent;
class UPCGData;
class UPCGComponent;

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

	PCGEXCORE_API bool IsPCGExAttribute(const FString& InStr);
	PCGEXCORE_API bool IsPCGExAttribute(const FName InName);
	PCGEXCORE_API bool IsPCGExAttribute(const FText& InText);

	PCGEXCORE_API FName MakePCGExAttributeName(const FString& Str0);
	PCGEXCORE_API FName MakePCGExAttributeName(const FString& Str0, const FString& Str1);

	PCGEXCORE_API bool IsWritableAttributeName(const FName Name);
	PCGEXCORE_API FString StringTagFromName(const FName Name);
	PCGEXCORE_API bool IsValidStringTag(const FString& Tag);

	PCGEXCORE_API bool TryGetAttributeName(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, FName& OutName);

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
		if (!InData) { return false; }
		return HasAttribute(InData->ConstMetadata(), Identifier);
	}

	template <typename T>
	static const FPCGMetadataAttribute<T>* TryGetConstAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata) { return nullptr; }
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain)) { return nullptr; }

		// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
		// ReSharper disable once CppRedundantTemplateKeyword
		return InMetadata->template GetConstTypedAttribute<T>(Identifier);
	}

	template <typename T>
	static const FPCGMetadataAttribute<T>* TryGetConstAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InData) { return nullptr; }
		return TryGetConstAttribute<T>(InData->ConstMetadata(), Identifier);
	}

	template <typename T>
	static FPCGMetadataAttribute<T>* TryGetMutableAttribute(UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata) { return nullptr; }
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain)) { return nullptr; }

		// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
		// ReSharper disable once CppRedundantTemplateKeyword
		return InMetadata->template GetMutableTypedAttribute<T>(Identifier);
	}

	template <typename T>
	static FPCGMetadataAttribute<T>* TryGetMutableAttribute(UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InData) { return nullptr; }
		return TryGetMutableAttribute<T>(InData->MutableMetadata(), Identifier);
	}

	constexpr static EPCGMetadataTypes GetPropertyType(const EPCGPointProperties Property)
	{
		switch (Property)
		{
		case EPCGPointProperties::Density:
		case EPCGPointProperties::Steepness: return EPCGMetadataTypes::Float;
		case EPCGPointProperties::BoundsMin:
		case EPCGPointProperties::BoundsMax:
		case EPCGPointProperties::Extents:
		case EPCGPointProperties::Position:
		case EPCGPointProperties::Scale:
		case EPCGPointProperties::LocalCenter:
		case EPCGPointProperties::LocalSize:
		case EPCGPointProperties::ScaledLocalSize: return EPCGMetadataTypes::Vector;
		case EPCGPointProperties::Color: return EPCGMetadataTypes::Vector4;
		case EPCGPointProperties::Rotation: return EPCGMetadataTypes::Quaternion;
		case EPCGPointProperties::Transform: return EPCGMetadataTypes::Transform;
		case EPCGPointProperties::Seed: return EPCGMetadataTypes::Integer32;
		default: return EPCGMetadataTypes::Unknown;
		}
	}

	constexpr static EPCGPointNativeProperties GetPropertyNativeTypes(const EPCGPointProperties Property)
	{
		switch (Property)
		{
		case EPCGPointProperties::Density: return EPCGPointNativeProperties::Density;
		case EPCGPointProperties::BoundsMin: return EPCGPointNativeProperties::BoundsMin;
		case EPCGPointProperties::BoundsMax: return EPCGPointNativeProperties::BoundsMax;
		case EPCGPointProperties::Color: return EPCGPointNativeProperties::Color;
		case EPCGPointProperties::Position: return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Rotation: return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Scale: return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Transform: return EPCGPointNativeProperties::Transform;
		case EPCGPointProperties::Steepness: return EPCGPointNativeProperties::Steepness;
		case EPCGPointProperties::Seed: return EPCGPointNativeProperties::Seed;
		case EPCGPointProperties::Extents:
		case EPCGPointProperties::LocalCenter:
		case EPCGPointProperties::LocalSize: return EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax;
		case EPCGPointProperties::ScaledLocalSize: return EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Transform;
		default: return EPCGPointNativeProperties::None;
		}
	}

	constexpr static EPCGMetadataTypes GetPropertyType(const EPCGExtraProperties Property)
	{
		switch (Property)
		{
		case EPCGExtraProperties::Index: return EPCGMetadataTypes::Integer32;
		default: return EPCGMetadataTypes::Unknown;
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

	PCGEXCORE_API FString GetSelectorDisplayName(const FPCGAttributePropertyInputSelector& InSelector);
}
