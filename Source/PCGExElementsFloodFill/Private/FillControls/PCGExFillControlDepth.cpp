// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlDepth.h"


#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExFillControlConfigDepth::ApplyDeprecation()
{
	MaxDepthValue.Update(MaxDepthInput_DEPRECATED, MaxDepthAttribute_DEPRECATED, MaxDepth_DEPRECATED);
}

void FPCGExFillControlConfigDepth::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxDepth")), FName(TEXT("MaxDepthValue")), FName(TEXT("Constant")), FName(TEXT("Max Depth")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxDepthAttribute")), FName(TEXT("MaxDepthValue")), FName(TEXT("Attribute")), FName(TEXT("Max Depth (Attr)")));
}
#endif

bool FPCGExFillControlDepth::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryDepth* TypedFactory = Cast<UPCGExFillControlsFactoryDepth>(Factory);

	DepthLimit = TypedFactory->Config.MaxDepthValue.GetValueSetting();
	DepthLimit->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!DepthLimit->Init(GetSourceFacade()))
	{
		return false;
	}

	return true;
}

bool FPCGExFillControlDepth::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return Candidate.Depth <= DepthLimit->Read(GetSettingsIndex(Diffusion));
}

bool FPCGExFillControlDepth::IsValidProbe(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return Candidate.Depth <= DepthLimit->Read(GetSettingsIndex(Diffusion));
}

bool FPCGExFillControlDepth::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	return Candidate.Depth <= DepthLimit->Read(GetSettingsIndex(Diffusion));
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryDepth::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlDepth)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryDepth::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	if (Config.Source == EPCGExFloodFillSettingSource::Vtx)
	{
		FacadePreloader.Register<int32>(InContext, Config.MaxDepthValue.Attribute);
	}
}

UPCGExFactoryData* UPCGExFillControlsDepthProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryDepth* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryDepth>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
void UPCGExFillControlsDepthProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsDepthProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsDepthProviderSettings::GetDisplayName() const
{
	FString DName = GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC")) + TEXT(" @ ");

	if (Config.MaxDepthValue.Input == EPCGExInputValueType::Attribute)
	{
		DName += Config.MaxDepthValue.Attribute.ToString();
	}
	else
	{
		DName += FString::Printf(TEXT("%d"), Config.MaxDepthValue.Constant);
	}

	return DName;
}
#endif
