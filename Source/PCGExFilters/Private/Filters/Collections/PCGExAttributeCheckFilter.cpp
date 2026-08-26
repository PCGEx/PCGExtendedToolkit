// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExAttributeCheckFilter.h"


#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"

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

	// An unprefixed name resolves to the Default domain id, which compares equal to no concrete domain --
	// resolve it to the data's actual default so Match works for plain names.
	FPCGMetadataDomainID MatchDomain = Identifier.MetadataDomain;
	if (MatchDomain.IsDefault())
	{
		if (const FPCGMetadataDomain* DefaultDomain = IO->GetIn()->Metadata->GetConstDefaultMetadataDomain())
		{
			MatchDomain = DefaultDomain->GetDomainID();
		}
	}

	for (const PCGExData::FAttributeIdentity& Identity : Infos->Identities)
	{
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
			if (TypedFilterFactory->Config.Domain == EPCGExAttribtueDomainCheck::Match && Identity.Identifier.MetadataDomain != MatchDomain)
			{
				continue;
			}
		}

		if (!PCGExCompare::Compare(TypedFilterFactory->Config.Match, Identity.Identifier.Name.ToString(), IdentifierStr))
		{
			continue;
		}

		if (!TypedFilterFactory->Config.bDoCheckType || Identity.UnderlyingType == TypedFilterFactory->Config.Type)
		{
			bResult = true;
			break;
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
