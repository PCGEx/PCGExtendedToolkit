// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Types/PCGExAttributeIdentity.h"

#include "PCGParamData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSpatialData.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"

namespace PCGExData
{
	FAttributeIdentity::FAttributeIdentity(const FPCGMetadataAttributeBase* InAttribute)
	{
		if (!InAttribute)
		{
			return;
		}

		// Copy the base desc from the attribute (name, value type, containers, value type object, key type/object).
		static_cast<FPCGMetadataAttributeDesc&>(*this) = InAttribute->GetAttributeDesc();
		MetadataDomain = InAttribute->GetMetadataDomain()->GetDomainID();
		Attribute = InAttribute;
	}

	FAttributeIdentity::FAttributeIdentity(const FPCGMetadataAttributeDesc& InDesc, const FPCGMetadataDomainID& InDomain)
		: FPCGMetadataAttributeDesc(InDesc)
		  , MetadataDomain(InDomain)
	{
	}

	FString FAttributeIdentity::GetDisplayName() const
	{
		return FString(Name.ToString() + FString::Printf(TEXT("( %d )"), static_cast<int32>(ValueType)));
	}

	void FAttributeIdentity::Get(const UPCGMetadata* InMetadata, TArray<FAttributeIdentity>& OutIdentities, const TSet<FName>* OptionalIgnoreList)
	{
		if (!InMetadata)
		{
			return;
		}

		TArray<FPCGAttributeIdentifier> Identifiers;
		TArray<EPCGMetadataTypes> Types;
		InMetadata->GetAllAttributes(Identifiers, Types);

		const int32 NumAttributes = Identifiers.Num();

		OutIdentities.Reserve(OutIdentities.Num() + NumAttributes);

		for (int i = 0; i < NumAttributes; i++)
		{
			if (OptionalIgnoreList && OptionalIgnoreList->Contains(Identifiers[i].Name))
			{
				continue;
			}
			OutIdentities.AddUnique(FAttributeIdentity(InMetadata->GetConstAttribute(Identifiers[i])));
		}
	}

	void FAttributeIdentity::Get(const UPCGMetadata* InMetadata, TArray<FPCGAttributeIdentifier>& OutIdentifiers, TMap<FPCGAttributeIdentifier, FAttributeIdentity>& OutIdentities, const TSet<FName>* OptionalIgnoreList)
	{
		if (!InMetadata)
		{
			return;
		}

		TArray<EPCGMetadataTypes> Types;
		InMetadata->GetAllAttributes(OutIdentifiers, Types);

		const int32 NumAttributes = OutIdentifiers.Num();
		OutIdentities.Reserve(OutIdentities.Num() + NumAttributes);

		for (int i = 0; i < NumAttributes; i++)
		{
			const FPCGAttributeIdentifier& Identifier = OutIdentifiers[i];
			if (OptionalIgnoreList && OptionalIgnoreList->Contains(Identifier.Name))
			{
				continue;
			}
			OutIdentities.Add(Identifier, FAttributeIdentity(InMetadata->GetConstAttribute(Identifier)));
		}
	}

	bool FAttributeIdentity::Get(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, FAttributeIdentity& OutIdentity)
	{
		FPCGAttributePropertyInputSelector FixedSelector = InSelector.CopyAndFixLast(InData);
		if (!FixedSelector.IsValid() || FixedSelector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			return false;
		}

		const FPCGMetadataAttributeBase* SourceAttribute = InData->Metadata->GetConstAttribute(PCGExMetaHelpers::GetAttributeIdentifier(FixedSelector, InData));
		if (!SourceAttribute)
		{
			return false;
		}

		OutIdentity = FAttributeIdentity(SourceAttribute);
		return true;
	}

	int32 FAttributeIdentity::ForEach(const UPCGMetadata* InMetadata, FForEachFunc&& Func)
	{
		// BUG : This does not account for metadata domains

		if (!InMetadata)
		{
			return 0;
		}

		TArray<FPCGAttributeIdentifier> Identifiers;
		TArray<EPCGMetadataTypes> Types;

		InMetadata->GetAllAttributes(Identifiers, Types);
		const int32 NumAttributes = Identifiers.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity Identity = FAttributeIdentity(InMetadata->GetConstAttribute(Identifiers[i]));
			Func(Identity, i);
		}

		return NumAttributes;
	}

	bool FAttributesInfos::Contains(const FName AttributeName, const EPCGMetadataTypes Type)
	{
		for (FAttributeIdentity& Identity : Identities)
		{
			if (Identity.Name == AttributeName && Identity.ValueType == Type)
			{
				return true;
			}
		}
		return false;
	}

	bool FAttributesInfos::Contains(const FName AttributeName)
	{
		for (FAttributeIdentity& Identity : Identities)
		{
			if (Identity.Name == AttributeName)
			{
				return true;
			}
		}
		return false;
	}

	FAttributeIdentity* FAttributesInfos::Find(const FName AttributeName)
	{
		for (FAttributeIdentity& Identity : Identities)
		{
			if (Identity.Name == AttributeName)
			{
				return &Identity;
			}
		}
		return nullptr;
	}

	bool FAttributesInfos::FindMissing(const TSet<FName>& Checklist, TSet<FName>& OutMissing)
	{
		bool bAnyMissing = false;
		for (const FName& Id : Checklist)
		{
			if (!Contains(Id) || !PCGExMetaHelpers::IsWritableAttributeName(Id))
			{
				OutMissing.Add(Id);
				bAnyMissing = true;
			}
		}
		return bAnyMissing;
	}

	bool FAttributesInfos::FindMissing(const TArray<FName>& Checklist, TSet<FName>& OutMissing)
	{
		bool bAnyMissing = false;
		for (const FName& Id : Checklist)
		{
			if (!Contains(Id) || !PCGExMetaHelpers::IsWritableAttributeName(Id))
			{
				OutMissing.Add(Id);
				bAnyMissing = true;
			}
		}
		return bAnyMissing;
	}

	void FAttributesInfos::Append(const TSharedPtr<FAttributesInfos>& Other, const FPCGExAttributeGatherDetails& InGatherDetails, TSet<FName>& OutTypeMismatch)
	{
		for (int i = 0; i < Other->Identities.Num(); i++)
		{
			const FAttributeIdentity& OtherId = Other->Identities[i];

			if (!InGatherDetails.Test(OtherId.Name.ToString()))
			{
				continue;
			}

			const FPCGAttributeIdentifier OtherIdentifier = OtherId.GetIdentifier();
			if (const int32* Index = Map.Find(OtherIdentifier))
			{
				const FAttributeIdentity& Id = Identities[*Index];
				// Desc-aware mismatch: catches Struct<A> vs Struct<B>, TArray<int> vs int, etc.
				if (!Id.IsSameType(OtherId))
				{
					OutTypeMismatch.Add(Id.Name);
					// TODO : Update existing based on settings
				}

				continue;
			}

			int32 AppendIndex = Identities.Add(OtherId);
			Map.Add(OtherIdentifier, AppendIndex);
		}
	}

	void FAttributesInfos::Append(const TSharedPtr<FAttributesInfos>& Other, TSet<FName>& OutTypeMismatch, const TSet<FName>* InIgnoredAttributes)
	{
		for (int i = 0; i < Other->Identities.Num(); i++)
		{
			const FAttributeIdentity& OtherId = Other->Identities[i];

			if (InIgnoredAttributes && InIgnoredAttributes->Contains(OtherId.Name))
			{
				continue;
			}

			const FPCGAttributeIdentifier OtherIdentifier = OtherId.GetIdentifier();
			if (const int32* Index = Map.Find(OtherIdentifier))
			{
				const FAttributeIdentity& Id = Identities[*Index];
				if (!Id.IsSameType(OtherId))
				{
					OutTypeMismatch.Add(Id.Name);
					// TODO : Update existing based on settings
				}

				continue;
			}

			int32 AppendIndex = Identities.Add(OtherId);
			Map.Add(OtherIdentifier, AppendIndex);
		}
	}

	void FAttributesInfos::Update(const FAttributesInfos* Other, const FPCGExAttributeGatherDetails& InGatherDetails, TSet<FName>& OutTypeMismatch)
	{
		// TODO : Update types and attributes according to input settings?
	}

	void FAttributesInfos::Filter(const FilterCallback& FilterFn)
	{
		TArray<FName> FilteredOutNames;
		FilteredOutNames.Reserve(Identities.Num());

		for (const TPair<FPCGAttributeIdentifier, int32>& Pair : Map)
		{
			if (FilterFn(Pair.Key.Name))
			{
				continue;
			}
			FilteredOutNames.Add(Pair.Key.Name);
		}

		// Filter out identities
		for (FName FilteredOutName : FilteredOutNames)
		{
			Map.Remove(FilteredOutName);
			for (int i = 0; i < Identities.Num(); i++)
			{
				if (Identities[i].Name == FilteredOutName)
				{
					Identities.RemoveAt(i);
					break;
				}
			}
		}

		// Refresh indices
		for (int i = 0; i < Identities.Num(); i++)
		{
			Map.Add(Identities[i].GetIdentifier(), i);
		}
	}

	TSharedPtr<FAttributesInfos> FAttributesInfos::Get(const UPCGMetadata* InMetadata, const TSet<FName>* IgnoredAttributes)
	{
		PCGEX_MAKE_SHARED(NewInfos, FAttributesInfos)
		FAttributeIdentity::Get(InMetadata, NewInfos->Identities, IgnoredAttributes);

		for (int i = 0; i < NewInfos->Identities.Num(); i++)
		{
			NewInfos->Map.Add(NewInfos->Identities[i].GetIdentifier(), i);
		}

		return NewInfos;
	}

	TSharedPtr<FAttributesInfos> FAttributesInfos::Get(const TSharedPtr<FPointIOCollection>& InCollection, TSet<FName>& OutTypeMismatch, const TSet<FName>* IgnoredAttributes)
	{
		PCGEX_MAKE_SHARED(NewInfos, FAttributesInfos)
		for (const TSharedPtr<FPointIO>& IO : InCollection->Pairs)
		{
			TSharedPtr<FAttributesInfos> Infos = Get(IO->GetIn()->Metadata, IgnoredAttributes);
			NewInfos->Append(Infos, OutTypeMismatch, IgnoredAttributes);
		}

		return NewInfos;
	}

	void GatherAttributes(const TSharedPtr<FAttributesInfos>& OutInfos, const FPCGContext* InContext, const FName InputLabel, const FPCGExAttributeGatherDetails& InDetails, TSet<FName>& Mismatches)
	{
		TArray<FPCGTaggedData> InputData = InContext->InputData.GetInputsByPin(InputLabel);
		for (const FPCGTaggedData& TaggedData : InputData)
		{
			if (const UPCGParamData* AsParamData = Cast<UPCGParamData>(TaggedData.Data))
			{
				OutInfos->Append(FAttributesInfos::Get(AsParamData->Metadata), InDetails, Mismatches);
			}
			else if (const UPCGSpatialData* AsSpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
			{
				OutInfos->Append(FAttributesInfos::Get(AsSpatialData->Metadata), InDetails, Mismatches);
			}
		}
	}

	TSharedPtr<FAttributesInfos> GatherAttributes(const FPCGContext* InContext, const FName InputLabel, const FPCGExAttributeGatherDetails& InDetails, TSet<FName>& Mismatches)
	{
		PCGEX_MAKE_SHARED(OutInfos, FAttributesInfos)
		GatherAttributes(OutInfos, InContext, InputLabel, InDetails, Mismatches);
		return OutInfos;
	}

	TSharedPtr<FAttributesInfos> GatherAttributeInfos(const FPCGContext* InContext, const FName InPinLabel, const FPCGExAttributeGatherDetails& InGatherDetails, const bool bThrowError)
	{
		TSharedPtr<FAttributesInfos> OutInfos = MakeShared<FAttributesInfos>();
		TArray<FPCGTaggedData> TaggedDatas = InContext->InputData.GetInputsByPin(InPinLabel);

		bool bHasErrors = false;

		for (FPCGTaggedData& TaggedData : TaggedDatas)
		{
			const UPCGMetadata* Metadata = nullptr;

			if (const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data))
			{
				Metadata = ParamData->Metadata;
			}
			else if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
			{
				Metadata = SpatialData->Metadata;
			}

			if (!Metadata)
			{
				continue;
			}

			TSet<FName> Mismatch;
			TSharedPtr<FAttributesInfos> Infos = FAttributesInfos::Get(Metadata);

			OutInfos->Append(Infos, InGatherDetails, Mismatch);

			if (bThrowError && !Mismatch.IsEmpty())
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Some inputs share the same name but not the same type."));
				bHasErrors = true;
				break;
			}
		}

		if (bHasErrors)
		{
			OutInfos.Reset();
		}
		return OutInfos;
	}
}
