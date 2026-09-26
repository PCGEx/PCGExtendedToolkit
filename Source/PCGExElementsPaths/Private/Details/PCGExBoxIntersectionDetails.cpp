// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExBoxIntersectionDetails.h"

#include "Async/ParallelFor.h"
#include "Blenders/PCGExMetadataBlender.h"
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

void FPCGExBoxIntersectionDetails::SetIntersection(const int32 PointIndex, const PCGExMath::OBB::FCut& InCut) const
{
	check(InCut.Idx != -1)
	if (const TSharedPtr<PCGExData::FDataForwardHandler>& IntersectionForwardHandler = IntersectionForwardHandlers[InCut.Idx])
	{
		IntersectionForwardHandler->Forward(InCut.BoxIndex, PointIndex);
	}

	if (IsIntersectionWriter)
	{
		IsIntersectionWriter->SetValue(PointIndex, true);
	}
	if (CutTypeWriter)
	{
		CutTypeWriter->SetValue(PointIndex, CutTypeValueMapping[InCut.Type]);
	}
	if (NormalWriter)
	{
		NormalWriter->SetValue(PointIndex, InCut.Normal);
	}
	if (BoundIndexWriter)
	{
		BoundIndexWriter->SetValue(PointIndex, InCut.BoxIndex);
	}
}
