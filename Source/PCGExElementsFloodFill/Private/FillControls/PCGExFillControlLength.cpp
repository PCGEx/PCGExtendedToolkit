// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlLength.h"


#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExFillControlConfigLength::ApplyDeprecation()
{
	MaxLengthValue.Update(MaxLengthInput_DEPRECATED, MaxLengthAttribute_DEPRECATED, MaxLength_DEPRECATED);
}

void FPCGExFillControlConfigLength::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxLength")), FName(TEXT("MaxLengthValue")), FName(TEXT("Constant")), FName(TEXT("Max Length")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxLengthAttribute")), FName(TEXT("MaxLengthValue")), FName(TEXT("Attribute")), FName(TEXT("Max Length (Attr)")));
}
#endif

bool FPCGExFillControlLength::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryLength* TypedFactory = Cast<UPCGExFillControlsFactoryLength>(Factory);
	bUsePathLength = TypedFactory->Config.bUsePathLength;

	DistanceLimit = TypedFactory->Config.MaxLengthValue.GetValueSetting();
	DistanceLimit->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!DistanceLimit->Init(GetSourceFacade()))
	{
		return false;
	}

	return true;
}

bool FPCGExFillControlLength::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return (bUsePathLength ? Candidate.PathDistance : Candidate.Distance) <= DistanceLimit->Read(GetSettingsIndex(Diffusion));
}

bool FPCGExFillControlLength::IsValidProbe(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return (bUsePathLength ? Candidate.PathDistance : Candidate.Distance) <= DistanceLimit->Read(GetSettingsIndex(Diffusion));
}

bool FPCGExFillControlLength::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	return (bUsePathLength ? Candidate.PathDistance : Candidate.Distance) <= DistanceLimit->Read(GetSettingsIndex(Diffusion));
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryLength::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlLength)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryLength::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	if (Config.Source == EPCGExFloodFillSettingSource::Vtx)
	{
		FacadePreloader.Register<double>(InContext, Config.MaxLengthValue.Attribute);
	}
}

UPCGExFactoryData* UPCGExFillControlsLengthProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryLength* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryLength>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
void UPCGExFillControlsLengthProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsLengthProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsLengthProviderSettings::GetDisplayName() const
{
	FString DName = GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC")) + TEXT(" @ ");

	if (Config.MaxLengthValue.Input == EPCGExInputValueType::Attribute)
	{
		DName += Config.MaxLengthValue.Attribute.ToString();
	}
	else
	{
		DName += FString::Printf(TEXT("%.1f"), Config.MaxLengthValue.Constant);
	}

	return DName;
}
#endif
