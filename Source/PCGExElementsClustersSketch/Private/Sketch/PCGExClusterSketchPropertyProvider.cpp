// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchPropertyProvider.h"

#include "Sketch/PCGExClusterSketchData.h"
#include "Sketch/PCGExClusterSketchModel.h"

namespace PCGExClusterSketchPropertyProvider
{
	/** Flatten DataId -> record once, so the per-row lookup is an array read rather than a map probe --
	 *  and so nothing is memoized later, which the concurrent edge path forbids. */
	template <typename TItem>
	void BuildItemRecords(const FPCGExSketchDataLayer& InLayer, const TConstArrayView<TItem> InItems, TArray<const FPCGExSketchDataRecord*>& OutRecords)
	{
		if (InLayer.Records.IsEmpty())
		{
			return;
		}

		TMap<uint32, int32> RecordIndex;
		InLayer.BuildRecordIndex(RecordIndex);

		OutRecords.SetNumZeroed(InItems.Num());
		for (int32 i = 0; i < InItems.Num(); ++i)
		{
			// An invalid id is the sparse default and must never match a record that also carries one.
			if (InItems[i].DataId == PCGExSketch::InvalidRecordId)
			{
				continue;
			}
			if (const int32* Found = RecordIndex.Find(InItems[i].DataId))
			{
				OutRecords[i] = &InLayer.Records[*Found];
			}
		}
	}
}

FPCGExSketchLayerPropertyProvider::FPCGExSketchLayerPropertyProvider(const FPCGExSketchDataLayer& InLayer, const TConstArrayView<FPCGExClusterSketchVertex> InVertices)
	: Layer(InLayer)
{
	PCGExClusterSketchPropertyProvider::BuildItemRecords(InLayer, InVertices, ItemRecords);
}

FPCGExSketchLayerPropertyProvider::FPCGExSketchLayerPropertyProvider(const FPCGExSketchDataLayer& InLayer, const TConstArrayView<FPCGExClusterSketchEdge> InEdges)
	: Layer(InLayer)
{
	PCGExClusterSketchPropertyProvider::BuildItemRecords(InLayer, InEdges, ItemRecords);
}

const FInstancedStruct* FPCGExSketchLayerPropertyProvider::FindPrototypeProperty(const FName PropertyName) const
{
	return Layer.Schema.GetPropertyByName(PropertyName);
}

const FInstancedStruct* FPCGExSketchLayerPropertyProvider::GetPropertyAt(const int32 SourceIndex, const FName PropertyName) const
{
	const FPCGExSketchDataRecord* Record = ItemRecords.IsValidIndex(SourceIndex) ? ItemRecords[SourceIndex] : nullptr;
	return Layer.ResolveEffectiveFrom(Record, PropertyName);
}
