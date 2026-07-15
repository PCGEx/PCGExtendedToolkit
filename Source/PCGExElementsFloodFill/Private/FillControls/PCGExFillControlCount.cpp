// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlCount.h"


#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExFillControlConfigCount::ApplyDeprecation()
{
	MaxCountValue.Update(MaxCountInput_DEPRECATED, MaxCountAttribute_DEPRECATED, MaxCount_DEPRECATED);
}

void FPCGExFillControlConfigCount::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxCount")), FName(TEXT("MaxCountValue")), FName(TEXT("Constant")), FName(TEXT("Max Count")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxCountAttribute")), FName(TEXT("MaxCountValue")), FName(TEXT("Attribute")), FName(TEXT("Max Count (Attr)")));
}
#endif

bool FPCGExFillControlCount::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryCount* TypedFactory = Cast<UPCGExFillControlsFactoryCount>(Factory);

	CountLimit = TypedFactory->Config.MaxCountValue.GetValueSetting();
	CountLimit->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!CountLimit->Init(GetSourceFacade()))
	{
		return false;
	}

	return true;
}

bool FPCGExFillControlCount::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	const int32 Limit = CountLimit->Read(GetSettingsIndex(Diffusion));
	return Diffusion->Captured.Num() < Limit;
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryCount::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlCount)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryCount::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	if (Config.Source == EPCGExFloodFillSettingSource::Vtx)
	{
		FacadePreloader.Register<int32>(InContext, Config.MaxCountValue.Attribute);
	}
}

UPCGExFactoryData* UPCGExFillControlsCountProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryCount* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryCount>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
void UPCGExFillControlsCountProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsCountProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsCountProviderSettings::GetDisplayName() const
{
	FString DName = GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC")) + TEXT(" @ ");

	if (Config.MaxCountValue.Input == EPCGExInputValueType::Attribute)
	{
		DName += Config.MaxCountValue.Attribute.ToString();
	}
	else
	{
		DName += FString::Printf(TEXT("%d"), Config.MaxCountValue.Constant);
	}

	return DName;
}
#endif
