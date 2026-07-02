// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExActionFactoryProvider.h"


#include "PCGPin.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Types/PCGExAttributeIdentity.h"


#define LOCTEXT_NAMESPACE "PCGExWriteActions"
#define PCGEX_NAMESPACE PCGExWriteActions

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoAction, UPCGExActionFactoryData)

bool FPCGExActionOperation::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	PrimaryDataFacade = InPointDataFacade;

	FilterManager = MakeShared<PCGExPointFilter::FManager>(PrimaryDataFacade.ToSharedRef());

	if (!FilterManager->Init(InContext, Factory->FilterFactories))
	{
		return false;
	}

	return true;
}

void FPCGExActionOperation::ProcessPoint(const int32 Index)
{
	if (FilterManager->Test(Index))
	{
		OnMatchSuccess(Index);
	}
	else
	{
		OnMatchFail(Index);
	}
}

void FPCGExActionOperation::OnMatchSuccess(int32 Index)
{
}

void FPCGExActionOperation::OnMatchFail(int32 Index)
{
}

#if WITH_EDITOR
FString UPCGExActionProviderSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

TSharedPtr<FPCGExActionOperation> UPCGExActionFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(ActionOperation)
	NewOperation->Factory = const_cast<UPCGExActionFactoryData*>(this);
	return NewOperation;
}

bool UPCGExActionFactoryData::Boot(FPCGContext* InContext)
{
	return true;
}

bool UPCGExActionFactoryData::AppendAndValidate(const TSharedPtr<PCGExData::FAttributesInfos>& InInfos, FString& OutMessage) const
{
	TSet<FName> Mismatch;

	const FPCGExAttributeGatherDetails GatherDetails = FPCGExAttributeGatherDetails(); // Required for Append

	if (CheckSuccessInfos)
	{
		InInfos->Append(CheckSuccessInfos, GatherDetails, Mismatch);
	}
	if (!Mismatch.IsEmpty())
	{
		for (const FName& MismatchName : Mismatch)
		{
			OutMessage = FText::Format(FText::FromString(TEXT("Attribute \"{0}\" is referenced multiple times but has different types.")), FText::FromName(MismatchName)).ToString();
		}
		return false;
	}

	if (CheckFailInfos)
	{
		InInfos->Append(CheckFailInfos, GatherDetails, Mismatch);
	}
	if (!Mismatch.IsEmpty())
	{
		for (const FName& MismatchName : Mismatch)
		{
			OutMessage = FText::Format(FText::FromString(TEXT("Attribute \"{0}\" is referenced multiple times but has different types.")), FText::FromName(MismatchName)).ToString();
		}
		return false;
	}

	return true;
}

void UPCGExActionFactoryData::BeginDestroy()
{
	Super::BeginDestroy();
}

TArray<FPCGPinProperties> UPCGExActionProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (GetRequiresFilters())
	{
		PCGEX_PIN_FILTERS(PCGExActions::Labels::SourceConditionsFilterLabel, "Filters used to define if there's a match or not.", Required)
	}
	else
	{
		PCGEX_PIN_FILTERS(PCGExActions::Labels::SourceConditionsFilterLabel, "Filters used to define if there's a match or not.", Normal)
	}
	return PinProperties;
}

UPCGExFactoryData* UPCGExActionProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	Super::CreateFactory(InContext, InFactory);

	if (UPCGExActionFactoryData* TypedFactory = Cast<UPCGExActionFactoryData>(InFactory))
	{
		if (!PCGExFactories::GetInputFactories(InContext, PCGExActions::Labels::SourceConditionsFilterLabel, TypedFactory->FilterFactories, PCGExFactories::PointFilters()))
		{
			return nullptr;
		}

		TypedFactory->Priority = Priority;

		if (!TypedFactory->Boot(InContext))
		{
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}

	return InFactory;
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
