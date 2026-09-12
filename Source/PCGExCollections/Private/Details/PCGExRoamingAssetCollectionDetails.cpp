// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExRoamingAssetCollectionDetails.h"

#include "Collections/PCGExOmniCollection.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Details/PCGExSettingsDetails.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

bool FPCGExRoamingAssetCollectionDetails::Validate(FPCGExContext* InContext) const
{
	if (AssetPathSourceAttribute.IsNone())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Asset path attribute is not set."));
		return false;
	}

	return true;
}

FString FPCGExRoamingAssetCollectionDetails::GetConfigId() const
{
	TStringBuilder<512> Builder;
	Builder << AssetPathSourceAttribute << TEXT('|') << WeightSourceAttribute << TEXT('|') << CategorySourceAttribute;
	Builder << TEXT('|') << static_cast<int32>(PropertyAttributes.FilterMode)
		<< TEXT('|') << PropertyAttributes.CommaSeparatedNames
		<< TEXT('|') << static_cast<int32>(PropertyAttributes.CommaSeparatedNameFilter)
		<< TEXT('|') << (PropertyAttributes.bPreservePCGExData ? 1 : 0);
	for (const TPair<FString, EPCGExStringMatchMode>& Match : PropertyAttributes.Matches)
	{
		Builder << TEXT('|') << Match.Key << TEXT(':') << static_cast<int32>(Match.Value);
	}
	Builder << TEXT('|') << DefaultStagingBoundsMin.ToString() << TEXT('|') << DefaultStagingBoundsMax.ToString();
	return Builder.ToString();
}

UPCGExAssetCollection* FPCGExRoamingAssetCollectionDetails::TryBuildCollection(FPCGExContext* InContext, const UPCGParamData* InAttributeSet, const bool bBuildStaging) const
{
	UPCGExOmniCollection* Collection = InContext->ManagedObjects->New<UPCGExOmniCollection>(GetTransientPackage());
	if (!Collection)
	{
		return nullptr;
	}

	if (!PCGExCollectionHelpers::BuildFromAttributeSet(Collection, InContext, InAttributeSet, *this, bBuildStaging))
	{
		InContext->ManagedObjects->Destroy(Collection);
		return nullptr;
	}

	return Collection;
}

UPCGExAssetCollection* FPCGExRoamingAssetCollectionDetails::TryBuildCollection(FPCGExContext* InContext, const FName InputPin, const bool bBuildStaging) const
{
	UPCGExOmniCollection* Collection = InContext->ManagedObjects->New<UPCGExOmniCollection>(GetTransientPackage());
	if (!Collection)
	{
		return nullptr;
	}

	if (!PCGExCollectionHelpers::BuildFromAttributeSet(Collection, InContext, InputPin, *this, bBuildStaging))
	{
		InContext->ManagedObjects->Destroy(Collection);
		return nullptr;
	}

	return Collection;
}
