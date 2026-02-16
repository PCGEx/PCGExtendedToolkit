// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExValencyAssetStaging.h"

#include "PCGParamData.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "PCGExCollectionsCommon.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Containers/PCGExManagedObjects.h"

#define LOCTEXT_NAMESPACE "PCGExValencyAssetStaging"
#define PCGEX_NAMESPACE ValencyAssetStaging

PCGEX_INITIALIZE_ELEMENT(ValencyAssetStaging)
PCGEX_ELEMENT_BATCH_POINT_IMPL(ValencyAssetStaging)

#pragma region UPCGExValencyAssetStagingSettings

PCGExData::EIOInit UPCGExValencyAssetStagingSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

TArray<FPCGPinProperties> UPCGExValencyAssetStagingSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PARAM(PCGExValency::Labels::SourceValencyMapLabel, "Valency map from Solve or Generative nodes.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExValencyAssetStagingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::OutputCollectionMapLabel, "Collection map for downstream spawners", Required)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExValencyAssetStagingElement

bool FPCGExValencyAssetStagingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyAssetStaging)

	// Create and unpack Valency Map
	Context->ValencyUnpacker = MakeShared<PCGExValency::FValencyUnpacker>();
	Context->ValencyUnpacker->UnpackPin(InContext, PCGExValency::Labels::SourceValencyMapLabel);

	if (!Context->ValencyUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid Valency Map from the provided input."));
		return false;
	}

	// Build collection caches for all loaded BondingRules
	for (const auto& Pair : Context->ValencyUnpacker->GetBondingRules())
	{
		UPCGExValencyBondingRules* Rules = Pair.Value;
		if (!Rules) { continue; }

		if (UPCGExMeshCollection* MeshCol = Rules->GetMeshCollection()) { MeshCol->BuildCache(); }
		if (UPCGExActorCollection* ActorCol = Rules->GetActorCollection()) { ActorCol->BuildCache(); }
	}

	// Create pick packer for Collection Map output
	Context->PickPacker = MakeShared<PCGExCollections::FPickPacker>(InContext);

	return true;
}

bool FPCGExValencyAssetStagingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(ValencyAssetStaging)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	// Output Collection Map
	UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();
	Context->PickPacker->PackToDataset(ParamData);
	Context->StageOutput(ParamData, PCGExCollections::Labels::OutputCollectionMapLabel, PCGExData::EStaging::None);

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExValencyAssetStaging::FProcessor

namespace PCGExValencyAssetStaging
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExValencyAssetStaging::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		// Read ValencyEntry hashes
		const FName EntryAttrName = PCGExValency::EntryData::GetEntryAttributeName(Settings->EntrySuffix);
		ValencyEntryReader = PointDataFacade->GetReadable<int64>(EntryAttrName, PCGExData::EIOSide::In, true);
		if (!ValencyEntryReader)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(
				FTEXT("ValencyEntry attribute '{0}' not found. Run Valency : Solve first."),
				FText::FromName(EntryAttrName)));
			return false;
		}

		// Create Collection Entry writer
		CollectionEntryWriter = PointDataFacade->GetWritable<int64>(PCGExCollections::Labels::Tag_EntryIdx, 0, true, PCGExData::EBufferInit::Inherit);
		if (!CollectionEntryWriter) { return false; }

		// Allocate native properties for fitting output
		EPCGPointNativeProperties PointAllocations = EPCGPointNativeProperties::Transform;
		PointAllocations |= EPCGPointNativeProperties::BoundsMin;
		PointAllocations |= EPCGPointNativeProperties::BoundsMax;
		PointDataFacade->GetOut()->AllocateProperties(PointAllocations);

		// Initialize fitting handler
		FittingHandler.ScaleToFit = Settings->ScaleToFit;
		FittingHandler.Justification = Settings->Justification;
		if (!FittingHandler.Init(ExecutionContext, PointDataFacade)) { return false; }

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExValencyAssetStaging::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<FVector> OutBoundsMin = PointDataFacade->GetOut()->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> OutBoundsMax = PointDataFacade->GetOut()->GetBoundsMaxValueRange(false);
		TConstPCGValueRange<int32> InSeeds = PointDataFacade->GetIn()->GetConstSeedValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			const int64 RawRead = ValencyEntryReader->Read(Index);
			const uint64 ValencyHash = static_cast<uint64>(RawRead);
			if (ValencyHash == PCGExValency::EntryData::INVALID_ENTRY) { continue; }

			// Resolve ValencyEntry -> BondingRules + ModuleIndex
			uint16 ModuleIndex = 0;
			uint16 PatternFlags = 0;
			UPCGExValencyBondingRules* Rules = Context->ValencyUnpacker->ResolveEntry(ValencyHash, ModuleIndex, PatternFlags);

			if (!Rules || !Rules->IsCompiled()) { continue; }

			const FPCGExValencyBondingRulesCompiled* CompiledRules = Rules->GetCompiledData();
			if (ModuleIndex >= CompiledRules->ModuleCount) { continue; }

			const EPCGExValencyAssetType AssetType = CompiledRules->ModuleAssetTypes[ModuleIndex];

			// Resolve to collection entry
			const UPCGExAssetCollection* Collection = nullptr;
			FPCGExEntryAccessResult Result{};
			int32 EntryIndex = -1;
			int16 SecondaryIndex = -1;

			FTransform& OutTransform = OutTransforms[Index];

			if (AssetType == EPCGExValencyAssetType::Mesh)
			{
				Collection = Rules->GetMeshCollection();
				EntryIndex = Rules->GetMeshEntryIndex(ModuleIndex);
				if (Collection)
				{
					Result = Collection->GetEntryRaw(EntryIndex);
					if (Result.IsValid())
					{
						if (const PCGExAssetCollection::FMicroCache* MicroCache = Result.Entry->MicroCache.Get())
						{
							SecondaryIndex = MicroCache->GetPickRandomWeighted(InSeeds[Index]);
						}
					}
				}
			}
			else if (AssetType == EPCGExValencyAssetType::Actor)
			{
				Collection = Rules->GetActorCollection();
				EntryIndex = Rules->GetActorEntryIndex(ModuleIndex);
				if (Collection)
				{
					Result = Collection->GetEntryRaw(EntryIndex);
				}
			}

			if (Collection && Result.IsValid())
			{
				// Write Collection Entry hash via PickPacker
				const uint64 CollectionHash = Context->PickPacker->GetPickIdx(Collection, EntryIndex, SecondaryIndex);
				CollectionEntryWriter->SetValue(Index, static_cast<int64>(CollectionHash));

				// Apply fitting
				FBox OutBounds = Result.Entry->Staging.Bounds;
				FVector Translation = FVector::ZeroVector;
				FittingHandler.ComputeTransform(Index, OutTransform, OutBounds, Translation);
				OutBoundsMin[Index] = OutBounds.Min;
				OutBoundsMax[Index] = OutBounds.Max;
			}

			// Apply local transform if enabled (uses point seed to select among variants)
			if (Settings->bApplyLocalTransforms && CompiledRules->ModuleHasLocalTransform[ModuleIndex])
			{
				const FTransform LocalTransform = CompiledRules->GetModuleLocalTransform(ModuleIndex, InSeeds[Index]);
				OutTransform = LocalTransform * OutTransform;
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
