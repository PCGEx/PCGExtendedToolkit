// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"


#include "Blenders/PCGExMetadataBlender.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExPointElements.h"
#include "Paths/PCGExPathsCommon.h"


void FPCGExSubPointsBlendInterpolate::BlendSubPoints(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, PCGExData::FScope& Scope, const PCGExPaths::FPathMetrics& Metrics) const
{
	EPCGExBlendOver SafeBlendOver = TypedFactory->BlendOver;
	if (TypedFactory->BlendOver == EPCGExBlendOver::Distance && !Metrics.IsValid())
	{
		SafeBlendOver = EPCGExBlendOver::Index;
	}

	if (SafeBlendOver == EPCGExBlendOver::Distance)
	{
		PCGExPaths::FPathMetrics PathMetrics = PCGExPaths::FPathMetrics(From.GetLocation());
		TPCGValueRange<FTransform> OutTransform = Scope.Data->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			FVector Location = OutTransform[Index].GetLocation();
			MetadataBlender->Blend(From.Index, To.Index, Index, Metrics.GetTime(PathMetrics.Add(Location)));
			//OutTransform[Index].SetLocation(Location);
		}
	}
	else if (SafeBlendOver == EPCGExBlendOver::Index)
	{
		const double Divider = Scope.Count;
		TPCGValueRange<FTransform> OutTransform = Scope.Data->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			//FVector Location = OutTransform[Index].GetLocation();
			// Index is absolute into the output data; the weight has to be scope-relative.
			MetadataBlender->Blend(From.Index, To.Index, Index, (Index - Scope.Start) / Divider);
			//OutTransform[Index].SetLocation(Location);
		}
	}
	else if (SafeBlendOver == EPCGExBlendOver::Fixed)
	{
		TPCGValueRange<FTransform> OutTransform = Scope.Data->GetTransformValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			//FVector Location = OutTransform[Index].GetLocation();
			MetadataBlender->Blend(From.Index, To.Index, Index, Lerp);
			//OutTransform[Index].SetLocation(Location);
		}
	}
}

void UPCGExSubPointsBlendInterpolate::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExSubPointsBlendInterpolate* TypedOther = Cast<UPCGExSubPointsBlendInterpolate>(Other))
	{
		BlendOver = TypedOther->BlendOver;
		Lerp = TypedOther->Lerp;
	}
}

TSharedPtr<FPCGExSubPointsBlendOperation> UPCGExSubPointsBlendInterpolate::CreateOperation() const
{
	PCGEX_CREATE_SUBPOINTBLEND_OPERATION(Interpolate)
	NewOperation->TypedFactory = this;
	NewOperation->Lerp = Lerp;
	return NewOperation;
}
