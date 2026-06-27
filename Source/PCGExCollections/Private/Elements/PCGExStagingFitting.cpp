// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingFitting.h"

#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Engine/StaticMesh.h"
#include "Helpers/PCGExAssetLoader.h"
#include "Helpers/PCGExRandomHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExStagingFittingElement"
#define PCGEX_NAMESPACE StagingFitting

#pragma region UPCGExStagingFittingSettings

bool UPCGExStagingFittingSettings::WantsDataStealing() const
{
	return Super::WantsDataStealing() && !bPruneEmptyPoints;
}

PCGExData::EIOInit UPCGExStagingFittingSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_INITIALIZE_ELEMENT(StagingFitting)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingFitting)

void UPCGExStagingFittingSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	if (Source == EPCGExFittingSource::CollectionMap)
	{
		PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	}

	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExStagingFittingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExStagingFittingElement

void FPCGExStagingFittingContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();
	if (StaticMeshLoader)
	{
		StaticMeshLoader->AddAssetDependencies();
	}
}

bool FPCGExStagingFittingElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	if (!Context)
	{
		return false;
	}
	
	return Context->CurrentPhase == EPCGExecutionPhase::PrepareData
		&& Context->GetInputSettings<UPCGExStagingFittingSettings>()->Source == EPCGExFittingSource::CollectionMap;
}

bool FPCGExStagingFittingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingFitting)

	if (Settings->Source == EPCGExFittingSource::CollectionMap)
	{
		Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
		Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

		if (!Context->CollectionPickUnpacker->HasValidMapping())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
			return false;
		}
	}
	else
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->AssetPathAttributeName)

		TArray<FName> Names = {Settings->AssetPathAttributeName};
		Context->StaticMeshLoader = MakeShared<PCGEx::TAssetLoader<UStaticMesh>>(Context, Context->MainPoints, Names);
		if (!Context->StaticMeshLoader->Discover())
		{
			return Context->CancelExecution(TEXT("Failed to find any assets to load."));
		}
	}

	return true;
}

void FPCGExStagingFittingElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(StagingFitting)
	if (Context->StaticMeshLoader)
	{
		Context->StaticMeshLoader->Finalize();
	}
}

bool FPCGExStagingFittingElement::PostBoot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::PostBoot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingFitting)
	if (Context->StaticMeshLoader && Context->StaticMeshLoader->IsEmpty())
	{
		return InContext->CancelExecution(TEXT("Failed to load any meshes from points."));
	}
	return true;
}

bool FPCGExStagingFittingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingFittingElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingFitting)
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
				NewBatch->bRequiresWriteStep = Settings->bPruneEmptyPoints;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExStagingFitting::FProcessor

namespace PCGExStagingFitting
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingFitting::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}


		if (Settings->Source == EPCGExFittingSource::CollectionMap)
		{
			PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

			EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
			if (!EntryHashGetter)
			{
				return false;
			}

			Variations = Settings->Variations;
			Variations.Init(Settings->Seed);
		}
		else
		{
			MeshKeys = Context->StaticMeshLoader->GetKeys(PointDataFacade->Source->IOIndex);

			// Missing attribute on processed source
			if (Settings->Source == EPCGExFittingSource::MeshAttribute && !MeshKeys)
			{
				PCGEX_INIT_IO(PointDataFacade->Source, Settings->bPruneEmptyPoints ? PCGExData::EIOInit::NoInit : PCGExData::EIOInit::Forward)
				return false;
			}
			else
			{
				PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())
			}


			// Build bounds cache from loaded meshes
			for (const auto& Pair : Context->StaticMeshLoader->AssetsMap)
			{
				if (const UStaticMesh* SM = Pair.Value)
				{
					BoundsCache.Add(Pair.Key, SM->GetBoundingBox());
				}
			}
		}

		FittingHandler.ScaleToFit = Settings->ScaleToFit;
		FittingHandler.Justification = Settings->Justification;

		if (!FittingHandler.Init(ExecutionContext, PointDataFacade))
		{
			return false;
		}

		if (Settings->bWriteTranslation)
		{
			TranslationWriter = PointDataFacade->GetWritable<FVector>(Settings->TranslationAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::Inherit);
		}

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		AllocateFor |= EPCGPointNativeProperties::BoundsMin;
		AllocateFor |= EPCGPointNativeProperties::BoundsMax;
		AllocateFor |= EPCGPointNativeProperties::Transform;

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		if (Settings->bPruneEmptyPoints)
		{
			Mask.Init(1, PointDataFacade->GetNum());
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingFitting::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		const TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange(false);
		const TPCGValueRange<FVector> OutBoundsMin = OutPointData->GetBoundsMinValueRange(false);
		const TPCGValueRange<FVector> OutBoundsMax = OutPointData->GetBoundsMaxValueRange(false);
		const TConstPCGValueRange<int32> Seeds = OutPointData->GetConstSeedValueRange();

		int32 LocalNumInvalid = 0;

		auto InvalidPoint = [&](const int32 Index)
		{
			if (Settings->bPruneEmptyPoints)
			{
				Mask[Index] = 0;
				LocalNumInvalid++;
			}
		};

		const bool bUseMeshAttribute = Settings->Source == EPCGExFittingSource::MeshAttribute;
		const bool bConsiderEntryScaleToFit = Settings->bConsiderEntryScaleToFit;
		const bool bConsiderEntryJustification = Settings->bConsiderEntryJustification;
		// Null in CollectionMap mode (never assigned, never indexed); the MeshAttribute null
		// case bails in Process(), so this is guaranteed valid wherever bUseMeshAttribute is set.
		const TArray<PCGExValueHash>* MeshKeysPtr = MeshKeys.Get();

		int16 MaterialPick = 0;
		FRandomStream RandomSource;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				InvalidPoint(Index);
				continue;
			}

			FTransform& OutTransform = OutTransforms[Index];
			FVector OutTranslation = FVector::ZeroVector;

			if (bUseMeshAttribute)
			{
				check(MeshKeysPtr);
				const FBox* CachedBounds = BoundsCache.Find((*MeshKeysPtr)[Index]);
				if (!CachedBounds || !CachedBounds->IsValid)
				{
					InvalidPoint(Index);
					continue;
				}

				FBox OutBounds = *CachedBounds;

				FittingHandler.ComputeTransform(Index, OutTransform, OutBounds, OutTranslation);

				if (TranslationWriter)
				{
					TranslationWriter->SetValue(Index, OutTranslation);
				}

				OutBoundsMin[Index] = OutBounds.Min;
				OutBoundsMax[Index] = OutBounds.Max;
			}
			else
			{
				const uint64 Hash = EntryHashGetter->Read(Index);

				FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);

				if (!Result.IsValid() || !Result.Entry->Staging.Bounds.IsValid)
				{
					InvalidPoint(Index);
					continue;
				}

				const FPCGExAssetCollectionEntry* Entry = Result.Entry;
				const UPCGExAssetCollection* EntryHost = Result.Host;

				FBox OutBounds = Entry->Staging.AlteredBounds;
				const FPCGExFittingVariations& EntryVariations = Entry->GetVariations(EntryHost);

				PCGExFitting::FOverridesView EntryOverrides;
				if (bConsiderEntryScaleToFit)
				{
					EntryOverrides.ScaleToFit = Entry->GetScaleToFitOverride(EntryHost);
				}
				if (bConsiderEntryJustification)
				{
					EntryOverrides.Justification = Entry->GetJustificationOverride(EntryHost);
				}

				RandomSource.Initialize(PCGExRandomHelpers::GetSeed(Seeds[Index], Variations.Seed));

				if (Variations.bEnabledBefore)
				{
					FTransform LocalXForm = FTransform::Identity;
					Variations.Apply(RandomSource, LocalXForm, EntryVariations, EPCGExVariationMode::Before);
					FittingHandler.ComputeLocalTransform(Index, LocalXForm, OutTransform, OutBounds, OutTranslation, EntryOverrides);
				}
				else
				{
					FittingHandler.ComputeTransform(Index, OutTransform, OutBounds, OutTranslation, true, EntryOverrides);
				}

				if (TranslationWriter)
				{
					TranslationWriter->SetValue(Index, OutTranslation);
				}

				OutBoundsMin[Index] = OutBounds.Min;
				OutBoundsMax[Index] = OutBounds.Max;

				if (Variations.bEnabledAfter)
				{
					Variations.Apply(RandomSource, OutTransform, EntryVariations, EPCGExVariationMode::After);
				}
			}
		}

		FPlatformAtomics::InterlockedAdd(&NumInvalid, LocalNumInvalid);
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteBuffers(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->Write();
			});
	}

	void FProcessor::Write()
	{
		if (Settings->bPruneEmptyPoints)
		{
			(void)PointDataFacade->Source->Gather(Mask);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
