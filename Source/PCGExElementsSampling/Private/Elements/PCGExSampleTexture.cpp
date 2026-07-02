// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleTexture.h"


#include "Core/PCGExTexCommon.h"
#include "Core/PCGExTexParamFactoryProvider.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExSampleTextureElement"
#define PCGEX_NAMESPACE SampleTexture

UPCGExSampleTextureSettings::UPCGExSampleTextureSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UVSource.Update("UVCoords");
}

TArray<FPCGPinProperties> UPCGExSampleTextureSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_TEXTURES(PCGExTexture::SourceTextureDataLabel, "Texture objects referenced by input points.", Required)
	PCGEX_PIN_FACTORIES(PCGExTexture::SourceTexLabel, "Texture params to extract from reference materials.", Required, FPCGExDataTypeInfoTexParam::AsId())
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SampleTexture)

PCGExData::EIOInit UPCGExSampleTextureSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleTexture)

bool FPCGExSampleTextureElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleTexture)

	if (!PCGExFactories::GetInputFactories(InContext, PCGExTexture::SourceTexLabel, Context->TexParamsFactories, {FPCGExDataTypeInfoTexParam::AsId()}))
	{
		return false;
	}

	TSet<FName> UniqueSampleNames;
	for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Context->TexParamsFactories)
	{
		PCGEX_VALIDATE_NAME_C(InContext, Factory->Config.TextureIDAttributeName)
		PCGEX_VALIDATE_NAME_C(InContext, Factory->Config.SampleAttributeName)

		bool bAlreadySet = false;
		if (Factory->Config.OutputType == EPCGExTexSampleAttributeType::Invalid)
		{
			PCGEX_LOG_INVALID_ATTR_C(InContext, "Sample Name (Texture Params)", Factory->Config.SampleAttributeName)
			continue;
		}

		UniqueSampleNames.Add(Factory->Config.SampleAttributeName, &bAlreadySet);
		if (bAlreadySet && !Settings->bQuietDuplicateSampleNamesWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Sample output attribute name \"{0}\" is used multiple times. If this is intended, you can quiet this warning in the settings."), FText::FromName(Factory->Config.SampleAttributeName)));
		}
	}

	Context->TextureMap = MakeShared<PCGExTexture::FLookup>();
	Context->TextureMap->BuildMapFrom(Context, PCGExTexture::SourceTextureDataLabel);

	return true;
}

bool FPCGExSampleTextureElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleTextureElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleTexture)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				if (Settings->bPruneFailedSamples)
				{
					NewBatch->bRequiresWriteStep = true;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to sample."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleTexture
{
	FSampler::FSampler(const FPCGExTextureParamConfig& InConfig, const TSharedPtr<PCGExTexture::FLookup>& InTextureMap, const TSharedRef<PCGExData::FFacade>& InDataFacade)
		: Config(InConfig)
		  , TextureMap(InTextureMap)
	{
		IDGetter = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
		bValid = IDGetter->Prepare(Config.TextureIDAttributeName, InDataFacade->Source);
		if (!bValid)
		{
			return;
		}
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleTexture::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		SamplingMask.Init(false, PointDataFacade->GetNum());

		UVGetter = PointDataFacade->GetBroadcaster<FVector2D>(Settings->UVSource, true);

		if (!UVGetter)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(Context, UV Attribute, Settings->UVSource)
			return false;
		}

		for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Context->TexParamsFactories)
		{
			if (Factory->Config.OutputType == EPCGExTexSampleAttributeType::Invalid)
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(Factory->Config.MetadataType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				TSharedPtr<TSampler<T>> Sampler = MakeShared<TSampler<T>>(Factory->Config, Context->TextureMap, PointDataFacade);
				if (!Sampler->IsValid())
				{
					PCGEX_LOG_INVALID_ATTR_C(Context, ID, Factory->Config.TextureIDAttributeName)
					return;
				}

				Samplers.Add(Sampler.ToSharedRef());
			});
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleTexture::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		bool bAnySuccessLocal = false;

		const UPCGBasePointData* InPoints = PointDataFacade->GetIn();

		PCGEX_SCOPE_LOOP(Index)
		{
			auto SamplingFailed = [&]()
			{
				SamplingMask[Index] = false;
			};

			if (!PointFilterCache[Index])
			{
				if (Settings->bProcessFilteredOutAsFails)
				{
					SamplingFailed();
				}
				continue;
			}

			bool bSuccess = false;

			const FVector2D UV = UVGetter->Read(Index);
			PCGExData::FConstPoint Point(InPoints, Index);

			for (const TSharedRef<FSampler>& Sampler : Samplers)
			{
				if (Sampler->Sample(Point, UV))
				{
					bSuccess = true;
				}
			}

			if (!bSuccess)
			{
				SamplingFailed();
				continue;
			}

			SamplingMask[Index] = bSuccess;
			bAnySuccessLocal = true;
		}

		if (bAnySuccessLocal)
		{
			FPlatformAtomics::InterlockedExchange(&bAnySuccess, 1);
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);

		if (Settings->bTagIfHasSuccesses && bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasSuccessesTag);
		}
		if (Settings->bTagIfHasNoSuccesses && !bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasNoSuccessesTag);
		}
	}

	void FProcessor::Write()
	{
		if (Settings->bPruneFailedSamples)
		{
			(void)PointDataFacade->Source->Gather(SamplingMask);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
