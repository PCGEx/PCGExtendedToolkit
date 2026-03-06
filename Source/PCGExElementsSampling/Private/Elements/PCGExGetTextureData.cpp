// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetTextureData.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PCGComponent.h"

#include "Data/PCGRenderTargetData.h"
#include "Data/PCGTextureData.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "TextureResource.h"
#include "PCGExSubSystem.h"
#include "Core/PCGExTexCommon.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Helpers/PCGExStreamingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGetTextureDataElement"
#define PCGEX_NAMESPACE GetTextureData

UPCGExGetTextureDataSettings::UPCGExGetTextureDataSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TArray<FPCGPinProperties> UPCGExGetTextureDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (SourceType == EPCGExGetTexturePathType::MaterialPath) { PCGEX_PIN_FACTORIES(PCGExTexture::SourceTexLabel, "Texture params to extract from reference materials.", Required, FPCGExDataTypeInfoTexParam::AsId()) }
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetTextureDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (SourceType == EPCGExGetTexturePathType::TexturePath || bBuildTextureData) { PCGEX_PIN_TEXTURES(PCGExTexture::OutputTextureDataLabel, "Texture data.", Required) }
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(GetTextureData)
PCGEX_ELEMENT_BATCH_POINT_IMPL(GetTextureData)

bool FPCGExGetTextureDataElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(GetTextureData)

	Context->Transform = Settings->Transform;

	AActor* OriginalActor = UPCGBlueprintHelpers::GetOriginalComponent(*Context)->GetOwner();

	if (!Settings->bUseAbsoluteTransform)
	{
		FTransform OriginalActorTransform = OriginalActor->GetTransform();
		Context->Transform = Context->Transform * OriginalActorTransform;

		FBox OriginalActorLocalBounds = PCGHelpers::GetActorLocalBounds(OriginalActor);
		Context->Transform.SetScale3D(Context->Transform.GetScale3D() * 0.5 * (OriginalActorLocalBounds.Max - OriginalActorLocalBounds.Min));
	}

	if (Settings->SourceType == EPCGExGetTexturePathType::MaterialPath)
	{
		if (!PCGExFactories::GetInputFactories(InContext, PCGExTexture::SourceTexLabel, Context->TexParamsFactories, {PCGExFactories::EType::TexParam}))
		{
			return false;
		}

		if (Settings->bOutputTextureIds)
		{
			for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Context->TexParamsFactories) { PCGEX_VALIDATE_NAME_C(InContext, Factory->Config.TextureIDAttributeName) }
		}
	}

	Context->AddConsumableAttributeName(Settings->SourceAttributeName);

	return true;
}

bool FPCGExGetTextureDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetTextureDataElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(GetTextureData)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to sample."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_AsyncPreparation)

	PCGEX_ON_STATE(PCGExCommon::States::State_AsyncPreparation)
	{
		Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);
		if (!Context->TextureReferences.IsEmpty())
		{
			// Start loading textures...
			TSharedPtr<TSet<FSoftObjectPath>> Paths = MakeShared<TSet<FSoftObjectPath>>();
			for (const PCGExTexture::FReference& Ref : Context->TextureReferences) { Paths->Add(Ref.TexturePath); }
			PCGExHelpers::LoadBlocking_AnyThread(Paths, Context);

			Context->TextureReferencesList = Context->TextureReferences.Array();
			Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);

			Context->TextureReady.Init(false, Context->TextureReferencesList.Num());
			Context->TextureDataList.Init(nullptr, Context->TextureReferencesList.Num());

			Context->TextureProcessingToken = Context->GetTaskManager()->TryCreateToken(FName("TextureProcessing"));
			if (!Context->TextureProcessingToken.IsValid()) { return true; }

			PCGExMT::ExecuteOnMainThread(Context->GetTaskManager(), [CtxHandle = Context->GetOrCreateHandle()]()
			{
				PCGEX_SHARED_TCONTEXT_VOID(GetTextureData, CtxHandle)
				SharedContext.Get()->AdvanceProcessing(0);
			});
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		Context->Done();
		Context->MainPoints->StageOutputs();
	}

	return Context->TryComplete();
}

void FPCGExGetTextureDataContext::AdvanceProcessing(const int32 Index)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCreateTextureTask::ExecuteTask);

	if (!TextureProcessingToken.IsValid()) { return; }
	if (!TextureReferencesList.IsValidIndex(Index))
	{
		PCGEX_ASYNC_RELEASE_TOKEN(TextureProcessingToken)
		return;
	}

	PCGEX_SETTINGS_LOCAL(GetTextureData)

	auto MoveToNextTask = [&]()
	{
		PCGEX_SUBSYSTEM
		PCGExSubsystem->RegisterBeginTickAction([CtxHandle = GetOrCreateHandle(), Idx = Index + 1]()
		{
			const FSharedContext<FPCGExGetTextureDataContext> SharedContext(CtxHandle);
			if (FPCGExGetTextureDataContext* Ctx = SharedContext.Get())
			{
				Ctx->AdvanceProcessing(Idx);
			}
		});
	};

	auto ApplySettings = [&](UPCGTexture2DSingleBaseData* InTex)
	{
		InTex->Filter = Settings->Filter == EPCGExTextureFilter::Bilinear ? EPCGTextureFilter::Bilinear : EPCGTextureFilter::Point;

		InTex->ColorChannel = Settings->ColorChannel;
		InTex->TexelSize = Settings->TexelSize;
		InTex->Rotation = Settings->Rotation;
		InTex->bUseAdvancedTiling = Settings->bUseAdvancedTiling;
		InTex->Tiling = Settings->Tiling;
		InTex->CenterOffset = Settings->CenterOffset;
		InTex->bUseTileBounds = Settings->bUseTileBounds;
		InTex->TileBounds = Settings->TileBounds;
	};

	PCGExTexture::FReference Ref = TextureReferencesList[Index];
	TSoftObjectPtr<UTexture> Texture = TSoftObjectPtr<UTexture>(Ref.TexturePath);

	if (!Texture.Get()) { return; }

	TObjectPtr<UPCGTextureData> TexData = TextureDataList[Index];

	if (!TexData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FCreateTextureTask::CreateTexture);

		EDITOR_TrackPath(Texture.Get());

#pragma region RenderTarget

		if (TObjectPtr<UTextureRenderTarget2D> RT = Cast<UTextureRenderTarget2D>(Texture.Get()))
		{
			TObjectPtr<UPCGRenderTargetData> RTData = ManagedObjects->New<UPCGRenderTargetData>();

			ApplySettings(RTData);
			RTData->Initialize(RT, Transform);

			StageOutput(RTData, PCGExTexture::OutputTextureDataLabel, PCGExData::EStaging::None, {Ref.GetTag()});


			MoveToNextTask();
			return;
		}

#pragma endregion

#pragma region Regular Texture

		TexData = ManagedObjects->New<UPCGTextureData>();
		TextureDataList[Index] = TexData;

		TextureReady[Index] = TexData->Initialize(Texture.Get(), Ref.TextureIndex, Transform);
	}
	else
	{
		TextureReady[Index] = TexData->Initialize(Texture.Get(), Ref.TextureIndex, Transform);
	}

	if (!TextureReady[Index])
	{
		PCGEX_SUBSYSTEM
		PCGExSubsystem->RegisterBeginTickAction([CtxHandle = GetOrCreateHandle(), Idx = Index]()
		{
			const FSharedContext<FPCGExGetTextureDataContext> SharedContext(CtxHandle);
			if (FPCGExGetTextureDataContext* Ctx = SharedContext.Get())
			{
				Ctx->AdvanceProcessing(Idx);
			}
		});

		return;
	}

	if (!TexData->IsSuccessfullyInitialized())
	{
		MoveToNextTask();
		return;
	}

	if (!TexData->IsValid())
	{
		MoveToNextTask();
		return;
	}

	StageOutput(TexData, PCGExTexture::OutputTextureDataLabel, PCGExData::EStaging::None, {Ref.GetTag()});

	MoveToNextTask();

#pragma endregion
}

namespace PCGExGetTextureData
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetTextureData::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->bCleanupConsumableAttributes ? PCGExData::EIOInit::Duplicate : PCGExData::EIOInit::Forward)

		if (Settings->SourceType == EPCGExGetTexturePathType::MaterialPath)
		{
			MaterialReferences = MakeShared<TSet<FSoftObjectPath>>();

			// So texture params are registered last, otherwise they're first in the list and it's confusing
			TexParamLookup = MakeShared<PCGExTexture::FLookup>();
			if (!TexParamLookup->BuildFrom(Context->TexParamsFactories))
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("There was an unknown error when processing texture parameters."));
				return false;
			}

			if (Settings->bOutputTextureIds)
			{
				TexParamLookup->PrepareForWrite(Context, PointDataFacade);
			}
		}

		PathGetter = PointDataFacade->GetBroadcaster<FSoftObjectPath>(Settings->SourceAttributeName, true);

		if (!PathGetter)
		{
			PCGEX_LOG_INVALID_ATTR_C(Context, Asset Path, Settings->SourceAttributeName)
			return false;
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::GetTextureData::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			FSoftObjectPath AssetPath = PathGetter->Read(Index);

			if (Settings->SourceType == EPCGExGetTexturePathType::MaterialPath)
			{
				{
					FReadScopeLock ReadScopeLock(ReferenceLock);
					if (MaterialReferences->Contains(AssetPath)) { continue; }
				}
				{
					FWriteScopeLock WriteScopeLock(ReferenceLock);
					bool bAlreadySet = false;
					MaterialReferences->Add(AssetPath, &bAlreadySet);
					if (!bAlreadySet) { Context->EDITOR_TrackPath(AssetPath); }
				}
				continue;
			}

			PCGExTexture::FReference Ref = PCGExTexture::FReference(AssetPath);

			// See if the path breaks down as a path:index textureArray2D path
			int32 LastColonIndex;
			if (const FString Str = AssetPath.ToString(); Str.FindLastChar(TEXT(':'), LastColonIndex))
			{
				const FString PotentialIndex = Str.Mid(LastColonIndex + 1);
				const FString Prefix = Str.Left(LastColonIndex);

				// Check if the part after the last ":" is numeric
				if (PotentialIndex.IsNumeric())
				{
					int32 N = FCString::Atoi(*PotentialIndex);
					if (N >= 0 && N < 64)
					{
						// TextureArray2D don't support more entries, so if it's greater it's not that.
						// This is a very weak check :(
						Ref.TexturePath = Prefix;
						Ref.TextureIndex = FCString::Atoi(*PotentialIndex);
					}
				}
			}

			{
				FReadScopeLock ReadScopeLock(ReferenceLock);
				if (TextureReferences.Contains(Ref)) { continue; }
			}
			{
				FWriteScopeLock WriteScopeLock(ReferenceLock);
				TextureReferences.Add(Ref);
			}
		}
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedTextureReferences.Reserve(Loops.Num());
		for (int i = 0; i < Loops.Num(); i++)
		{
			TSharedPtr<TSet<PCGExTexture::FReference>> ScopedSet = MakeShared<TSet<PCGExTexture::FReference>>();
			ScopedTextureReferences.Add(ScopedSet);
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			TexParamLookup->ExtractParamsAndReferences(Index, TSoftObjectPtr<UMaterialInterface>(PathGetter->Read(Index)).Get(), *ScopedTextureReferences[Scope.LoopIndex].Get());
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		FWriteScopeLock WriteScopeLock(Context->ReferenceLock);
		for (const TSharedPtr<TSet<PCGExTexture::FReference>>& Set : ScopedTextureReferences)
		{
			Context->TextureReferences.Append(*Set.Get());
		}

		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->SourceType == EPCGExGetTexturePathType::MaterialPath)
		{
			// Load materials on the main thread x_x
			PCGExHelpers::LoadBlocking_AnyThread(MaterialReferences, Context);

			if (Settings->bOutputTextureIds)
			{
				StartParallelLoopForRange(PointDataFacade->GetNum());
				return;
			}

			for (const FSoftObjectPath& Path : *MaterialReferences.Get())
			{
				UMaterialInterface* Material = TSoftObjectPtr<UMaterialInterface>(Path).Get();
				if (!Material) { continue; }
				TexParamLookup->ExtractReferences(Material, TextureReferences);
			}
		}

		FWriteScopeLock WriteScopeLock(Context->ReferenceLock);
		Context->TextureReferences.Append(TextureReferences);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
