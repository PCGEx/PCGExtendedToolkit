// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorSharedData.h"

#include "Data/PCGExPointIO.h"
#include "Selectors/PCGExSelectorFactoryProvider.h"

namespace PCGExCollections
{
	void FSelectorSharedDataCache::SetBatchPointCounts(const PCGExData::FPointIOCollection& InCollection)
	{
		AllInputsPointCount = InCollection.GetTotalNum(PCGExData::EIOSide::In);

		PerInputPointCounts.Init(0, InCollection.Pairs.Num());
		for (const TSharedPtr<PCGExData::FPointIO>& IO : InCollection.Pairs)
		{
			if (IO && PerInputPointCounts.IsValidIndex(IO->IOIndex))
			{
				const UPCGBasePointData* InData = IO->GetIn();
				PerInputPointCounts[IO->IOIndex] = InData ? InData->GetNumPoints() : 0;
			}
		}
	}

	TSharedPtr<FSelectorSharedData> FSelectorSharedDataCache::GetOrBuild(
		const UPCGExSelectorFactoryData* Factory,
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target)
	{
		if (!Factory || !Target)
		{
			return nullptr;
		}

		const FKey Key{Factory, Target};

		FScopeLock Lock(&Mutex);
		if (const TSharedPtr<FSelectorSharedData>* Found = Entries.Find(Key))
		{
			return *Found;
		}

		TSharedPtr<FSelectorSharedData> NewData = Factory->BuildSharedData(Collection, Target);
		if (NewData)
		{
			// Finalize batch-dependent state before publication -- see FSelectorSharedData::OnCached.
			NewData->OnCached(*this);
		}
		// Cache the result even when null so factories that don't participate aren't re-queried.
		Entries.Add(Key, NewData);
#if WITH_EDITOR
		if (NewData)
		{
			++BuildCount;
		}
#endif
		return NewData;
	}
}
