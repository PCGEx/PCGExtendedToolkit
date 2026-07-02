// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Core/PCGExPointStates.h"

#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExFilterTypeSets.h"

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoPointState, UPCGExPointStateFactoryData)

TSharedPtr<PCGExPointFilter::IFilter> UPCGExPointStateFactoryData::CreateFilter() const
{
	PCGEX_MAKE_SHARED(NewState, PCGExPointStates::FState, this)
	NewState->BaseConfig = BaseConfig;
	return NewState;
}

void UPCGExPointStateFactoryData::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	PCGExPointFilter::RegisterBuffersDependencies(InContext, FilterFactories, FacadePreloader);
}

void UPCGExPointStateFactoryData::BeginDestroy()
{
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGExStateFactoryProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameOutputPin(FName("Flag"), PCGExPointStates::Labels::OutputStateLabel);
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

namespace PCGExPointStates
{
	FState::~FState()
	{
	}

	bool FState::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		if (!IFilter::Init(InContext, InPointDataFacade))
		{
			return false;
		}

		Manager = MakeShared<PCGExPointFilter::FManager>(InPointDataFacade.ToSharedRef());
		return true;
	}

	bool FState::InitInternalManager(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories)
	{
		return Manager->Init(InContext, InFactories);
	}

	bool FState::Test(const int32 Index) const
	{
		const bool bResult = Manager->Test(Index);
		return bResult;
	}

	void FState::ProcessFlags(const bool bSuccess, int64& InFlags) const
	{
		// TODO : Implement
	}

	FStateManager::FStateManager(const TSharedPtr<TArray<int64>>& InFlags, const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
		: FManager(InPointDataFacade)
	{
		FlagsCache = InFlags;
	}

	void FStateManager::PostInitFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter)
	{
		const TSharedPtr<FState> State = StaticCastSharedPtr<FState>(InFilter);
		State->InitInternalManager(InContext, State->StateFactory->FilterFactories);

		FManager::PostInitFilter(InContext, InFilter);

		States.Add(State);
	}

	bool FStateManager::Test(const int32 Index)
	{
		int64& Flags = (*FlagsCache)[Index];
		for (const TSharedPtr<FState>& State : States)
		{
			State->ProcessFlags(State->Test(Index), Flags);
		}
		return true;
	}
}

bool UPCGExStateFactoryProviderSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->IsOutputPin() && (InPin->Properties.Label == PCGExPointStates::Labels::OutputOnPassBitmaskLabel || InPin->Properties.Label == PCGExPointStates::Labels::OutputOnFailBitmaskLabel))
	{
		return bOutputBitmasks;
	}

	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExStateFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourceFiltersLabel, TEXT("Filters used to check whether this state is true or not. Accepts regular point filters & cluster filters."), Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExStateFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (CanOutputBitmasks())
	{
		PCGEX_PIN_PARAMS(PCGExPointStates::Labels::OutputOnPassBitmaskLabel, TEXT("On Pass Bitmask. Note that based on the selected operation, this value may not be useful."), Advanced)
		PCGEX_PIN_PARAMS(PCGExPointStates::Labels::OutputOnFailBitmaskLabel, TEXT("On Fail Bitmask. Note that based on the selected operation, this value may not be useful."), Advanced)
	}
	return PinProperties;
}

FName UPCGExStateFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGExPointStates::Labels::OutputStateLabel;
}

UPCGExFactoryData* UPCGExStateFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExPointStateFactoryData* NewFactory = Cast<UPCGExPointStateFactoryData>(InFactory);
	if (!NewFactory)
	{
		return NewFactory;
	}

	NewFactory->Priority = Priority;

	if (const bool bRequiresFilters = NewFactory->GetRequiresFilters();
		!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourceFiltersLabel, NewFactory->FilterFactories, PCGExFactories::ClusterNodeFilters(), bRequiresFilters) && bRequiresFilters)
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	if (CanOutputBitmasks() && bOutputBitmasks)
	{
		OutputBitmasks(InContext, NewFactory->BaseConfig);
	}

	Super::CreateFactory(InContext, InFactory);
	return InFactory;
}

#if WITH_EDITOR
FString UPCGExStateFactoryProviderSettings::GetDisplayName() const
{
	return Name.ToString();
}
#endif

TSet<FPCGDataTypeBaseId> UPCGExStateFactoryProviderSettings::GetInternalFilterTypes() const
{
	return PCGExFactories::PointFilters();
}

void UPCGExStateFactoryProviderSettings::OutputBitmasks(FPCGExContext* InContext, const FPCGExStateConfigBase& InConfig) const
{
	UPCGParamData* PassBitmask = InContext->ManagedObjects->New<UPCGParamData>();

	if (InConfig.bOnTestPass)
	{
		PassBitmask->Metadata->CreateAttribute<int64>(FName("OnPassBitmask"), InConfig.PassStateFlags.Get(), false, true);
	}
	else
	{
		PassBitmask->Metadata->CreateAttribute<int64>(FName("OnPassBitmask"), 0, false, true);
	}

	PassBitmask->Metadata->AddEntry();
	FPCGTaggedData& OutPassData = InContext->OutputData.TaggedData.Emplace_GetRef();
	OutPassData.Pin = PCGExPointStates::Labels::OutputOnPassBitmaskLabel;
	OutPassData.Data = PassBitmask;

	UPCGParamData* FailBitmask = InContext->ManagedObjects->New<UPCGParamData>();
	if (InConfig.bOnTestFail)
	{
		FailBitmask->Metadata->CreateAttribute<int64>(FName("OnFailBitmask"), InConfig.FailStateFlags.Get(), false, true);
	}
	else
	{
		FailBitmask->Metadata->CreateAttribute<int64>(FName("OnFailBitmask"), 0, false, true);
	}

	FailBitmask->Metadata->AddEntry();
	FPCGTaggedData& OutFailData = InContext->OutputData.TaggedData.Emplace_GetRef();
	OutFailData.Pin = PCGExPointStates::Labels::OutputOnFailBitmaskLabel;
	OutFailData.Data = FailBitmask;
}
