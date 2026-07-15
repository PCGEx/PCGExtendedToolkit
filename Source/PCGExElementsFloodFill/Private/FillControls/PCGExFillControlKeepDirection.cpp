// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "FillControls/PCGExFillControlKeepDirection.h"


#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExFillControlConfigKeepDirection::ApplyDeprecation()
{
	WindowSizeValue.Update(WindowSizeInput_DEPRECATED, WindowSizeAttribute_DEPRECATED, WindowSize_DEPRECATED);
}

void FPCGExFillControlConfigKeepDirection::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("WindowSize")), FName(TEXT("WindowSizeValue")), FName(TEXT("Constant")), FName(TEXT("Window Size")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("WindowSizeAttribute")), FName(TEXT("WindowSizeValue")), FName(TEXT("Attribute")), FName(TEXT("Window Size (Attr)")));
}
#endif

bool FPCGExFillControlKeepDirection::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryKeepDirection* TypedFactory = Cast<UPCGExFillControlsFactoryKeepDirection>(Factory);

	WindowSize = TypedFactory->Config.WindowSizeValue.GetValueSetting();
	WindowSize->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!WindowSize->Init(GetSourceFacade()))
	{
		return false;
	}

	HashComparisonDetails = TypedFactory->Config.HashComparisonDetails;
	if (!HashComparisonDetails.Init(InContext, GetSourceFacade().ToSharedRef()))
	{
		return false;
	}

	return true;
}

bool FPCGExFillControlKeepDirection::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	const int32 Window = WindowSize->Read(GetSettingsIndex(Diffusion));

	int32 PathNodeIndex = PCGEx::NH64A(Diffusion->TravelStack->Get(From.Node->Index));
	int32 PathEdgeIndex = -1;

	if (PathNodeIndex != -1)
	{
		const FVector CurrentDir = Cluster->GetDir(From.Node->Index, Candidate.Node->Index);

		FVector Avg = FVector::ZeroVector;
		int32 Sampled = 0;

		while (PathNodeIndex != -1 && Sampled < Window)
		{
			const int32 CurrentIndex = PathNodeIndex;
			PCGEx::NH64(Diffusion->TravelStack->Get(CurrentIndex), PathNodeIndex, PathEdgeIndex);

			if (PathEdgeIndex < 0)
			{
				continue;
			}

			Avg += Cluster->GetDir(PathNodeIndex, CurrentIndex);
			Sampled++;
		}

		if (Sampled < 1)
		{
			return true;
		}

		return HashComparisonDetails.Test(CurrentDir, (Avg / Sampled).GetSafeNormal(), GetSettingsIndex(Diffusion));
	}
	return true;
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryKeepDirection::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlKeepDirection)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryKeepDirection::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	if (Config.Source == EPCGExFloodFillSettingSource::Vtx)
	{
		FacadePreloader.Register<int32>(InContext, Config.WindowSizeValue.Attribute);
	}
}

UPCGExFactoryData* UPCGExFillControlsKeepDirectionProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryKeepDirection* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryKeepDirection>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
void UPCGExFillControlsKeepDirectionProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsKeepDirectionProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsKeepDirectionProviderSettings::GetDisplayName() const
{
	FString DName = GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC")) + TEXT(" @ ");

	if (Config.WindowSizeValue.Input == EPCGExInputValueType::Attribute)
	{
		DName += PCGExMetaHelpers::GetSelectorDisplayName(Config.WindowSizeValue.Attribute);
	}
	else
	{
		DName += FString::Printf(TEXT("%d"), Config.WindowSizeValue.Constant);
	}

	return DName;
}
#endif
