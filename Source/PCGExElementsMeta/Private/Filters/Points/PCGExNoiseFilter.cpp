// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExNoiseFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExNoise3DCommon.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExNoiseGenerator.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExNoiseFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	NoiseGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!NoiseGenerator->Init(InContext))
	{
		return false;
	}

	return true;
}

// Makes the collection-level Test reachable: noise is sampled from the transform/bounds alone,
// so only the comparison operand gates data-domain-only evaluation.
bool UPCGExNoiseFilterFactory::DomainCheck()
{
	return Config.Comparison.Input == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.Comparison.Attribute);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNoiseFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FNoiseFilter>(this);
}

void UPCGExNoiseFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.Comparison.RegisterBufferDependencies(InContext, FacadePreloader);
}

bool PCGExPointFilter::FNoiseFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	NoiseGenerator = TypedFilterFactory->NoiseGenerator;

	OperandB = TypedFilterFactory->Config.Comparison.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandB->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandB->Init(PointDataFacade))
	{
		return false;
	}

	InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

bool PCGExPointFilter::FNoiseFilter::Test(const int32 PointIndex) const
{
	return TypedFilterFactory->Config.Comparison.Compare(
		NoiseGenerator->GetDouble(InTransforms[PointIndex].GetLocation()),
		OperandB->Read(PointIndex));
}

bool PCGExPointFilter::FNoiseFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double B = 0;

	if (!TypedFilterFactory->Config.Comparison.TryReadDataValue(IO, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	return TypedFilterFactory->Config.Comparison.Compare(
		NoiseGenerator->GetDouble(IO->GetIn()->GetBounds().GetCenter()),
		B);
}

TArray<FPCGPinProperties> UPCGExNoiseFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DLabel, "Noises", Required, FPCGExDataTypeInfoNoise3D::AsId())
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Noise)

#if WITH_EDITOR
FString UPCGExNoiseFilterProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
