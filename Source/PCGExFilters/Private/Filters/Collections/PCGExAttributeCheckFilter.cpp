// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExAttributeCheckFilter.h"


#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExAttributeCheckFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FAttributeCheckFilter>(this);
}

bool PCGExPointFilter::FAttributeCheckFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(IO->GetIn()->Metadata);

	bool bResult = false;

	const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(FName(TypedFilterFactory->Config.AttributeName), IO->GetIn());
	const FString IdentifierStr = Identifier.Name.ToString();

	if (TypedFilterFactory->Config.bDoCheckType)
	{
		for (const PCGExData::FAttributeIdentity& Identity : Infos->Identities)
		{
			const FString Str = Identity.Identifier.Name.ToString();
			bool bMatches = false;

			if (TypedFilterFactory->Config.Domain != EPCGExAttribtueDomainCheck::Any)
			{
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Data && !Identity.InDataDomain())
				{
					continue;
				}
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Elements && Identity.InDataDomain())
				{
					continue;
				}
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Match && Identity.Identifier.MetadataDomain != Identifier.MetadataDomain)
				{
					continue;
				}
			}

			if (PCGExCompare::Compare(TypedFilterFactory->Config.Match, Str, IdentifierStr))
			{
				bMatches = true;
			}

			if (bMatches && Identity.UnderlyingType == TypedFilterFactory->Config.Type)
			{
				bResult = true;
				break;
			}
		}
	}
	else
	{
		// NOTE: When not checking type, the Data domain filter below uses the opposite
		// condition compared to the bDoCheckType branch (line 36 uses !InDataDomain()).
		// This means the "no type check" branch skips Data-domain attributes when Domain==Data.
		for (const PCGExData::FAttributeIdentity& Identity : Infos->Identities)
		{
			const FString Str = Identity.Identifier.Name.ToString();
			bool bMatches = false;

			if (TypedFilterFactory->Config.Domain != EPCGExAttribtueDomainCheck::Any)
			{
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Data && Identity.InDataDomain())
				{
					continue;
				}
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Elements && Identity.InDataDomain())
				{
					continue;
				}
				if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Match && Identity.Identifier.MetadataDomain != Identifier.MetadataDomain)
				{
					continue;
				}
			}

			if (PCGExCompare::Compare(TypedFilterFactory->Config.Match, Str, IdentifierStr))
			{
				bMatches = true;
			}

			if (bMatches)
			{
				bResult = true;
				break;
			}
		}
	}

	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(AttributeCheck)

#if WITH_EDITOR
FString UPCGExAttributeCheckFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Attribute ") + PCGExCompare::ToString(Config.Match);
	DisplayName += FString::Printf(TEXT(" \"%s\""), *Config.AttributeName);
	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
