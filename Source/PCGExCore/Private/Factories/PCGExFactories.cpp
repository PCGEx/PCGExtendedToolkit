// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExFactories.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Tasks/Task.h"

#define LOCTEXT_NAMESPACE "PCGExFactoryProvider"
#define PCGEX_NAMESPACE PCGExFactoryProvider

namespace PCGExFactories
{
	bool GetInputFactories_Internal(FPCGExContext* InContext, const FName InLabel, TArray<TObjectPtr<const UPCGExFactoryData>>& OutFactories, const TSet<FPCGDataTypeBaseId>& Types, const bool bRequired)
	{
		const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(InLabel);
		TSet<uint32> UniqueData;
		UniqueData.Reserve(Inputs.Num());

		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			bool bIsAlreadyInSet;
			UniqueData.Add(TaggedData.Data->GetUniqueID(), &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				continue;
			}

			const UPCGExFactoryData* Factory = Cast<UPCGExFactoryData>(TaggedData.Data);
			if (Factory)
			{
				if (!Types.Contains(Factory->GetDataTypeId()))
				{
					PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(FTEXT("Input '{0}' is not supported by pin {1}."), FText::FromString(TaggedData.Data->GetClass()->GetName()), FText::FromName(InLabel)))
					continue;
				}

				OutFactories.AddUnique(Factory);
				Factory->RegisterConsumableAttributes(InContext);
			}
			else
			{
				PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(FTEXT("Input '{0}' is not supported by pin {1}."), FText::FromString(TaggedData.Data->GetClass()->GetName()), FText::FromName(InLabel)))
			}
		}

		if (OutFactories.IsEmpty())
		{
			if (bRequired)
			{
				PCGEX_LOG_MISSING_INPUT(InContext, FText::Format(FTEXT("Missing required inputs on pin '{0}'."), FText::FromName(InLabel)))
			}
			return false;
		}

		OutFactories.Sort([](const UPCGExFactoryData& A, const UPCGExFactoryData& B)
		{
			return A.Priority < B.Priority;
		});

		return true;
	}

	void RegisterConsumableAttributesWithData_Internal(const TArray<TObjectPtr<const UPCGExFactoryData>>& InFactories, FPCGExContext* InContext, const UPCGData* InData)
	{
		check(InContext)

		if (!InData || InFactories.IsEmpty())
		{
			return;
		}

		for (const TObjectPtr<const UPCGExFactoryData>& Factory : InFactories)
		{
			if (!Factory.Get())
			{
				continue;
			}
			Factory->RegisterConsumableAttributesWithData(InContext, InData);
		}
	}

	void RegisterConsumableAttributesWithFacade_Internal(const TArray<TObjectPtr<const UPCGExFactoryData>>& InFactories, const TSharedPtr<PCGExData::FFacade>& InFacade)
	{
		FPCGContext::FSharedContext<FPCGExContext> SharedContext(InFacade->Source->GetContextHandle());
		check(SharedContext.Get())

		if (!InFacade->GetIn())
		{
			return;
		}

		const UPCGData* Data = InFacade->GetIn();

		if (!Data)
		{
			return;
		}

		for (const TObjectPtr<const UPCGExFactoryData>& Factory : InFactories)
		{
			Factory->RegisterConsumableAttributesWithData(SharedContext.Get(), Data);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
