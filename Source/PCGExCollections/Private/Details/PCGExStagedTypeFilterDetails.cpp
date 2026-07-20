// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExStagedTypeFilterDetails.h"
#include "Core/PCGExCollectionHelpers.h"


FPCGExStagedTypeFilterDetails::FPCGExStagedTypeFilterDetails()
{
	RefreshFromRegistry();
}

void FPCGExStagedTypeFilterDetails::RefreshFromRegistry()
{
	// Preserve existing values where possible
	TMap<FName, bool> OldValues = MoveTemp(TypeFilter);
	TypeFilter.Empty();

	// Populate from registry
	PCGExAssetCollection::FTypeRegistry::Get().ForEach([this, &OldValues](const PCGExAssetCollection::FTypeInfo& Info)
	{
		// Skip base type - it's abstract
		if (Info.Id == PCGExAssetCollection::TypeIds::Base || Info.Id == PCGExAssetCollection::TypeIds::None)
		{
			return;
		}

		// Preserve old value if it existed, otherwise default to true
		const bool* OldValue = OldValues.Find(Info.Id);
		TypeFilter.Add(Info.Id, OldValue ? *OldValue : true);
	});
}

bool FPCGExStagedTypeFilterDetails::Matches(PCGExAssetCollection::FTypeId TypeId) const
{
	if (TypeId == PCGExAssetCollection::TypeIds::None || TypeId == PCGExAssetCollection::TypeIds::Base)
	{
		return bIncludeInvalid;
	}

	const bool* Value = TypeFilter.Find(TypeId);
	if (!Value)
	{
		// Unknown type - check parent types
		const PCGExAssetCollection::FTypeId ParentType = PCGExAssetCollection::FTypeRegistry::Get().GetParentType(TypeId);
		if (ParentType != PCGExAssetCollection::TypeIds::None)
		{
			return Matches(ParentType);
		}
		return bIncludeInvalid;
	}

	return *Value;
}

#if WITH_EDITOR
void FPCGExStagedTypeFilterDetails::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Ensure registry types are always present
	RefreshFromRegistry();
}
#endif
