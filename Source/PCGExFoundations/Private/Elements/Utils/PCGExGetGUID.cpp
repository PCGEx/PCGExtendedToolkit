// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExGetGUID.h"

#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/Guid.h"

#define LOCTEXT_NAMESPACE "PCGExGetGUIDElement"
#define PCGEX_NAMESPACE GetGUID

TArray<FPCGPinProperties> UPCGExGetGUIDSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAMS(FName("GUID"), TEXT("GUID."), Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(GetGUID)

PCGExData::EIOHandling UPCGExGetGUIDSettings::GetMainDataHandling() const
{
	return PCGExData::EIOHandling::Transactional;
}

bool FPCGExGetGUIDElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(GetGUID)

	PCGEX_VALIDATE_NAME(Settings->Config.OutputAttributeName)

	return true;
}

bool FPCGExGetGUIDElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetGUIDElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(GetGUID)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		while (Context->AdvancePointsIO())
		{
			PCGEX_MAKE_SHARED(Facade, PCGExData::FFacade, Context->CurrentIO.ToSharedRef())
			TSharedRef<PCGExData::FFacade> FacadeRef = Facade.ToSharedRef();
			FPCGExGUIDDetails Config = Settings->Config;

			int32 TargetIndex = PCGExMath::SanitizeIndex(Settings->Index, FacadeRef->GetNum() - 1, Settings->IndexSafety);

			Facade->bSupportsScopedGet = true;

			if (!FacadeRef->Source->GetIn()->GetConstDensityValueRange().IsValidIndex(TargetIndex))
			{
				return Context->CancelExecution(TEXT("Selected index is invalid."));
			}
			if (!Config.Init(Context, FacadeRef))
			{
				return Context->CancelExecution();
			}

			PCGExMT::FScope FetchScope = PCGExMT::FScope(TargetIndex, 1);
			Facade->Fetch(FetchScope);

			FGuid GUID = FGuid();
			Config.GetGUID(TargetIndex, Facade->Source->GetInPoint(TargetIndex), GUID);

			UPCGParamData* GuidData = Context->ManagedObjects->New<UPCGParamData>();

			if (Config.OutputType == EPCGExGUIDOutputType::Integer)
			{
				GuidData->Metadata->CreateAttribute<int32>(Config.OutputAttributeName, GetTypeHash(GUID.ToString(Config.GUIDFormat)), Config.bAllowInterpolation, true);
			}
			else
			{
				GuidData->Metadata->CreateAttribute<FString>(Config.OutputAttributeName, GUID.ToString(Config.GUIDFormat), Config.bAllowInterpolation, true);
			}

			GuidData->Metadata->AddEntry();

			Context->StageOutput(GuidData, FName("GUID"), PCGExData::EStaging::Managed);
		}

		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
