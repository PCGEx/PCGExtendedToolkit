// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGMetadataCommon.h"

struct FPCGContext;

namespace PCGExData
{
	class FPointIOCollection;
}

struct FPCGExAttributeGatherDetails;
class UPCGMetadata;


namespace PCGExData
{
	// Inherits the full 5.8 attribute type descriptor (Name, ValueType, ContainerTypes, ValueTypeObject, KeyType, KeyTypeObject)
	// and extends it with a metadata domain and an optional pointer back to the source attribute.
	// This is PCGEx's runtime type carrier; it is not a USTRUCT and is not serialized -- it lives on the stack
	// or inside TArray members of non-UObject state. TObjectPtr fields on the base are safe because
	// UStruct/UEnum/UClass are module-lifetime and never GC'd.
	struct PCGEXCORE_API FAttributeIdentity : FPCGMetadataAttributeDesc
	{
		// Inherited from FPCGMetadataAttributeDesc:
		//   FName Name
		//   EPCGMetadataTypes ValueType
		//   TArray<EPCGMetadataAttributeContainerTypes> ContainerTypes
		//   TObjectPtr<const UObject> ValueTypeObject
		//   EPCGMetadataTypes KeyType
		//   TObjectPtr<const UObject> KeyTypeObject
		//   + IsSameType / IsValid / IsSingleValue / IsArray / IsSet / IsMap / GetTypeString

		FPCGMetadataDomainID MetadataDomain = PCGMetadataDomainID::Default;

		// Non-owning pointer back to the source attribute. Lifetime follows the source UPCGMetadata.
		// nullptr for synthesized identities (e.g. placeholders before an attribute exists).
		const FPCGMetadataAttributeBase* Attribute = nullptr;

		// True for synthesized identities fed purely from tag values, with no backing metadata attribute
		// (Attribute == nullptr). Set by FPCGExPointIOMerger's tags-to-attributes path; ignored elsewhere.
		bool bTagOnly = false;

		FAttributeIdentity() = default;

		// Canonical construction: from a live attribute. Copies the attribute's desc, domain and caches the pointer.
		explicit FAttributeIdentity(const FPCGMetadataAttributeBase* InAttribute);

		// Lower-level: from a desc + domain (no attribute). For the rare synthesized-identity path.
		FAttributeIdentity(const FPCGMetadataAttributeDesc& InDesc, const FPCGMetadataDomainID& InDomain);

		// --- Identity accessors ---
		FPCGAttributeIdentifier GetIdentifier() const
		{
			return FPCGAttributeIdentifier(Name, MetadataDomain);
		}

		bool InDataDomain() const
		{
			return MetadataDomain.Flag == EPCGMetadataDomainFlag::Data;
		}

		// --- Type accessors ---
		EPCGMetadataTypes GetType() const
		{
			return ValueType;
		}

		int16 GetTypeId() const
		{
			return static_cast<int16>(ValueType);
		}

		bool IsA(const int16 InType) const
		{
			return GetTypeId() == InType;
		}

		bool IsA(const EPCGMetadataTypes InType) const
		{
			return ValueType == InType;
		}

		// Templated IsA<T>: defers to the attribute (correct for Struct/Enum/Object with ValueTypeObject),
		// falls back to static enum mapping for synthesized identities.
		template <typename T>
		bool IsA() const
		{
			if (Attribute)
			{
				return Attribute->IsOfType<T>();
			}
			return static_cast<uint16>(ValueType) == PCG::Private::MetadataTypes<T>::Id;
		}

		// Interpolation preference is a property of the source attribute. Default to true when synthesized.
		bool GetAllowsInterpolation() const
		{
			return Attribute ? Attribute->AllowsInterpolation() : true;
		}

		FString GetDisplayName() const;

		// PCGEx semantics: identity = same name + same domain (not same type).
		// Explicitly hides FPCGMetadataAttributeDesc::operator== which would also compare types.
		bool operator==(const FAttributeIdentity& Other) const
		{
			return Name == Other.Name && MetadataDomain == Other.MetadataDomain;
		}

		static void Get(const UPCGMetadata* InMetadata, TArray<FAttributeIdentity>& OutIdentities, const TSet<FName>* OptionalIgnoreList = nullptr);
		static void Get(const UPCGMetadata* InMetadata, TArray<FPCGAttributeIdentifier>& OutIdentifiers, TMap<FPCGAttributeIdentifier, FAttributeIdentity>& OutIdentities, const TSet<FName>* OptionalIgnoreList = nullptr);
		static bool Get(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, FAttributeIdentity& OutIdentity);

		using FForEachFunc = std::function<void (const FAttributeIdentity&, const int32)>;
		static int32 ForEach(const UPCGMetadata* InMetadata, FForEachFunc&& Func);
	};

	class PCGEXCORE_API FAttributesInfos : public TSharedFromThis<FAttributesInfos>
	{
	public:
		TMap<FPCGAttributeIdentifier, int32> Map;
		TArray<FAttributeIdentity> Identities; // Each identity carries its own Attribute pointer (was a parallel array pre-5.8 cleanup).

		bool Contains(FName AttributeName);
		FAttributeIdentity* Find(FName AttributeName);

		bool FindMissing(const TArray<FName>& Checklist, TSet<FName>& OutMissing);

		void Append(const TSharedPtr<FAttributesInfos>& Other, const FPCGExAttributeGatherDetails& InGatherDetails, TSet<FName>& OutTypeMismatch);
		void Append(const TSharedPtr<FAttributesInfos>& Other, TSet<FName>& OutTypeMismatch, const TSet<FName>* InIgnoredAttributes = nullptr);

		using FilterCallback = std::function<bool(const FName&)>;

		void Filter(const FilterCallback& FilterFn);

		~FAttributesInfos() = default;

		static TSharedPtr<FAttributesInfos> Get(const UPCGMetadata* InMetadata, const TSet<FName>* IgnoredAttributes = nullptr);
		static TSharedPtr<FAttributesInfos> Get(const TSharedPtr<FPointIOCollection>& InCollection, TSet<FName>& OutTypeMismatch, const TSet<FName>* IgnoredAttributes = nullptr);
	};

	PCGEXCORE_API
	void GatherAttributes(const TSharedPtr<FAttributesInfos>& OutInfos, const FPCGContext* InContext, const FName InputLabel, const FPCGExAttributeGatherDetails& InDetails, TSet<FName>& Mismatches);

	PCGEXCORE_API
	TSharedPtr<FAttributesInfos> GatherAttributes(const FPCGContext* InContext, const FName InputLabel, const FPCGExAttributeGatherDetails& InDetails, TSet<FName>& Mismatches);

	PCGEXCORE_API
	TSharedPtr<FAttributesInfos> GatherAttributeInfos(const FPCGContext* InContext, const FName InPinLabel, const FPCGExAttributeGatherDetails& InGatherDetails, const bool bThrowError);
}
