// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExTensorHandler.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Core/PCGExTensorOperation.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

PCGEX_SETTING_VALUE_IMPL(FPCGExTensorHandlerDetails, Size, double, SizeInput, SizeAttribute, SizeConstant)

namespace PCGExTensor
{
	FTensorsHandler::FTensorsHandler(const FPCGExTensorHandlerDetails& InConfig)
		: Config(InConfig)
	{
	}

	bool FTensorsHandler::Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExTensorFactoryData>>& InFactories, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		if (!InContext->GetWorkHandle().Pin())
		{
			// nuh-uh
			return false;
		}

		Tensors.Reserve(InFactories.Num());

		if (Config.bNormalize)
		{
			Size = Config.GetValueSettingSize();
			if (!Size->Init(InDataFacade))
			{
				return false;
			}
		}

		for (const UPCGExTensorFactoryData* Factory : InFactories)
		{
			TSharedPtr<PCGExTensorOperation> Op = Factory->CreateOperation(InContext);
			if (!Op->PrepareForData(InDataFacade))
			{
				continue;
			}
			Tensors.Add(Op);
		}

		if (Config.SamplerSettings.Sampler)
		{
			SamplerInstance = InContext->ManagedObjects->New<UPCGExTensorSampler>(GetTransientPackage(), Config.SamplerSettings.Sampler);
		}
		if (!SamplerInstance)
		{
			SamplerInstance = InContext->ManagedObjects->New<UPCGExTensorSampler>();
		}
		if (!SamplerInstance)
		{
			return false;
		}

		SamplerInstance->BindContext(InContext);
		SamplerInstance->PrimaryDataFacade = InDataFacade;

		// Fwd settings
		SamplerInstance->Radius = Config.SamplerSettings.Radius;
		SamplerInstance->MinStepFraction = Config.SamplerSettings.MinStepFraction;
		SamplerInstance->MaxStepFraction = Config.SamplerSettings.MaxStepFraction;
		SamplerInstance->ErrorTolerance = Config.SamplerSettings.ErrorTolerance;
		SamplerInstance->MaxSubSteps = Config.SamplerSettings.MaxSubSteps;

		return SamplerInstance->PrepareForData(InContext);
	}

	bool FTensorsHandler::Init(FPCGExContext* InContext, const FName InPin, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		TArray<TObjectPtr<const UPCGExTensorFactoryData>> InFactories;
		if (!PCGExFactories::GetInputFactories(InContext, InPin, InFactories, {FPCGExDataTypeInfoTensor::AsId()}))
		{
			return false;
		}

		if (InFactories.IsEmpty())
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing tensors."))
			return false;
		}

		return Init(InContext, InFactories, InDataFacade);
	}

	FTensorSample FTensorsHandler::Sample(const int32 InSeedIndex, const FTransform& InProbe, bool& OutSuccess) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FTensorsHandler::Sample);

		check(SamplerInstance)

		FTensorSample Result = SamplerInstance->Sample(Tensors, InSeedIndex, InProbe, OutSuccess);

		if (Config.bNormalize)
		{
			Result.DirectionAndSize = Result.DirectionAndSize.GetSafeNormal() * Size->Read(InSeedIndex);
		}

		if (Config.bInvert)
		{
			Result.DirectionAndSize *= -1;
			Result.Rotation = FQuat(-Result.Rotation.X, -Result.Rotation.Y, -Result.Rotation.Y, Result.Rotation.W);
		}

		Result.DirectionAndSize *= Config.UniformScale;

		return Result;
	}
}
