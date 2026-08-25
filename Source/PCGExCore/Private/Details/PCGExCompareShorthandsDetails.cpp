// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExCompareShorthandsDetails.h"

#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"


PCGEX_SETTING_VALUE_IMPL_SHORTHAND(FPCGExCompareSelectorDouble, , double, Input, Attribute, Constant)

bool FPCGExCompareSelectorDouble::TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, double& OutValue, const bool bQuiet) const
{
	return PCGExData::Helpers::TryGetSettingDataValue(IO, Input, Attribute, Constant, OutValue, bQuiet);
}

#if WITH_EDITOR
FString FPCGExCompareSelectorDouble::GetDisplayNamePostfix() const
{
	FString DisplayName = PCGExCompare::ToString(Comparison);
	if (Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Attribute);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%.1f"), Constant);
	}
	return DisplayName;
}
#endif

void FPCGExCompareSelectorDouble::RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (Input == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Attribute);
	}
}
