// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExBlendingHelpers.h"

#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Metadata/PCGMetadata.h"
#include "Types/PCGExAttributeIdentity.h"

namespace PCGExBlending::Helpers
{
	void MergeBestCandidatesAttributes(
		const TSharedPtr<PCGExData::FPointIO>& Target,
		const TArray<TSharedPtr<PCGExData::FPointIO>>& Collections,
		const TArray<int32>& BestIndices,
		const PCGExData::FAttributesInfos& InAttributesInfos)
	{
		UPCGMetadata* OutMetadata = Target->GetOut()->Metadata;

		for (int i = 0; i < BestIndices.Num(); i++)
		{
			const TSharedPtr<PCGExData::FPointIO> IO = Collections[i];

			if (BestIndices[i] == -1 || !IO)
			{
				continue;
			}

			PCGMetadataEntryKey InKey = IO->GetIn()->GetMetadataEntry(BestIndices[i]);
			PCGMetadataEntryKey OutKey = Target->GetOut()->GetMetadataEntry(i);
			UPCGMetadata* InMetadata = IO->GetIn()->Metadata;

			for (const PCGExData::FAttributeIdentity& Identity : InAttributesInfos.Identities)
			{
				PCGExMetaHelpers::ExecuteWithRightType(Identity.GetTypeId(), [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					const FPCGMetadataAttribute<T>* InAttribute = InMetadata->GetConstTypedAttribute<T>(Identity.Identifier);
					FPCGMetadataAttribute<T>* OutAttribute = PCGExMetaHelpers::TryGetMutableAttribute<T>(OutMetadata, Identity.Identifier);

					if (!OutAttribute)
					{
						OutAttribute = Target->FindOrCreateAttribute<T>(Identity.Identifier, InAttribute->GetMetadataDomain()->GetDomainID().Flag == EPCGMetadataDomainFlag::Data ? PCGExData::Helpers::ReadDataValue(InAttribute) : InAttribute->GetValueFromItemKey(PCGDefaultValueKey), InAttribute->AllowsInterpolation());
					}

					if (!OutAttribute)
					{
						return;
					}

					OutAttribute->SetValue(OutKey, InAttribute->GetValueFromItemKey(InKey));
				});
			}
		}
	}
}
