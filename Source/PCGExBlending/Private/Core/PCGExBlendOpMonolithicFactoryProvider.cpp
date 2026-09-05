// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlendOpMonolithicFactoryProvider.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"

#define LOCTEXT_NAMESPACE "PCGExBlendOpMonolithic"
#define PCGEX_NAMESPACE BlendOpMonolithic

#pragma region UPCGExBlendOpMonolithicFactory

bool UPCGExBlendOpMonolithicFactory::ResolveConfigs(
	FPCGExContext* InContext,
	const TSharedPtr<PCGExData::FFacade>& InSourceAFacade,
	TArray<FPCGExAttributeBlendConfig>& OutConfigs,
	const TSet<FName>* InSupersedeNames) const
{
	TArray<PCGExBlending::FBlendingParam> Params;
	TArray<FPCGAttributeIdentifier> AttributeIdentifiers;

	BlendingDetails.GetPointPropertyBlendingParams(Params);

	// Gather attribute params from source metadata
	if (InSourceAFacade)
	{
		BlendingDetails.GetBlendingParams(
			InSourceAFacade->GetIn()->Metadata, InSourceAFacade->GetIn()->Metadata,
			Params, AttributeIdentifiers,
			true, // bSkipProperties - already handled above
			InSupersedeNames);
	}

	OutConfigs.Reserve(OutConfigs.Num() + Params.Num());

	for (const PCGExBlending::FBlendingParam& Param : Params)
	{
		if (Param.Blending == EPCGExABBlendingType::None)
		{
			continue;
		}

		// Check supersede set
		if (InSupersedeNames && InSupersedeNames->Contains(Param.Identifier.Name))
		{
			continue;
		}

		FPCGExAttributeBlendConfig OpConfig;
		OpConfig.BlendMode = Param.Blending;
		OpConfig.OperandA = Param.Selector;
		OpConfig.bUseOperandB = false;
		OpConfig.OutputMode = EPCGExBlendOpOutputMode::SameAsA;
		OpConfig.bResetValueBeforeMultiSourceBlend = true;
		OpConfig.Init();

		OutConfigs.Add(MoveTemp(OpConfig));
	}

	return true;
}

void UPCGExBlendOpMonolithicFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	// Skip UPCGExBlendOpFactory::RegisterBuffersDependencies which uses Config.OperandA/B
	UPCGExFactoryData::RegisterBuffersDependencies(InContext, FacadePreloader);
	BlendingDetails.RegisterBuffersDependencies(InContext, FacadePreloader);
}

void UPCGExBlendOpMonolithicFactory::RegisterBuffersDependenciesForSourceA(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	BlendingDetails.RegisterBuffersDependencies(InContext, FacadePreloader);
}

void UPCGExBlendOpMonolithicFactory::RegisterBuffersDependenciesForSourceB(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	BlendingDetails.RegisterBuffersDependencies(InContext, FacadePreloader);
}

bool UPCGExBlendOpMonolithicFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	// Monolithic blending can consume any attribute that it blends
	// We don't know the full list until runtime, so we don't register specific consumables
	return true;
}

#pragma endregion

#pragma region UPCGExBlendOpMonolithicProviderSettings

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExBlendOpMonolithicProviderSettings::GetPreconfiguredInfo() const
{
	return {};
	// No preconfigured infos: they add too much noise to the node search.
}
#endif

void UPCGExBlendOpMonolithicProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExBlendingType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			BlendingDetails.DefaultBlending = static_cast<EPCGExBlendingType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

UPCGExFactoryData* UPCGExBlendOpMonolithicProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExBlendOpMonolithicFactory* NewFactory = InContext->ManagedObjects->New<UPCGExBlendOpMonolithicFactory>();
	NewFactory->Priority = Priority;
	NewFactory->BlendingDetails = BlendingDetails;

	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExBlendOpMonolithicProviderSettings::GetDisplayName() const
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExBlendingType>())
	{
		return FString::Printf(TEXT("Blend All (%s)"), *EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(BlendingDetails.DefaultBlending)).ToString());
	}

	return TEXT("PCGEx | BlendOp : Blend All");
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
