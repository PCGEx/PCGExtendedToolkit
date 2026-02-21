// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadPCGData.h"

#include "PCGDataAsset.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSurfaceData.h"
#include "Data/PCGVolumeData.h"
#include "Data/PCGLandscapeData.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Data/PCGExDataTags.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Helpers/PCGExRandomHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPCGDataAssetLoaderElement"
#define PCGEX_NAMESPACE PCGDataAssetLoader

#pragma region FPCGExSharedAssetPool

FPCGExSharedAssetPool::~FPCGExSharedAssetPool()
{
	PCGExHelpers::SafeReleaseHandle(LoadHandle);
}

void FPCGExSharedAssetPool::RegisterEntry(uint64 EntryHash, const FPCGExPCGDataAssetCollectionEntry* Entry)
{
	if (!Entry || Entry->bIsSubCollection || EntryHash == 0)
	{
		return;
	}

	FWriteScopeLock WriteLock(PoolLock);
	if (!EntryMap.Contains(EntryHash))
	{
		EntryMap.Add(EntryHash, Entry);
	}
}

void FPCGExSharedAssetPool::LoadAllAssets(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FOnLoadEnd&& OnLoadEnd)
{
	if (EntryMap.IsEmpty())
	{
		OnLoadEnd(false);
		return;
	}

	// Collect unique paths from all entries
	TSharedPtr<TSet<FSoftObjectPath>> PathsToLoad = MakeShared<TSet<FSoftObjectPath>>();
	for (const auto& Pair : EntryMap)
	{
		if (Pair.Value && Pair.Value->Staging.Path.IsValid())
		{
			PathsToLoad->Add(Pair.Value->Staging.Path);
		}
	}

	if (PathsToLoad->IsEmpty())
	{
		OnLoadEnd(false);
		return;
	}

	PCGExHelpers::Load(
		TaskManager,
		[PathsToLoad]() { return PathsToLoad->Array(); },
		[PCGEX_ASYNC_THIS_CAPTURE, OnLoadEnd](const bool bSuccess, TSharedPtr<FStreamableHandle> StreamableHandle)
		{
			PCGEX_ASYNC_THIS
			This->LoadHandle = StreamableHandle;

			if (bSuccess)
			{
				// Map loaded assets back to entries
				for (const auto& Pair : This->EntryMap)
				{
					if (Pair.Value && Pair.Value->Staging.Path.IsValid())
					{
						TSoftObjectPtr<UPCGDataAsset> SoftPtr(Pair.Value->Staging.Path);
						if (UPCGDataAsset* LoadedAsset = SoftPtr.Get())
						{
							This->LoadedAssets.Add(Pair.Value, LoadedAsset);
						}
					}
				}
			}

			OnLoadEnd(bSuccess);
		});
}

UPCGDataAsset* FPCGExSharedAssetPool::GetAsset(uint64 EntryHash) const
{
	FReadScopeLock ReadLock(PoolLock);

	const FPCGExPCGDataAssetCollectionEntry* const* EntryPtr = EntryMap.Find(EntryHash);
	if (!EntryPtr || !*EntryPtr)
	{
		return nullptr;
	}

	return GetAsset(*EntryPtr);
}

UPCGDataAsset* FPCGExSharedAssetPool::GetAsset(const FPCGExPCGDataAssetCollectionEntry* Entry) const
{
	const TObjectPtr<UPCGDataAsset>* Found = LoadedAssets.Find(Entry);
	return Found ? Found->Get() : nullptr;
}

bool FPCGExSharedAssetPool::HasEntries() const
{
	FReadScopeLock ReadLock(PoolLock);
	return !EntryMap.IsEmpty();
}

int32 FPCGExSharedAssetPool::GetNumEntries() const
{
	FReadScopeLock ReadLock(PoolLock);
	return EntryMap.Num();
}

#pragma endregion

#pragma region FPCGExSpatialDataTransformer

namespace PCGExPCGDataAssetLoader
{
	FSpatialTransformResult::FSpatialTransformResult(ETransformResult InResult)
		: Result(InResult)
	{
	}


	FSpatialTransformResult::FSpatialTransformResult(const TSharedPtr<PCGExMT::FTask>& InTask)
		: Result(ETransformResult::Success), Task(InTask)
	{
	}

	class FTransformTask : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformTask)

		FTransformTask(const FTransform& InTransform)
			: FTask(), Transform(InTransform)
		{
		}

		const FTransform& Transform;
	};

	class FTransformPoints final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformPoints)

		FTransformPoints(const FTransform& InTransform, UPCGBasePointData* InData)
			: FTransformTask(InTransform), Data(InData)
		{
		}

		UPCGBasePointData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TPCGValueRange<FTransform> OutTransforms = Data->GetTransformValueRange();
			
			PCGEX_PARALLEL_FOR(OutTransforms.Num(), OutTransforms[i] *= Transform; )
			
			const auto* Settings = TaskManager->GetContext()->GetInputSettings<UPCGExPCGDataAssetLoaderSettings>();
			if (Settings->bRefreshSeeds)
			{
				TPCGValueRange<int32> OutSeeds = Data->GetSeedValueRange(true);
				PCGEX_PARALLEL_FOR(OutSeeds.Num(), OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[i].GetLocation()); )
			}
		}
	};

	class FTransformSpline final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformSpline)

		FTransformSpline(const FTransform& InTransform, UPCGSplineData* InData)
			: FTransformTask(InTransform), Data(InData)
		{
		}

		UPCGSplineData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			// Copy keys
			TArray<FInterpCurvePoint<FVector>>& Scales = const_cast<FInterpCurveVector&>(Data->SplineStruct.GetSplinePointsScale()).Points;
			TArray<FInterpCurvePoint<FQuat>>& Rotations = const_cast<FInterpCurveQuat&>(Data->SplineStruct.GetSplinePointsRotation()).Points;
			TArray<FInterpCurvePoint<FVector>>& Positions = const_cast<FInterpCurveVector&>(Data->SplineStruct.GetSplinePointsPosition()).Points;

			FVector OutScale = Transform.GetScale3D();
			for (FInterpCurvePoint<FVector>& Scale : Scales)
			{
				Scale.ArriveTangent = Transform.TransformVector(Scale.ArriveTangent);
				Scale.LeaveTangent = Transform.TransformVector(Scale.LeaveTangent);
				Scale.OutVal *= OutScale;
			}

			for (FInterpCurvePoint<FQuat>& Rotation : Rotations)
			{
				Rotation.ArriveTangent = Transform.TransformRotation(Rotation.ArriveTangent);
				Rotation.LeaveTangent = Transform.TransformRotation(Rotation.LeaveTangent);
				Rotation.OutVal = Transform.TransformRotation(Rotation.OutVal);
			}

			for (FInterpCurvePoint<FVector>& Position : Positions)
			{
				Position.ArriveTangent = Transform.TransformVector(Position.ArriveTangent);
				Position.LeaveTangent = Transform.TransformVector(Position.LeaveTangent);
				Position.OutVal = Transform.TransformPosition(Position.OutVal);
			}
		}
	};

	class FTransformPolyline final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformPolyline)

		FTransformPolyline(const FTransform& InTransform, UPCGPolyLineData* InData)
			: FTransformTask(InTransform), Data(InData)
		{
		}

		UPCGPolyLineData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
		}
	};

	class FTransformVolume final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformVolume)

		FTransformVolume(const FTransform& InTransform, UPCGVolumeData* InData)
			: FTransformTask(InTransform), Data(InData)
		{
		}

		UPCGVolumeData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			Data->Initialize(Data->GetStrictBounds().TransformBy(Transform));
		}
	};

	FSpatialTransformResult PrepareTransformTask(UPCGSpatialData* InData, const FTransform& InTransform, const bool bOmitIfEmpty)
	{
		if (!InData) { return FSpatialTransformResult(); }

		if (UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			if (bOmitIfEmpty && PointData->IsEmpty()){ return FSpatialTransformResult(); }
			return FSpatialTransformResult(MakeShared<FTransformPoints>(InTransform, PointData));
		}

		if (UPCGSplineData* SplineData = Cast<UPCGSplineData>(InData))
		{
			if (bOmitIfEmpty && !SplineData->GetNumSegments()){ return FSpatialTransformResult(); }
			return FSpatialTransformResult(MakeShared<FTransformSpline>(InTransform, SplineData));
		}

		if (UPCGPolyLineData* PolyLineData = Cast<UPCGPolyLineData>(InData))
		{
			if (bOmitIfEmpty && !PolyLineData->GetNumSegments()){ return FSpatialTransformResult(); }
			return FSpatialTransformResult(MakeShared<FTransformPolyline>(InTransform, PolyLineData));
		}

		if (UPCGPrimitiveData* PrimitiveData = Cast<UPCGPrimitiveData>(InData))
		{
			return FSpatialTransformResult(ETransformResult::Unsupported);
		}

		if (UPCGSurfaceData* SurfaceData = Cast<UPCGSurfaceData>(InData))
		{
			return FSpatialTransformResult(ETransformResult::Unsupported);
		}

		if (UPCGVolumeData* VolumeData = Cast<UPCGVolumeData>(InData))
		{
			return FSpatialTransformResult(MakeShared<FTransformVolume>(InTransform, VolumeData));
		}

		if (UPCGLandscapeData* LandscapeData = Cast<UPCGLandscapeData>(InData))
		{
			return FSpatialTransformResult(ETransformResult::Unsupported);
		}

		return FSpatialTransformResult(ETransformResult::Unsupported);
	}
}
#pragma endregion

#pragma region FPCGExPCGDataAssetLoaderContext

void FPCGExPCGDataAssetLoaderContext::RegisterOutput(const FPCGTaggedData& InTaggedData, bool bAddPinTag, const int32 InIndex)
{
	if (!InTaggedData.Data) { return; }

	FName TargetPin = PCGExPCGDataAssetLoader::OutputPinDefault;

	// Check if we have a custom pin that matches
	if (CustomPinNames.Contains(InTaggedData.Pin)) { TargetPin = InTaggedData.Pin; }

	FPCGTaggedData LocalOutputData = InTaggedData;

	// Only add Pin: tag for data going to default "Out" pin
	if (bAddPinTag && TargetPin == PCGExPCGDataAssetLoader::OutputPinDefault && !InTaggedData.Pin.IsNone())
	{
		LocalOutputData.Tags.Add(FString::Printf(TEXT("Pin:%s"), *InTaggedData.Pin.ToString()));
	}

	LocalOutputData.Pin = TargetPin;

	{
		FWriteScopeLock WriteLock(OutputLock);
		OutputByPin.FindOrAdd(TargetPin).Add(LocalOutputData);
		OutputIndices.Add(InTaggedData.Data->GetUniqueID(), InIndex);
	}
}

void FPCGExPCGDataAssetLoaderContext::RegisterNonSpatialData(const FPCGTaggedData& InTaggedData, const int32 InIndex)
{
	if (!InTaggedData.Data) { return; }

	const uint32 UID = InTaggedData.Data->GetUniqueID();

	{
		FReadScopeLock ReadLock(NonSpatialLock);
		if (UniqueNonSpatialUIDs.Contains(UID)) { return; }
	}

	{
		FWriteScopeLock WriteLock(NonSpatialLock);

		bool bAlreadyInSet = false;
		UniqueNonSpatialUIDs.Add(UID, &bAlreadyInSet);
		if (bAlreadyInSet) { return; }

		// Non-spatial goes to appropriate pin, with Pin: tag if going to default
		RegisterOutput(InTaggedData, true, InIndex * -1);
	}
}

#pragma endregion

#pragma region UPCGSettings

void UPCGExPCGDataAssetLoaderSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExPCGDataAssetLoaderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainOutputPin(), "Loaded data. Spatial data is one per input point, other is single instance only.", Normal)

	// Add custom output pins first
	for (const FPCGPinProperties& CustomPin : CustomOutputPins)
	{
		if (!CustomPin.Label.IsNone())
		{
			PinProperties.Add(CustomPin);
		}
	}

	// Default fallback pin for unmatched data
	PCGEX_PIN_ANY(PCGExPCGDataAssetLoader::OutputPinDefault, "Default output for data that doesn't match custom pins. Tagged with Pin:OriginalPinName.", Normal)

	if (bMergeEmbeddedCollectionMaps)
	{
		PCGEX_PIN_PARAMS(PCGExCollections::Labels::OutputCollectionMapLabel, "Merged collection map from embedded data assets.", Normal)
	}

	return PinProperties;
}

#pragma endregion

PCGEX_INITIALIZE_ELEMENT(PCGDataAssetLoader)
PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(PCGDataAssetLoader)

bool FPCGExPCGDataAssetLoaderElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PCGDataAssetLoader)

	// Setup collection unpacker
	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	// Setup shared asset pool
	Context->SharedAssetPool = MakeShared<FPCGExSharedAssetPool>();
	Context->CustomPinNames.Reserve(Settings->CustomOutputPins.Num());

	// Build custom pin name set for fast lookup
	for (const FPCGPinProperties& CustomPin : Settings->CustomOutputPins)
	{
		if (!CustomPin.Label.IsNone())
		{
			Context->CustomPinNames.Add(CustomPin.Label);
		}
	}

	return true;
}

bool FPCGExPCGDataAssetLoaderElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPCGDataAssetLoaderElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PCGDataAssetLoader)
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

	// Stage outputs from all pins
	for (auto& Pair : Context->OutputByPin)
	{
		Pair.Value.Sort([&](const FPCGTaggedData& A, const FPCGTaggedData& B) { return Context->OutputIndices[A.Data->GetUniqueID()] < Context->OutputIndices[A.Data->GetUniqueID()]; });
		Context->OutputData.TaggedData.Append(Pair.Value);
	}

	// Mark unused pins as inactive
	int32 PinIndex = 0;
	
	// Check default pin
	if (!Context->OutputByPin.Contains(PCGExPCGDataAssetLoader::OutputPinDefault) ||
		Context->OutputByPin[PCGExPCGDataAssetLoader::OutputPinDefault].IsEmpty())
	{
		Context->OutputData.InactiveOutputPinBitmask |= (1ULL << 0);
	}
	
	PinIndex++;
	
	for (const FPCGPinProperties& CustomPin : Settings->CustomOutputPins)
	{
		if (!CustomPin.Label.IsNone())
		{
			if (!Context->OutputByPin.Contains(CustomPin.Label) || Context->OutputByPin[CustomPin.Label].IsEmpty())
			{
				Context->OutputData.InactiveOutputPinBitmask |= (1ULL << PinIndex);
			}
			PinIndex++;
		}
	}

	// Map pin (when bMergeEmbeddedCollectionMaps is enabled)
	if (Settings->bMergeEmbeddedCollectionMaps)
	{
		if (!Context->MergedMapPacker)
		{
			Context->OutputData.InactiveOutputPinBitmask |= (1ULL << PinIndex);
		}
		PinIndex++;
	}

	return Context->TryComplete();
}

namespace PCGExPCGDataAssetLoader
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPCGDataAssetLoader::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::NoInit)

		// Get hash attribute
		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Missing staging hash attribute. Make sure points were staged with Collection Map output."));
			return false;
		}

		// Setup forward handler if needed
		if (Settings->TargetsForwarding.bEnabled)
		{
			ForwardHandler = Settings->TargetsForwarding.GetHandler(PointDataFacade);
		}

		// Initialize per-point hash storage
		const int32 NumPoints = PointDataFacade->GetNum();
		PointEntryHashes.SetNumZeroed(NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::PCGDataAssetLoader::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		// Collect entry hashes and register to shared pool - no loading here (parallel safe)
		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			const int64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == -1) { continue; }

			int16 SecondaryIndex = 0;
			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, SecondaryIndex);

			if (!Result.IsValid()) { continue; }

			// Check if this is a PCGDataAsset entry
			if (!Result.Entry->IsType(PCGExAssetCollection::TypeIds::PCGDataAsset)) { continue; }

			const FPCGExPCGDataAssetCollectionEntry* PCGDataEntry = static_cast<const FPCGExPCGDataAssetCollectionEntry*>(Result.Entry);

			// Store hash for this point
			PointEntryHashes[Index] = Hash;

			// Register to shared pool (thread-safe, deduplicates by hash)
			Context->SharedAssetPool->RegisterEntry(Hash, PCGDataEntry);
		}
	}

	bool FProcessor::PassesTagFilter(const FPCGTaggedData& InTaggedData) const
	{
		if (!Settings->bFilterByTags)
		{
			return true;
		}

		// Check exclude tags first
		for (const FString& ExcludeTag : Settings->ExcludeTags)
		{
			if (InTaggedData.Tags.Contains(ExcludeTag))
			{
				return false;
			}
		}

		// Check include tags (if specified)
		if (!Settings->IncludeTags.IsEmpty())
		{
			for (const FString& IncludeTag : Settings->IncludeTags)
			{
				if (InTaggedData.Tags.Contains(IncludeTag))
				{
					return true;
				}
			}
			return false;
		}

		return true;
	}

	FSpatialTransformResult FProcessor::ProcessTaggedData(int32 PointIndex, const FTransform& TargetTransform, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper)
	{
		UPCGData* Data = const_cast<UPCGData*>(InTaggedData.Data.Get());
		if (!Data) { return FSpatialTransformResult(); }

		const int32 OutIdx = BatchIndex * 1000000 + PointIndex;

		// Check if this is spatial data
		UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Data);

		if (!SpatialData)
		{
			// Non-spatial data: register once per unique asset (not per point)
			Context->RegisterNonSpatialData(InTaggedData, OutIdx);
			return FSpatialTransformResult();
		}

		// Spatial data: duplicate and transform for this point
		UPCGSpatialData* DuplicatedData = Context->ManagedObjects->DuplicateData<UPCGSpatialData>(SpatialData);

		if (!DuplicatedData)
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Failed to duplicate spatial data of type {0}"), FText::FromString(Data->GetClass()->GetName())));
			}
			return FSpatialTransformResult();
		}

		// Apply transform
		FSpatialTransformResult TransformResult = PrepareTransformTask(DuplicatedData, TargetTransform, Settings->bOmitEmptyData);

		if (TransformResult.Result == ETransformResult::Unsupported)
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Spatial data type {0} does not support transformation. Data will be output untransformed."), FText::FromString(Data->GetClass()->GetName())));
			}
		}
		else if (TransformResult.Result == ETransformResult::Failed)
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Failed to transform spatial data of type {0}"), FText::FromString(Data->GetClass()->GetName())));
			}
		}

		// Build output tagged data
		FPCGTaggedData OutputData;
		OutputData.Data = DuplicatedData;
		OutputData.Pin = InTaggedData.Pin;
		OutputData.Tags = InTaggedData.Tags;

		// Remap PCGEx cluster tags if present (maintains Vtx/Edges pairing with new IDs)
		RemapClusterTags(OutputData.Tags, ClusterRemapper);

		// Forward input tags if enabled
		if (Settings->bForwardInputTags)
		{
			PointDataFacade->Source->Tags->DumpTo(OutputData.Tags);
		}

		// Forward attributes to point data if configured
		if (ForwardHandler)
		{
			if (UPCGPointData* PointData = Cast<UPCGPointData>(DuplicatedData))
			{
				ForwardHandler->Forward(PointIndex, PointData->Metadata);
			}
		}

		// Register output (Pin: tag added only for default "Out" pin)
		Context->RegisterOutput(OutputData, true, OutIdx);
		return TransformResult;
	}

	void FProcessor::RemapClusterTags(TSet<FString>& Tags, FClusterIdRemapper& ClusterRemapper) const
	{
		// Look for PCGEx cluster tags: PCGEx/Cluster:ID
		static const FString ClusterTagPrefix = TEXT("PCGEx/Cluster:");

		TArray<FString> TagsToRemove;
		TArray<FString> TagsToAdd;

		for (const FString& Tag : Tags)
		{
			if (Tag.StartsWith(ClusterTagPrefix))
			{
				// Extract the original ID
				FString IdString = Tag.Mid(ClusterTagPrefix.Len());
				int32 OriginalId = FCString::Atoi(*IdString);

				// Get the remapped ID (consistent within this point's data)
				int32 NewId = ClusterRemapper.GetRemappedId(OriginalId);

				// Queue for replacement
				TagsToRemove.Add(Tag);
				TagsToAdd.Add(FString::Printf(TEXT("%s%d"), *ClusterTagPrefix, NewId));
			}
		}

		// Apply replacements
		for (const FString& Tag : TagsToRemove) { Tags.Remove(Tag); }
		for (const FString& Tag : TagsToAdd) { Tags.Add(Tag); }
	}

	void FProcessor::CompleteWork()
	{
		// Process each point using the shared asset pool

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const int32 NumPoints = PointDataFacade->GetNum();

		TArray<TSharedPtr<PCGExMT::FTask>> Tasks;
		Tasks.Reserve(NumPoints);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			if (!PointFilterCache[Index]) { continue; }

			const uint64 EntryHash = PointEntryHashes[Index];
			if (EntryHash == 0) { continue; }

			// Get asset from shared pool
			UPCGDataAsset* DataAsset = Context->SharedAssetPool->GetAsset(EntryHash);
			if (!DataAsset) { continue; }

			const FTransform& TargetTransform = InTransforms[Index];

			// Create cluster ID remapper for this point - all data within this point
			// shares the same remapper so Vtx/Edges pairs maintain their relationship
			FClusterIdRemapper ClusterRemapper(ClusterIdCounter);

			// Process each data item in the asset
			for (const FPCGTaggedData& TaggedData : DataAsset->Data.GetAllInputs())
			{
				// Strip embedded CollectionMap entries (consumed by merge in FBatch::OnLoadAssetsComplete)
				if (Settings->bMergeEmbeddedCollectionMaps && TaggedData.Pin == FName(TEXT("CollectionMap"))) { continue; }

				// Apply tag filtering
				if (!PassesTagFilter(TaggedData)) { continue; }

				// Process the data (cluster remapper ensures paired data gets consistent new IDs)
				FSpatialTransformResult Result = ProcessTaggedData(Index, TargetTransform, TaggedData, ClusterRemapper);
				if (Result.Task) { Tasks.Add(Result.Task); }
			}
		}

		if (!Tasks.IsEmpty())
		{
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, TransformTasks)
			TransformTasks->StartTasksBatch(Tasks);
		}
	}

	void FBatch::CompleteWork()
	{
		// Create a token to hold execution in its current state
		// Only move forward once loading is complete
		LoadingToken = TaskManager->TryCreateToken(TEXT("PCGDataAssetLoading"));
		if (!LoadingToken.IsValid())
		{
			// Token creation failed, proceed without loading
			TBatch<FProcessor>::CompleteWork();
			return;
		}

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PCGDataAssetLoader)

		if (!Context->SharedAssetPool->HasEntries())
		{
			// Nothing to load
			PCGEX_ASYNC_RELEASE_TOKEN(LoadingToken)
			TBatch<FProcessor>::CompleteWork();
			return;
		}

		// Load all assets from the shared pool once
		Context->SharedAssetPool->LoadAllAssets(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess)
			{
				PCGEX_ASYNC_THIS
				This->OnLoadAssetsComplete(bSuccess);
			});
	}

	void FBatch::OnLoadAssetsComplete(const bool bSuccess)
	{
		if (bSuccess)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(PCGDataAssetLoader)

			// Merge embedded collection maps if enabled
			if (Settings->bMergeEmbeddedCollectionMaps)
			{
				Context->MergedMapPacker = MakeShared<PCGExCollections::FPickPacker>();
				PCGExCollections::FPickUnpacker TempUnpacker;

				// Scan all loaded data assets for embedded CollectionMap entries
				for (const auto& Pair : Context->SharedAssetPool->GetEntryMap())
				{
					UPCGDataAsset* Asset = Context->SharedAssetPool->GetAsset(Pair.Key);
					if (!Asset) { continue; }

					for (const FPCGTaggedData& TD : Asset->Data.TaggedData)
					{
						if (TD.Pin != FName(TEXT("CollectionMap"))) { continue; }

						const UPCGParamData* ParamData = Cast<UPCGParamData>(TD.Data);
						if (!ParamData) { continue; }

						// Unpack into temporary unpacker (accumulates all GUID→Path pairs)
						TempUnpacker.UnpackDataset(Context, ParamData);
					}
				}

				// Re-pack merged map: iterate unpacker's collection map and register each
				// collection through the packer to produce a merged output
				if (TempUnpacker.HasValidMapping())
				{
					// Create merged param data
					UPCGParamData* MergedMapData = Context->ManagedObjects->New<UPCGParamData>();

					// The unpacker's CollectionMap has (GUID → Collection*) pairs.
					// We need to produce the same format that PackToDataset writes.
					// Since GUIDs are deterministic and globally unique, the union is trivial:
					// just feed each collection through the packer once.
					for (const auto& MapPair : TempUnpacker.GetCollections())
					{
						// Register each collection so packer knows about it
						// GetPickIdx with dummy indices just to register the collection
						Context->MergedMapPacker->GetPickIdx(MapPair.Value, 0, -1);
					}

					Context->MergedMapPacker->PackToDataset(MergedMapData);

					// Output on Map pin
					FPCGTaggedData MapOutput;
					MapOutput.Data = MergedMapData;
					MapOutput.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
					Context->OutputData.TaggedData.Add(MapOutput);
				}
			}

			// Assets loaded, now complete work on all processors
			TBatch<FProcessor>::CompleteWork();
		}

		PCGEX_ASYNC_RELEASE_TOKEN(LoadingToken)
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
