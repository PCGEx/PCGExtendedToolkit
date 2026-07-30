// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExFactoryProvider.h"

#include "PCGPin.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactoryData.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Tasks/Task.h"

#define LOCTEXT_NAMESPACE "PCGExFactoryProvider"
#define PCGEX_NAMESPACE PCGExFactoryProvider

TArray<FPCGPinProperties> UPCGExFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	{
#if PCGEX_ENGINE_VERSION > 506
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(GetMainOutputPin(), GetFactoryTypeId(), false, false);
#else
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(GetMainOutputPin(), EPCGDataType::Param, false, false);
#endif
		PCGEX_PIN_TOOLTIP(GetMainOutputPin().ToString())
		PCGEX_PIN_STATUS(Required)
	}

	return PinProperties;
}

FPCGElementPtr UPCGExFactoryProviderSettings::CreateElement() const
{
	return MakeShared<FPCGExFactoryProviderElement>();
}

#if WITH_EDITOR
FString UPCGExFactoryProviderSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

UPCGExFactoryData* UPCGExFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	InFactory->bCleanupConsumableAttributes = bCleanupConsumableAttributes;
	return InFactory;
}

bool FPCGExFactoryProviderElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFactoryProviderElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FactoryProvider)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->OutFactory = Settings->CreateFactory(Context, nullptr);

		if (!Context->OutFactory)
		{
			return true;
		}

		Context->OutFactory->OutputConfigToMetadata();

		{
			PCGEX_MAKE_SHARED(AssetDeps, TSet<FSoftObjectPath>)
			Context->OutFactory->RegisterAssetDependencies(*AssetDeps.Get());
			if (!AssetDeps->IsEmpty())
			{
				Context->OutFactory->AssetsDependencies = PCGExHelpers::LoadBlocking_AnyThread(AssetDeps);
			}
		}

		if (Context->OutFactory->WantsPreparation(Context))
		{
			Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);
			TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
			PCGEX_ASYNC_SCHEDULING_SCOPE(TaskManager, true)

			// Prepare()'s work can complete inline (loop dispatch is synchronous), so a completion callback may
			// already have written the real result -- its return value only seeds PrepResult, never overrides it.
			Context->OutFactory->PrepResult = PCGExFactories::EPreparationResult::None;
			const PCGExFactories::EPreparationResult SeedResult = Context->OutFactory->Prepare(Context, TaskManager);
			if (Context->OutFactory->PrepResult == PCGExFactories::EPreparationResult::None)
			{
				Context->OutFactory->PrepResult = SeedResult;
			}

			return false;
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		if (Context->OutFactory->PrepResult != PCGExFactories::EPreparationResult::Success)
		{
			if (Settings->ShouldCancel(Context, Context->OutFactory->PrepResult))
			{
				return Context->CancelExecution();
			}
		}
	}

	Context->Done();

	// Register declared dependencies to root them
	TArray<FPCGPinProperties> InputPins = Settings->InputPinProperties();
	for (const FPCGPinProperties& Pin : InputPins)
	{
		const TArray<FPCGTaggedData>& InputData = Context->InputData.GetInputsByPin(Pin.Label);
		for (const FPCGTaggedData& TaggedData : InputData)
		{
			Context->OutFactory->AddDataDependency(TaggedData.Data);
		}
	}

	Context->StageOutput(Context->OutFactory, Settings->GetMainOutputPin());

	return Context->TryComplete();
}

UPCGExFactoryProviderSettings::UPCGExFactoryProviderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CacheLoadedResources = EPCGExOptionState::Enabled;
}

void FPCGExFactoryProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	// Disabled factories should not output anything when disabled
	Context->OutputData.TaggedData.Empty();
}

#if PCGEX_ENGINE_VERSION > 506
const FPCGDataTypeBaseId& UPCGExFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGExFactoryDataTypeInfo::AsId();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
