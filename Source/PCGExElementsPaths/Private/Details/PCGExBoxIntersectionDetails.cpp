// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExBoxIntersectionDetails.h"

#include "Algo/AnyOf.h"
#include "Async/ParallelFor.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Containers/ArrayView.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Math/OBB/PCGExOBBIntersections.h"


FPCGExBoxIntersectionDetails::FPCGExBoxIntersectionDetails()
{
	if (const UEnum* EnumClass = StaticEnum<EPCGExCutType>())
	{
		const int32 NumEnums = EnumClass->NumEnums() - 1; // Skip _MAX 
		for (int32 i = 0; i < NumEnums; ++i)
		{
			CutTypeValueMapping.Add(static_cast<EPCGExCutType>(EnumClass->GetValueByIndex(i)), i);
		}
	}
}

bool FPCGExBoxIntersectionDetails::Validate(const FPCGContext* InContext) const
{
#define PCGEX_LOCAL_DETAIL_CHECK(_NAME, _TYPE, _DEFAULT) if (bWrite##_NAME) { PCGEX_VALIDATE_NAME_C(InContext, _NAME##AttributeName) }
	PCGEX_FOREACH_FIELD_INTERSECTION(PCGEX_LOCAL_DETAIL_CHECK)
#undef PCGEX_LOCAL_DETAIL_CHECK

	return true;
}

void FPCGExBoxIntersectionDetails::Init(const TSharedPtr<PCGExData::FFacade>& PointDataFacade, const TSharedPtr<PCGExMatching::FTargetsHandler>& TargetsHandler)
{
	const int32 NumTargets = TargetsHandler->Num();
	IntersectionForwardHandlers.Init(nullptr, NumTargets);

	TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& InTarget, const int32 Index)
	{
		IntersectionForwardHandlers[Index] = IntersectionForwarding.TryGetHandler(InTarget, PointDataFacade, PCGExData::EForwardDomain::Inherit);
	});

	if (Algo::AnyOf(IntersectionForwardHandlers))
	{
		ForwardSourceRows.Init(-1, PointDataFacade->GetOut()->GetNumPoints());
	}

#define PCGEX_LOCAL_DETAIL_WRITER(_NAME, _TYPE, _DEFAULT) if (bWrite##_NAME){ _NAME##Writer = PointDataFacade->GetWritable( _NAME##AttributeName, _DEFAULT, true, PCGExData::EBufferInit::Inherit); }
	PCGEX_FOREACH_FIELD_INTERSECTION(PCGEX_LOCAL_DETAIL_WRITER)
#undef PCGEX_LOCAL_DETAIL_WRITER
}

bool FPCGExBoxIntersectionDetails::WillWriteAny() const
{
#define PCGEX_LOCAL_DETAIL_WILL_WRITE(_NAME, _TYPE, _DEFAULT) if (bWrite##_NAME){ return true; }
	PCGEX_FOREACH_FIELD_INTERSECTION(PCGEX_LOCAL_DETAIL_WILL_WRITE)
#undef PCGEX_LOCAL_DETAIL_WILL_WRITE

	return IntersectionForwarding.bEnabled;
}

void FPCGExBoxIntersectionDetails::Mark(const TSharedRef<PCGExData::FPointIO>& InPointIO) const
{
#define PCGEX_LOCAL_DETAIL_MARK(_NAME, _TYPE, _DEFAULT) if (bWrite##_NAME) { PCGExData::WriteMark(InPointIO, _NAME##AttributeName, _DEFAULT); }
	PCGEX_FOREACH_FIELD_INTERSECTION(PCGEX_LOCAL_DETAIL_MARK)
#undef PCGEX_LOCAL_DETAIL_MARK
}

void FPCGExBoxIntersectionDetails::SetIntersections(const int32 StartIndex, const TConstArrayView<PCGExMath::OBB::FCut> Cuts)
{
	const int32 NumCuts = Cuts.Num();

	// Forwarding precedes the writers below so a same-named writer keeps the last word on each point.
	if (!ForwardSourceRows.IsEmpty())
	{
		for (int32 j = 0; j < NumCuts; j++)
		{
			ForwardSourceRows[StartIndex + j] = Cuts[j].BoxIndex;
		}

		// One scoped call per run of consecutive cuts sharing a target IO, in output order, so a single-slot
		// (@Data) writer still ends up holding the last forwarded row.
		int32 RunStart = 0;
		while (RunStart < NumCuts)
		{
			const int32 TargetIOIndex = Cuts[RunStart].Idx;
			check(TargetIOIndex != -1)
			int32 RunEnd = RunStart + 1;
			while (RunEnd < NumCuts && Cuts[RunEnd].Idx == TargetIOIndex)
			{
				RunEnd++;
			}

			if (const TSharedPtr<PCGExData::FDataForwardHandler>& Handler = IntersectionForwardHandlers[TargetIOIndex])
			{
				Handler->ForwardScoped(PCGExMT::FScope(StartIndex + RunStart, RunEnd - RunStart), ForwardSourceRows);
			}

			RunStart = RunEnd;
		}
	}

	for (int32 j = 0; j < NumCuts; j++)
	{
		const PCGExMath::OBB::FCut& Cut = Cuts[j];
		const int32 PointIndex = StartIndex + j;

		if (IsIntersectionWriter)
		{
			IsIntersectionWriter->SetValue(PointIndex, true);
		}
		if (CutTypeWriter)
		{
			CutTypeWriter->SetValue(PointIndex, CutTypeValueMapping[Cut.Type]);
		}
		if (NormalWriter)
		{
			NormalWriter->SetValue(PointIndex, Cut.Normal);
		}
		if (BoundIndexWriter)
		{
			BoundIndexWriter->SetValue(PointIndex, Cut.BoxIndex);
		}
	}
}
