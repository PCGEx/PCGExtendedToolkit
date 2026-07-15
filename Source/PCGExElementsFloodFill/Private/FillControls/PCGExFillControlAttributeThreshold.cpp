// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "FillControls/PCGExFillControlAttributeThreshold.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Utils/PCGExCompare.h"

#if WITH_EDITOR
void FPCGExFillControlConfigAttributeThreshold::ApplyDeprecation()
{
	ThresholdValue.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, Threshold_DEPRECATED);
}

void FPCGExFillControlConfigAttributeThreshold::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Threshold")), FName(TEXT("ThresholdValue")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("ThresholdValue")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

bool FPCGExFillControlAttributeThreshold::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryAttributeThreshold* TypedFactory = Cast<UPCGExFillControlsFactoryAttributeThreshold>(Factory);

	AttributeSource = TypedFactory->Config.AttributeSource;
	Comparison = TypedFactory->Config.Comparison;

	// Initialize threshold setting value
	Threshold = TypedFactory->Config.ThresholdValue.GetValueSetting();
	Threshold->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!Threshold->Init(GetSourceFacade()))
	{
		return false;
	}

	// Get the attribute buffer
	TSharedPtr<PCGExData::FFacade> SourceFacade = (AttributeSource == EPCGExClusterElement::Vtx)
		? InHandler->VtxDataFacade
		: InHandler->EdgeDataFacade;

	AttributeBuffer = SourceFacade->GetReadable<double>(TypedFactory->Config.Attribute.GetName());
	if (!AttributeBuffer)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("Attribute '{0}' not found for Attribute Threshold fill control."), FText::FromName(TypedFactory->Config.Attribute.GetName())));
		return false;
	}

	return true;
}

bool FPCGExFillControlAttributeThreshold::IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	return TestCandidate(Diffusion, Candidate);
}

bool FPCGExFillControlAttributeThreshold::IsValidProbe(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate)
{
	if (Candidate.Link.Edge == -1)
	{
		return true;
	} // Seed node
	return TestCandidate(Diffusion, Candidate);
}

bool FPCGExFillControlAttributeThreshold::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	return TestCandidate(Diffusion, Candidate);
}

bool FPCGExFillControlAttributeThreshold::TestCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate) const
{
	const int32 Index = (AttributeSource == EPCGExClusterElement::Vtx)
		? Candidate.Node->PointIndex
		: Candidate.Link.Edge;

	if (Index < 0)
	{
		return true;
	} // Invalid index, pass through

	const double Value = AttributeBuffer->Read(Index);
	const double ThresholdValue = Threshold->Read(GetSettingsIndex(Diffusion));

	return PCGExCompare::Compare(Comparison, Value, ThresholdValue);
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryAttributeThreshold::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlAttributeThreshold)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryAttributeThreshold::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	// Register the attribute we'll read
	if (Config.AttributeSource == EPCGExClusterElement::Vtx)
	{
		FacadePreloader.Register<double>(InContext, Config.Attribute.GetName());
	}
	// Edge attributes handled differently
}

UPCGExFactoryData* UPCGExFillControlsAttributeThresholdProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryAttributeThreshold* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryAttributeThreshold>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	Super::CreateFactory(InContext, NewFactory);

	return NewFactory;
}

#if WITH_EDITOR
void UPCGExFillControlsAttributeThresholdProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsAttributeThresholdProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsAttributeThresholdProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC"));
}
#endif
