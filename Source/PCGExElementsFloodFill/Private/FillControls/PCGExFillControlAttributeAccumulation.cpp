// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "FillControls/PCGExFillControlAttributeAccumulation.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExFillControlConfigAttributeAccumulation::ApplyDeprecation()
{
	MaxAccumulationValue.Update(MaxAccumulationInput_DEPRECATED, MaxAccumulationAttribute_DEPRECATED, MaxAccumulation_DEPRECATED);
}

void FPCGExFillControlConfigAttributeAccumulation::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxAccumulation")), FName(TEXT("MaxAccumulationValue")), FName(TEXT("Constant")), FName(TEXT("Max Accumulation")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("MaxAccumulationAttribute")), FName(TEXT("MaxAccumulationValue")), FName(TEXT("Attribute")), FName(TEXT("Max Accumulation (Attr)")));
}
#endif

bool FPCGExFillControlAttributeAccumulation::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryAttributeAccumulation* TypedFactory = Cast<UPCGExFillControlsFactoryAttributeAccumulation>(Factory);

	AttributeSource = TypedFactory->Config.AttributeSource;
	Mode = TypedFactory->Config.Mode;
	bWriteToAccumulatedValue = TypedFactory->Config.bWriteToAccumulatedValue;

	// Initialize max accumulation setting value
	MaxAccumulation = TypedFactory->Config.MaxAccumulationValue.GetValueSetting();
	MaxAccumulation->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!MaxAccumulation->Init(GetSourceFacade()))
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
		PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("Attribute '{0}' not found for Attribute Accumulation fill control."), FText::FromName(TypedFactory->Config.Attribute.GetName())));
		return false;
	}

	return true;
}

void FPCGExFillControlAttributeAccumulation::ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate)
{
	const int32 Index = (AttributeSource == EPCGExClusterElement::Vtx)
		? OutCandidate.Node->PointIndex
		: OutCandidate.Link.Edge;

	if (Index < 0)
	{
		return;
	} // Invalid index

	const double NewValue = AttributeBuffer->Read(Index);
	const double PreviousAccumulated = From.AccumulatedValue;

	// Compute new accumulated value based on mode
	const double Accumulated = ComputeAccumulation(PreviousAccumulated, NewValue, OutCandidate.Depth);

	if (bWriteToAccumulatedValue)
	{
		OutCandidate.AccumulatedValue = Accumulated;
	}

	// Also contribute to Score for sorting purposes
	OutCandidate.Score += NewValue;
}

double FPCGExFillControlAttributeAccumulation::ComputeAccumulation(double PreviousAccumulated, double NewValue, int32 Depth) const
{
	switch (Mode)
	{
	case EPCGExAccumulationMode::Sum:
		return PreviousAccumulated + NewValue;

	case EPCGExAccumulationMode::Max:
		return FMath::Max(PreviousAccumulated, NewValue);

	case EPCGExAccumulationMode::Min:
		// For min, we need to handle the initial case (depth 1 means first non-seed node)
		if (Depth <= 1)
		{
			return NewValue;
		}
		return FMath::Min(PreviousAccumulated, NewValue);

	case EPCGExAccumulationMode::Average:
		// Running average: ((prev * (n-1)) + new) / n where n is the depth
		if (Depth <= 1)
		{
			return NewValue;
		}
		return ((PreviousAccumulated * (Depth - 1)) + NewValue) / Depth;
	}

	return PreviousAccumulated + NewValue;
}

bool FPCGExFillControlAttributeAccumulation::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	const double MaxValue = MaxAccumulation->Read(GetSettingsIndex(Diffusion));
	return Candidate.AccumulatedValue <= MaxValue;
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryAttributeAccumulation::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlAttributeAccumulation)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryAttributeAccumulation::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	// Register the attribute we'll read
	if (Config.AttributeSource == EPCGExClusterElement::Vtx)
	{
		FacadePreloader.Register<double>(InContext, Config.Attribute.GetName());
	}
	// Edge attributes handled differently
}

UPCGExFactoryData* UPCGExFillControlsAttributeAccumulationProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryAttributeAccumulation* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryAttributeAccumulation>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	Super::CreateFactory(InContext, NewFactory);

	return NewFactory;
}

#if WITH_EDITOR
void UPCGExFillControlsAttributeAccumulationProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsAttributeAccumulationProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsAttributeAccumulationProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control"), TEXT("FC"));
}
#endif
