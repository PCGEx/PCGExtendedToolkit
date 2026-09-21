// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExSpatialTriage.h"

#include "Async/ParallelFor.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSpatialData.h"

#define LOCTEXT_NAMESPACE "PCGExSpatialTriageElement"
#define PCGEX_NAMESPACE SpatialTriage

namespace PCGExSpatialTriage
{
	// Half-open [Min, Max), as UPCGActorHelpers::GetCellCoord floors: a centre on a face shared by two
	// abutting boxes belongs to exactly one of them.
	bool OwnsCenter(const FBox& Box, const FVector& Center)
	{
		return Center.X >= Box.Min.X && Center.X < Box.Max.X
			&& Center.Y >= Box.Min.Y && Center.Y < Box.Max.Y
			&& Center.Z >= Box.Min.Z && Center.Z < Box.Max.Z;
	}
}

TArray<FPCGPinProperties> UPCGExSpatialTriageSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Inputs", Required)
	PCGEX_PIN_SPATIAL(PCGExSpatialTriage::SourceLabelBounds, "Single spatial data whose bounds will be used to do the triage", Required)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSpatialTriageSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	PCGEX_PIN_ANY(PCGExSpatialTriage::OutputLabelInside, "Data fully within bounds and relevant", Normal)
	PCGEX_PIN_ANY(PCGExSpatialTriage::OutputLabelTouching, "Data intersects bounds but not relevant.", Normal)
	PCGEX_PIN_ANY(PCGExSpatialTriage::OutputLabelOutside, "Data neither within nor touching bounds.", Normal)

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SpatialTriage)

bool FPCGExSpatialTriageElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SpatialTriage)

	return true;
}

bool FPCGExSpatialTriageElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSpatialTriageElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SpatialTriage)
	PCGEX_EXECUTION_CHECK

	int32 NumInside = 0;
	int32 NumTouching = 0;
	int32 NumOutside = 0;

	PCGEX_ON_INITIAL_EXECUTION
	{
		TArray<FPCGTaggedData> TaggedDatas = Context->InputData.GetSpatialInputsByPin(PCGExSpatialTriage::SourceLabelBounds);
		if (TaggedDatas.IsEmpty())
		{
			return Context->CancelExecution(TEXT("No valid bounds."));
		}

		const FBox Filter = Cast<UPCGSpatialData>(TaggedDatas[0].Data)->GetBounds();

		TaggedDatas = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

		Context->OutputData.TaggedData.Reserve(TaggedDatas.Num());
		const int32 StartIndex = Context->OutputData.TaggedData.Num();
		Context->OutputData.TaggedData.Append(TaggedDatas);

		ParallelFor(TaggedDatas.Num(), [&](int32 i)
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData[StartIndex + i];
			const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data);
			FName OutputTo = PCGExSpatialTriage::OutputLabelOutside;
			if (SpatialData)
			{
				const FBox Bounds = SpatialData->GetBounds();
				if (Bounds.IsValid)
				{
					if (PCGExSpatialTriage::OwnsCenter(Filter, Bounds.GetCenter()))
					{
						OutputTo = PCGExSpatialTriage::OutputLabelInside;
						FPlatformAtomics::InterlockedIncrement(&NumInside);
					}
					else if (Filter.Intersect(Bounds))
					{
						OutputTo = PCGExSpatialTriage::OutputLabelTouching;
						FPlatformAtomics::InterlockedIncrement(&NumTouching);
					}
				}
				else if (!SpatialData->IsBounded())
				{
					// Unbounded: overlaps every box, owned by none. An empty box falls through to Outside.
					OutputTo = PCGExSpatialTriage::OutputLabelTouching;
					FPlatformAtomics::InterlockedIncrement(&NumTouching);
				}
			}

			if (OutputTo == PCGExSpatialTriage::OutputLabelOutside)
			{
				FPlatformAtomics::InterlockedIncrement(&NumOutside);
			}

			TaggedData.Pin = OutputTo;
		});
	}

	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;
	if (!NumInside)
	{
		Mask |= 1ULL << 0;
	}
	if (!NumTouching)
	{
		Mask |= 1ULL << 1;
	}
	if (!NumOutside)
	{
		Mask |= 1ULL << 2;
	}

	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
