// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExInfluenceDetails.h"

#include "Details/PCGExSettingsDetails.h"

bool FPCGExInfluenceDetails::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
{
	InfluenceBuffer = InfluenceValue.GetValueSetting();
	return InfluenceBuffer->Init(InPointDataFacade, false);
}

double FPCGExInfluenceDetails::GetInfluence(const int32 PointIndex) const
{
	return InfluenceBuffer->Read(PointIndex);
}

#if WITH_EDITOR
void FPCGExInfluenceDetails::ApplyDeprecation()
{
	InfluenceValue.Update(InfluenceInput_DEPRECATED, LocalInfluence_DEPRECATED, Influence_DEPRECATED);
}

void FPCGExInfluenceDetails::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("LocalInfluence")), FName(TEXT("InfluenceValue")), FName(TEXT("Attribute")), FName(TEXT("Influence (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Influence")), FName(TEXT("InfluenceValue")), FName(TEXT("Constant")), FName(TEXT("Influence")));
}
#endif
