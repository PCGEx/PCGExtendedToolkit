// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadPCGData.h"

#include "PCGDataAsset.h"
#include "PCGParamData.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Collections/PCGExPCGDataAssetCollection.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGLandscapeData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGSurfaceData.h"
#include "Data/PCGVolumeData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Elements/PCGExStagingLoadProperties.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Data/PCGPointArrayData.h"
#include "Data/Utils/PCGExPointReplicate.h"

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
		[PathsToLoad]()
		{
			return PathsToLoad->Array();
		},
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
		: Result(ETransformResult::Success)
		  , Task(InTask)
	{
	}

	class FTransformTask : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformTask)

		FTransformTask(const FTransform& InTransform)
			: FTask()
			  , Transform(InTransform)
		{
		}

		const FTransform& Transform;
	};

	class FTransformPoints final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformPoints)

		FTransformPoints(const FTransform& InTransform, UPCGBasePointData* InData)
			: FTransformTask(InTransform)
			  , Data(InData)
		{
		}

		UPCGBasePointData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TPCGValueRange<FTransform> OutTransforms = Data->GetTransformValueRange();

			PCGEX_PARALLEL_FOR(OutTransforms.Num(), OutTransforms[i] *= Transform;)

			const auto* Settings = TaskManager->GetContext()->GetInputSettings<UPCGExPCGDataAssetLoaderSettings>();
			if (Settings->bRefreshSeeds)
			{
				TPCGValueRange<int32> OutSeeds = Data->GetSeedValueRange(true);
				PCGEX_PARALLEL_FOR(OutSeeds.Num(), OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[i].GetLocation());)
			}
		}
	};

	class FTransformSpline final : public FTransformTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FTransformSpline)

		FTransformSpline(const FTransform& InTransform, UPCGSplineData* InData)
			: FTransformTask(InTransform)
			  , Data(InData)
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
			: FTransformTask(InTransform)
			  , Data(InData)
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
			: FTransformTask(InTransform)
			  , Data(InData)
		{
		}

		UPCGVolumeData* Data = nullptr;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			Data->Initialize(Data->GetStrictBounds().TransformBy(Transform));
		}
	};

	/** Emptiness rule behind bOmitEmptyData. */
	bool IsSpatialDataEmpty(const UPCGSpatialData* InData)
	{
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			return PointData->IsEmpty();
		}

		if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(InData))
		{
			return SplineData->GetNumSegments() == 0;
		}

		if (const UPCGPolyLineData* PolyLineData = Cast<UPCGPolyLineData>(InData))
		{
			return PolyLineData->GetNumSegments() == 0;
		}

		return false;
	}

	FSpatialTransformResult PrepareTransformTask(UPCGSpatialData* InData, const FTransform& InTransform)
	{
		if (!InData)
		{
			return FSpatialTransformResult();
		}

		if (UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			return FSpatialTransformResult(MakeShared<FTransformPoints>(InTransform, PointData));
		}

		if (UPCGSplineData* SplineData = Cast<UPCGSplineData>(InData))
		{
			return FSpatialTransformResult(MakeShared<FTransformSpline>(InTransform, SplineData));
		}

		if (UPCGPolyLineData* PolyLineData = Cast<UPCGPolyLineData>(InData))
		{
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

FPCGTaggedData FPCGExPCGDataAssetLoaderContext::ResolveOutput(const FPCGTaggedData& InTaggedData) const
{
	FName TargetPin = PCGExPCGDataAssetLoader::OutputPinDefault;

	// Check if we have a custom pin that matches
	if (CustomPinNames.Contains(InTaggedData.Pin))
	{
		TargetPin = InTaggedData.Pin;
	}

	FPCGTaggedData LocalOutputData = InTaggedData;

	// Only add Pin: tag for data going to default "Out" pin
	if (TargetPin == PCGExPCGDataAssetLoader::OutputPinDefault && !InTaggedData.Pin.IsNone())
	{
		LocalOutputData.Tags.Add(FString::Printf(TEXT("Pin:%s"), *InTaggedData.Pin.ToString()));
	}

	LocalOutputData.Pin = TargetPin;
	return LocalOutputData;
}

void FPCGExPCGDataAssetLoaderContext::RegisterOutput(const FPCGTaggedData& InTaggedData, const PCGExPCGDataAssetLoader::FOutputOrder& InOrder)
{
	if (!InTaggedData.Data)
	{
		return;
	}

	FPCGTaggedData ResolvedData = ResolveOutput(InTaggedData);
	const FName TargetPin = ResolvedData.Pin;

	FWriteScopeLock WriteLock(OutputLock);
	OutputByPin.FindOrAdd(TargetPin).Add({MoveTemp(ResolvedData), InOrder, true});
}

void FPCGExPCGDataAssetLoaderContext::RegisterUniqueData(const FPCGTaggedData& InTaggedData, const PCGExPCGDataAssetLoader::FOutputOrder& InOrder)
{
	if (!InTaggedData.Data)
	{
		return;
	}

	const uint32 UID = InTaggedData.Data->GetUniqueID();

	{
		FReadScopeLock ReadLock(UniqueDataLock);
		if (const PCGExPCGDataAssetLoader::FUniqueSlot* Slot = UniqueData.Find(UID);
			Slot && !(InOrder < Slot->Order))
		{
			return;
		}
	}

	FWriteScopeLock WriteLock(UniqueDataLock);

	PCGExPCGDataAssetLoader::FUniqueSlot* Slot = UniqueData.Find(UID);
	if (Slot && !(InOrder < Slot->Order))
	{
		return;
	}

	FPCGTaggedData ResolvedData = ResolveOutput(InTaggedData);
	const FName TargetPin = ResolvedData.Pin;

	FWriteScopeLock OutputWriteLock(OutputLock);

	if (Slot)
	{
		// An earlier registrant in output order takes over the entry, tags included
		Slot->Order = InOrder;
		OutputByPin.FindChecked(Slot->Pin)[Slot->Index] = {MoveTemp(ResolvedData), InOrder, false};
		return;
	}

	TArray<PCGExPCGDataAssetLoader::FOutputEntry>& PinOutputs = OutputByPin.FindOrAdd(TargetPin);
	UniqueData.Add(UID, {InOrder, TargetPin, PinOutputs.Num()});
	PinOutputs.Add({MoveTemp(ResolvedData), InOrder, false});
}

void FPCGExPCGDataAssetLoaderContext::StageRegisteredOutputs()
{
	int32 NumOutputs = 0;
	for (const TPair<FName, TArray<PCGExPCGDataAssetLoader::FOutputEntry>>& Pair : OutputByPin)
	{
		NumOutputs += Pair.Value.Num();
	}

	IncreaseStagedOutputReserve(NumOutputs);

	// Pin order is irrelevant downstream (each pin is gathered separately); the order within a pin is total
	for (TPair<FName, TArray<PCGExPCGDataAssetLoader::FOutputEntry>>& Pair : OutputByPin)
	{
		Pair.Value.Sort([](const PCGExPCGDataAssetLoader::FOutputEntry& A, const PCGExPCGDataAssetLoader::FOutputEntry& B) { return A.Order < B.Order; });

		for (const PCGExPCGDataAssetLoader::FOutputEntry& Entry : Pair.Value)
		{
			// Asset-owned data is shared with the loaded asset: never mutated, never managed
			const PCGExData::EStaging Staging =
				(Entry.bOwned ? PCGExData::EStaging::MutableAndManaged : PCGExData::EStaging::None) |
				(Entry.TaggedData.bPinlessData ? PCGExData::EStaging::Pinless : PCGExData::EStaging::None);

			StageOutput(const_cast<UPCGData*>(Entry.TaggedData.Data.Get()), Entry.TaggedData.Pin, Staging, Entry.TaggedData.Tags);
		}
	}
}

#pragma endregion

#pragma region UPCGSettings

void UPCGExPCGDataAssetLoaderSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from staging nodes or Get Collection Data.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExPCGDataAssetLoaderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	// Main output pin, same label as PCGExPCGDataAssetLoader::OutputPinDefault ("Out").
	// Must be declared once and stay at index 0: RegisterOutput routes unmatched data to it,
	// and the inactive-pin bitmask in AdvanceWork assumes [Out, CustomOutputPins..., Map].
	PCGEX_PIN_ANY(GetMainOutputPin(), "Loaded data that doesn't match any custom pin, tagged with Pin:OriginalPinName. From points: spatial data is one per input point, other is single instance only. From attribute sets: asset contents as-is, once per asset, or once per row with Targets Forwarding.", Normal)

	// Custom output pins, routed by exact pin name
	for (const FPCGPinProperties& CustomPin : CustomOutputPins)
	{
		if (!CustomPin.Label.IsNone())
		{
			PinProperties.Add(CustomPin);
		}
	}

	if (bMergeEmbeddedCollectionMaps)
	{
		PCGEX_PIN_PARAMS(PCGExCollections::Labels::OutputCollectionMapLabel, "Merged collection map from embedded data assets.", Normal)
	}

	return PinProperties;
}

#pragma endregion

PCGEX_INITIALIZE_ELEMENT(PCGDataAssetLoader)
PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(PCGDataAssetLoader)

void FPCGExPCGDataAssetLoaderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	FPCGExPointsProcessorElement::DisabledPassThroughData(Context);
	PCGExCollections::ForwardCollectionMap(Context);
}

bool FPCGExPCGDataAssetLoaderElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PCGDataAssetLoader)

	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

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
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->StageRegisteredOutputs();

	// Mark unused pins as inactive
	int32 PinIndex = 0;

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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::NoInit)

		// Attribute-set inputs arrive as a temp identity-point conversion (see PCGExPointIO::ToPointData);
		// there is nothing to spawn onto, so loaded contents are output as-is.
		bPassthrough = PointDataFacade->Source->IsConvertedInput();

		EntryHashGetter = PointDataFacade->GetReadable<int64>(Settings->GetEntryIdxAttributeName(), PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Missing staging hash attribute. Make sure inputs were staged (points) or come from Get Collection Data (attribute sets), with a matching Collection Map."));
			return false;
		}

		// Setup forward handler if needed
		if (Settings->TargetsForwarding.bEnabled)
		{
			ForwardHandler = Settings->TargetsForwarding.GetHandler(PointDataFacade);

			// Protected, every layer: a seed's staged picks would overwrite the ones spawned points carry themselves
			ForwardHandler->ValidateIdentities([](const PCGExData::FAttributeIdentity& Identity)
			{
				return !PCGExCollections::Labels::IsEntryIdxName(Identity.Name);
			});
		}

		if (Settings->bForwardInputTags)
		{
			// The input's own pairing tags would give every copy the same pair ID
			const TSharedPtr<PCGExData::FTags> InputTags = MakeShared<PCGExData::FTags>(PointDataFacade->Source->Tags);
			InputTags->Remove(PCGExClusters::Labels::ProtectedClusterTags);
			InputTags->DumpTo(ForwardedInputTags);
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
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const int64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == -1)
			{
				continue;
			}

			int16 SecondaryIndex = 0;
			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, SecondaryIndex);

			if (!Result.IsValid())
			{
				continue;
			}

			if (!Result.Entry->IsType(PCGExAssetCollection::TypeIds::PCGDataAsset))
			{
				continue;
			}

			const FPCGExPCGDataAssetCollectionEntry* PCGDataEntry = static_cast<const FPCGExPCGDataAssetCollectionEntry*>(Result.Entry);

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

	const FString ClusterTagPrefix = TEXT("PCGEx/Cluster:");

	int32 ParseClusterId(const FString& InClusterTag)
	{
		return FCString::Atoi(*InClusterTag.Mid(ClusterTagPrefix.Len()));
	}

	ERoute FProcessor::RouteDatum(const FPCGTaggedData& InTaggedData) const
	{
		if (!InTaggedData.Data)
		{
			return ERoute::Skip;
		}

		// Embedded collection maps are merged into the Map output (FBatch::OnLoadAssetsComplete), never spawned
		if (Settings->bMergeEmbeddedCollectionMaps && InTaggedData.Pin == PCGExCollections::Labels::CollectionMapPin)
		{
			return ERoute::Skip;
		}

		if (!PassesTagFilter(InTaggedData))
		{
			return ERoute::Skip;
		}

		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(InTaggedData.Data);

		if (bPassthrough)
		{
			if (Settings->bOmitEmptyData && SpatialData && IsSpatialDataEmpty(SpatialData))
			{
				return ERoute::Skip;
			}

			// Attribute forwarding needs writable metadata
			return ForwardHandler ? ERoute::Copy : ERoute::Unique;
		}

		if (!SpatialData)
		{
			return ERoute::Unique;
		}

		if (Settings->bOmitEmptyData && IsSpatialDataEmpty(SpatialData))
		{
			return ERoute::Skip;
		}

		return ShouldMerge(InTaggedData) ? ERoute::Merge : ERoute::Copy;
	}

	UPCGDataAsset* FProcessor::GetTargetAsset(const int32 PointIndex) const
	{
		if (!PointFilterCache[PointIndex] || PointEntryHashes[PointIndex] == 0)
		{
			return nullptr;
		}

		return Context->SharedAssetPool->GetAsset(PointEntryHashes[PointIndex]);
	}

	int32 FProcessor::GetClusterIdDemand() const
	{
		// Routing depends on the datum alone, so every target of one asset consumes the same number of IDs.
		// An ID reserved for a copy whose duplication fails is left unused.
		TMap<const UPCGDataAsset*, int32> DemandPerAsset;
		int32 Demand = 0;

		for (int32 Index = 0; Index < PointEntryHashes.Num(); Index++)
		{
			const UPCGDataAsset* DataAsset = GetTargetAsset(Index);
			if (!DataAsset)
			{
				continue;
			}

			if (const int32* KnownDemand = DemandPerAsset.Find(DataAsset))
			{
				Demand += *KnownDemand;
				continue;
			}

			// One ID per distinct original ID among copied data, as FClusterIdRemapper assigns them
			TSet<int32> OriginalIds;
			for (const FPCGTaggedData& TaggedData : DataAsset->Data.GetAllInputs())
			{
				if (RouteDatum(TaggedData) != ERoute::Copy)
				{
					continue;
				}

				for (const FString& Tag : TaggedData.Tags)
				{
					if (Tag.StartsWith(ClusterTagPrefix))
					{
						OriginalIds.Add(ParseClusterId(Tag));
					}
				}
			}

			DemandPerAsset.Add(DataAsset, OriginalIds.Num());
			Demand += OriginalIds.Num();
		}

		return Demand;
	}

	FSpatialTransformResult FProcessor::ProcessTaggedData(const int32 PointIndex, const int32 DatumIndex, const FTransform& TargetTransform, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper)
	{
		const ERoute Route = RouteDatum(InTaggedData);
		if (Route == ERoute::Skip)
		{
			return FSpatialTransformResult();
		}

		const FOutputOrder Order{1, BatchIndex, PointIndex, DatumIndex};

		if (bPassthrough)
		{
			// Attribute-set input: no target transform, output loaded contents as-is
			ProcessPassthroughData(PointIndex, Order, Route, InTaggedData, ClusterRemapper);
			return FSpatialTransformResult();
		}

		if (Route == ERoute::Unique)
		{
			// Non-spatial data: output once per unique data (not per point), ahead of spatial data
			Context->RegisterUniqueData(InTaggedData, FOutputOrder{0, BatchIndex, PointIndex, DatumIndex});
			return FSpatialTransformResult();
		}

		if (Route == ERoute::Merge)
		{
			QueueMerge(PointIndex, Order, InTaggedData);
			return FSpatialTransformResult();
		}

		// Spatial data: duplicate and transform for this point
		const UPCGData* Data = InTaggedData.Data.Get();
		UPCGSpatialData* DuplicatedData = Context->ManagedObjects->DuplicateData<UPCGSpatialData>(Data);

		if (!DuplicatedData)
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Failed to duplicate spatial data of type {0}"), FText::FromString(Data->GetClass()->GetName())));
			}
			return FSpatialTransformResult();
		}

		// Apply transform
		FSpatialTransformResult TransformResult = PrepareTransformTask(DuplicatedData, TargetTransform);

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

		FPCGTaggedData OutputData;
		OutputData.Data = DuplicatedData;
		OutputData.Pin = InTaggedData.Pin;
		OutputData.Tags = InTaggedData.Tags;

		// Remap PCGEx cluster tags if present (maintains Vtx/Edges pairing with new IDs)
		RemapClusterTags(OutputData.Tags, ClusterRemapper);
		OutputData.Tags.Append(ForwardedInputTags);

		// Forward attributes to point data if configured
		if (ForwardHandler)
		{
			if (UPCGMetadata* TargetMetadata = DuplicatedData->MutableMetadata())
			{
				ForwardHandler->Forward(PointIndex, TargetMetadata);
			}
		}

		// Register output (Pin: tag added only for default "Out" pin)
		Context->RegisterOutput(OutputData, Order);
		return TransformResult;
	}

	void FProcessor::ProcessPassthroughData(const int32 PointIndex, const FOutputOrder& InOrder, const ERoute Route, const FPCGTaggedData& InTaggedData, FClusterIdRemapper& ClusterRemapper)
	{
		FPCGTaggedData OutputData = InTaggedData;

		if (Route == ERoute::Unique)
		{
			// Raw: asset-owned data goes out untouched, once per unique data. Cluster IDs stay as saved.
			OutputData.Tags.Append(ForwardedInputTags);
			Context->RegisterUniqueData(OutputData, InOrder);
			return;
		}

		// Attribute forwarding needs writable metadata: duplicate (never transformed), one per row
		const UPCGData* Data = InTaggedData.Data.Get();
		UPCGData* DuplicatedData = Context->ManagedObjects->DuplicateData<UPCGData>(Data);
		if (!DuplicatedData)
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Failed to duplicate data of type {0}"), FText::FromString(Data->GetClass()->GetName())));
			}
			return;
		}

		OutputData.Data = DuplicatedData;

		// Per-row copies need distinct cluster IDs to keep Vtx/Edges pairs unambiguous
		RemapClusterTags(OutputData.Tags, ClusterRemapper);
		OutputData.Tags.Append(ForwardedInputTags);

		if (UPCGMetadata* TargetMetadata = DuplicatedData->MutableMetadata())
		{
			ForwardHandler->Forward(PointIndex, TargetMetadata);
		}

		Context->RegisterOutput(OutputData, InOrder);
	}

	bool FProcessor::ShouldMerge(const FPCGTaggedData& InTaggedData) const
	{
		if (!Settings->bMergePointOutputs || bPassthrough || !Cast<UPCGBasePointData>(InTaggedData.Data))
		{
			return false;
		}

		// Concatenated Vtx copies would collide their endpoint identities with the paired Edges
		for (const FString& Tag : InTaggedData.Tags)
		{
			if (Tag.StartsWith(ClusterTagPrefix))
			{
				return false;
			}
		}

		return true;
	}

	void FProcessor::QueueMerge(const int32 PointIndex, const FOutputOrder& InOrder, const FPCGTaggedData& InTaggedData)
	{
		// InTaggedData lives in the loaded asset's collection, so its address identifies the entry across targets
		if (const int32* GroupIndex = MergeGroupByEntry.Find(&InTaggedData))
		{
			MergeGroups[*GroupIndex]->TargetIndices.Add(PointIndex);
			return;
		}

		TSharedPtr<FMergeGroup> Group = MakeShared<FMergeGroup>();
		Group->Source = InTaggedData;
		Group->Order = InOrder;
		Group->TargetIndices.Add(PointIndex);
		MergeGroupByEntry.Add(&InTaggedData, MergeGroups.Add(Group));
	}

	bool FProcessor::StartMergeGroup(const TSharedPtr<FMergeGroup>& Group)
	{
		const UPCGBasePointData* SourceData = Cast<UPCGBasePointData>(Group->Source.Data);

		Group->MergedIO = MakeShared<PCGExData::FPointIO>(PointDataFacade->Source->GetContextHandle(), SourceData);
		Group->MergedIO->SetInfos(0, OutputPinDefault);
		if (!Group->MergedIO->InitializeOutput<UPCGPointArrayData>(PCGExData::EIOInit::New))
		{
			if (!Settings->bQuietUnsupportedTypeWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Failed to create merged output for point data of type {0}"), FText::FromString(SourceData->GetClass()->GetName())));
			}
			return false;
		}

		// Registered now, ordered by first target; the batch only collects outputs once every task is done.
		FPCGTaggedData OutputData;
		OutputData.Data = Group->MergedIO->GetOut();
		OutputData.Pin = Group->Source.Pin;
		OutputData.Tags = Group->Source.Tags;
		OutputData.Tags.Append(ForwardedInputTags);

		Context->RegisterOutput(OutputData, Group->Order);

		// bOmitEmptyData off: one empty output stands in for the empty per-target duplicates.
		return SourceData->GetNumPoints() > 0;
	}

	void FProcessor::ReplicateGroup(const FMergeGroup& Group) const
	{
		const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		TArray<FTransform> CopyTransforms;
		CopyTransforms.SetNumUninitialized(Group.TargetIndices.Num());
		for (int32 k = 0; k < Group.TargetIndices.Num(); k++)
		{
			CopyTransforms[k] = InTransforms[Group.TargetIndices[k]];
		}

		PCGExPointReplicate::FCopies Copies;
		Copies.Transforms = CopyTransforms;
		Copies.bRefreshSeeds = Settings->bRefreshSeeds;

		PCGExPointReplicate::FForward Forward;
		Forward.Handler = ForwardHandler.Get();
		Forward.SourceIndices = Group.TargetIndices;

		PCGExPointReplicate::Replicate(Cast<UPCGBasePointData>(Group.Source.Data), Group.MergedIO->GetOut(), Copies, &Forward);
	}

	void FProcessor::RemapClusterTags(TSet<FString>& Tags, FClusterIdRemapper& ClusterRemapper) const
	{
		TArray<FString> TagsToRemove;
		TArray<FString> TagsToAdd;

		for (const FString& Tag : Tags)
		{
			if (Tag.StartsWith(ClusterTagPrefix))
			{
				// Get the remapped ID (consistent within this point's data)
				const int32 NewId = ClusterRemapper.GetRemappedId(ParseClusterId(Tag));

				// Queue for replacement
				TagsToRemove.Add(Tag);
				TagsToAdd.Add(FString::Printf(TEXT("%s%d"), *ClusterTagPrefix, NewId));
			}
		}

		// Apply replacements
		for (const FString& Tag : TagsToRemove)
		{
			Tags.Remove(Tag);
		}
		for (const FString& Tag : TagsToAdd)
		{
			Tags.Add(Tag);
		}
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
			const UPCGDataAsset* DataAsset = GetTargetAsset(Index);
			if (!DataAsset)
			{
				continue;
			}

			const FTransform& TargetTransform = InTransforms[Index];

			// Create cluster ID remapper for this point - all data within this point
			// shares the same remapper so Vtx/Edges pairs maintain their relationship
			FClusterIdRemapper ClusterRemapper(ClusterIdCounter);

			const TArray<FPCGTaggedData>& AssetData = DataAsset->Data.GetAllInputs();
			for (int32 DatumIndex = 0; DatumIndex < AssetData.Num(); DatumIndex++)
			{
				// Process the data (cluster remapper ensures paired data gets consistent new IDs)
				FSpatialTransformResult Result = ProcessTaggedData(Index, DatumIndex, TargetTransform, AssetData[DatumIndex], ClusterRemapper);
				if (Result.Task)
				{
					Tasks.Add(Result.Task);
				}
			}
		}

		if (!Tasks.IsEmpty())
		{
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, TransformTasks)
			TransformTasks->StartTasksBatch(Tasks);
		}

		TArray<int32> GroupsToReplicate;
		for (int32 GroupIndex = 0; GroupIndex < MergeGroups.Num(); GroupIndex++)
		{
			if (StartMergeGroup(MergeGroups[GroupIndex]))
			{
				GroupsToReplicate.Add(GroupIndex);
			}
		}

		if (!GroupsToReplicate.IsEmpty())
		{
			// One task per merged output: CompleteWork can run on the game thread (asset-load callback).
			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ReplicateTasks)
			for (const int32 GroupIndex : GroupsToReplicate)
			{
				ReplicateTasks->AddSimpleCallback(
					[PCGEX_ASYNC_THIS_CAPTURE, GroupIndex]()
					{
						PCGEX_ASYNC_THIS
						This->ReplicateGroup(*This->MergeGroups[GroupIndex]);
					});
			}
			ReplicateTasks->StartSimpleCallbacks();
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

			if (Settings->bMergeEmbeddedCollectionMaps)
			{
				Context->MergedMapPacker = MakeShared<PCGExCollections::FPickPacker>();
				PCGExCollections::FPickUnpacker TempUnpacker;

				// Asset path order, not pool order (filled by parallel point processing): a GUID collision keeps
				// the first mapping unpacked, and the merged map's rows follow unpack order.
				TSet<UPCGDataAsset*> UniqueAssets;
				for (const auto& Pair : Context->SharedAssetPool->GetEntryMap())
				{
					if (UPCGDataAsset* Asset = Context->SharedAssetPool->GetAsset(Pair.Key))
					{
						UniqueAssets.Add(Asset);
					}
				}

				TArray<UPCGDataAsset*> Assets = UniqueAssets.Array();
				Assets.Sort([](const UPCGDataAsset& A, const UPCGDataAsset& B)
				{
					return A.GetPathName().Compare(B.GetPathName(), ESearchCase::CaseSensitive) < 0;
				});

				for (const UPCGDataAsset* Asset : Assets)
				{
					for (const FPCGTaggedData& TD : Asset->Data.TaggedData)
					{
						if (TD.Pin != PCGExCollections::Labels::CollectionMapPin)
						{
							continue;
						}

						const UPCGParamData* ParamData = Cast<UPCGParamData>(TD.Data);
						if (!ParamData)
						{
							continue;
						}

						// Unpack into temporary unpacker (accumulates all GUID→Path pairs)
						TempUnpacker.UnpackDataset(Context, ParamData);
					}
				}

				if (TempUnpacker.HasValidMapping())
				{
					UPCGParamData* MergedMapData = Context->ManagedObjects->New<UPCGParamData>();

					TempUnpacker.RegisterCollectionsTo(*Context->MergedMapPacker);
					Context->MergedMapPacker->PackToDataset(MergedMapData);

					FPCGTaggedData MapOutput;
					MapOutput.Data = MergedMapData;
					MapOutput.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
					Context->OutputData.TaggedData.Add(MapOutput);
				}
			}

			ReserveClusterIds();

			// Assets loaded, now complete work on all processors
			TBatch<FProcessor>::CompleteWork();
		}

		PCGEX_ASYNC_RELEASE_TOKEN(LoadingToken)
	}

	void FBatch::ReserveClusterIds()
	{
		TArray<int32> Demand;
		Demand.Init(0, Processors.Num());

		PCGExMT::ParallelOrSequential(
			Processors.Num(),
			[&](const int32 i)
			{
				if (Processors[i]->bIsProcessorValid)
				{
					Demand[i] = GetProcessorRef<FProcessor>(i)->GetClusterIdDemand();
				}
			}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		// Serial and in input order: the bases never depend on which processor completes first
		int32 Base = 0;
		for (int32 i = 0; i < Processors.Num(); i++)
		{
			GetProcessorRef<FProcessor>(i)->ClusterIdCounter = Base;
			Base += Demand[i];
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
