// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExMatchingDetails.h"

#include "Details/PCGExInputShorthandsDetails.h"

#if WITH_EDITOR
void FPCGExMatchingDetails::ApplyDeprecation()
{
	LimitValue.Update(LimitInput_DEPRECATED, LimitAttribute_DEPRECATED, Limit_DEPRECATED);
}

void FPCGExMatchingDetails::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("LimitAttribute")), FName(TEXT("LimitValue")), FName(TEXT("Attribute")), FName(TEXT("Limit (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Limit")), FName(TEXT("LimitValue")), FName(TEXT("Constant")), FName(TEXT("Limit")));
}
#endif
