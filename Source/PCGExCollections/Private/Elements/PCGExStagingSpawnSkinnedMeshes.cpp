// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSpawnSkinnedMeshes.h"

#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Core/PCGExCollectionTags.h"
#include "Core/PCGExMT.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataMacros.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/SkinnedAsset.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGExCollectionPropertyFloatPacker.h"
#include "Helpers/PCGExFunctionPrototypes.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "InstanceDataPackers/PCGSkinnedMeshInstanceDataPackerByRegex.h"
#include "Internationalization/Regex.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSpawnSkinnedMeshesElement"
#define PCGEX_NAMESPACE StagingSpawnSkinnedMeshes

PCGEX_INITIALIZE_ELEMENT(StagingSpawnSkinnedMeshes)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingSpawnSkinnedMeshes)

#pragma region UPCGExStagingSpawnSkinnedMeshesSettings

void UPCGExStagingSpawnSkinnedMeshesSettings::PostLoad()
{
	Super::PostLoad();

	if (!InstanceDataPackerParameters)
	{
		RefreshInstancePacker();
	}
	else
	{
		InstanceDataPackerParameters->SetFlags(GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
	}
}

#if WITH_EDITOR
void UPCGExStagingSpawnSkinnedMeshesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGExStagingSpawnSkinnedMeshesSettings, InstanceDataPackerType))
	{
		RefreshInstancePacker();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGExStagingSpawnSkinnedMeshesSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

void UPCGExStagingSpawnSkinnedMeshesSettings::SetInstancePackerType(TSubclassOf<UPCGSkinnedMeshInstanceDataPackerBase> InInstancePackerType)
{
	if (!InstanceDataPackerParameters || InInstancePackerType != InstanceDataPackerType)
	{
		InstanceDataPackerType = InInstancePackerType;
		RefreshInstancePacker();
	}
}

void UPCGExStagingSpawnSkinnedMeshesSettings::RefreshInstancePacker()
{
	if (!InstanceDataPackerType)
	{
		InstanceDataPackerParameters = nullptr;
		return;
	}

	ensure(IsInGameThread());

	if (InstanceDataPackerParameters)
	{
#if WITH_EDITOR
		InstanceDataPackerParameters->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
#endif
		InstanceDataPackerParameters->MarkAsGarbage();
		InstanceDataPackerParameters = nullptr;
	}

	InstanceDataPackerParameters = NewObject<UPCGSkinnedMeshInstanceDataPackerBase>(this, InstanceDataPackerType, NAME_None, GetMaskedFlags(RF_PropagateToSubObjects));
}

#pragma endregion

#pragma region FPCGExStagingSpawnSkinnedMeshesElement

bool FPCGExStagingSpawnSkinnedMeshesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingSpawnSkinnedMeshes)

	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	if (Settings->InstanceDataPackerType && !Settings->InstanceDataPackerParameters)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Invalid instance data packer instance, try reselecting the packer type."));
		return false;
	}

	return true;
}

bool FPCGExStagingSpawnSkinnedMeshesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingSpawnSkinnedMeshesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingSpawnSkinnedMeshes)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		// Reuse detection key; each processor derives its own from it.
		GetDependenciesCrc(FPCGGetDependenciesCrcParams(&Context->InputData, Settings, nullptr), Context->DependenciesCrc);

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	// Output drives a main-thread loop that creates and fills components -- illegal during a package save or GC.
	PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (!Settings->PostProcessFunctionNames.IsEmpty())
	{
		AActor* TargetActor = Settings->TargetActor.Get() ? Settings->TargetActor.Get() : Context->GetTypedExecutionTarget<AActor>();
		if (TargetActor)
		{
			for (UFunction* Function : PCGExHelpers::FindUserFunctions(TargetActor->GetClass(), Settings->PostProcessFunctionNames, {UPCGExFunctionPrototypes::GetPrototypeWithNoParams()}, Context))
			{
				TargetActor->ProcessEvent(Function, nullptr);
			}
		}
	}

	Context->MainPoints->StageOutputs();
	return Context->TryComplete();
}

void FPCGExStagingSpawnSkinnedMeshesElement::AbortInternal(FPCGContext* InContext) const
{
	FPCGExPointsProcessorElement::AbortInternal(InContext);

	if (!InContext)
	{
		return;
	}

	// A component an interrupted run was filling is marked unused so the cleanup removes it, never kept as a complete one.
	FPCGExStagingSpawnSkinnedMeshesContext* Context = static_cast<FPCGExStagingSpawnSkinnedMeshesContext*>(InContext);
	TSet<TSoftObjectPtr<AActor>> Dummy;
	for (const TWeakObjectPtr<UPCGManagedISKMComponent>& Resource : Context->TouchedResources)
	{
		if (Resource.IsValid())
		{
			Resource->Release(/*bHardRelease=*/false, Dummy);
		}
	}
}

#pragma endregion

#pragma region PCGExStagingSpawnSkinnedMeshes::FProcessor

namespace PCGExStagingSpawnSkinnedMeshes
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingSpawnSkinnedMeshes::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)

		EntryHashGetter = PointDataFacade->GetReadable<int64>(Settings->GetEntryIdxAttributeName(), PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			return false;
		}

		AnimationIndexSV = Settings->AnimationIndex.GetValueSetting();
		if (!AnimationIndexSV->Init(PointDataFacade))
		{
			return false;
		}

		NumPoints = PointDataFacade->Source->GetNum(PCGExData::EIOSide::In);
		ResolvedEntries.SetNum(NumPoints);

		if (!AnimationIndexSV->IsConstant())
		{
			AnimationIndices.SetNumZeroed(NumPoints);
		}

		bApplyMaterials = Settings->bApplyMaterialOverrides;
		Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

		InputCrc = Context->DependenciesCrc;
		if (InputCrc.IsValid())
		{
			InputCrc.Combine(static_cast<uint32>(BatchIndex));
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		// Reserve hint of 4 paths per scope: a skinned asset plus a couple of variant materials. Sets grow if needed.
		ScopedUniquePaths = MakeShared<PCGExMT::TScopedSet<FSoftObjectPath>>(Loops, 4);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnSkinnedMeshes::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		const bool bAnimationIsConstant = AnimationIndexSV->IsConstant();
		PCGEX_SV_VIEW_COND(AnimationIndexSV, !bAnimationIsConstant)

		// Per-scope local set: no contention during the loop, one collapse pass afterwards.
		TSet<FSoftObjectPath>& LocalPaths = ScopedUniquePaths->Get_Ref(Scope);

		int16 MaterialPick = -1;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == static_cast<uint64>(-1))
			{
				continue;
			}

			MaterialPick = -1;
			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid())
			{
				continue;
			}

			if (!Result.Entry->IsType(PCGExAssetCollection::TypeIds::SkinnedMesh))
			{
				NumNonSkinnedPicks.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			const FPCGExSkinnedMeshCollectionEntry* Entry = static_cast<const FPCGExSkinnedMeshCollectionEntry*>(Result.Entry);
			if (Entry->SkinnedAsset.IsNull())
			{
				continue;
			}

			// Each thread writes unique indices, no lock
			FResolvedEntry& Resolved = ResolvedEntries[Index];
			Resolved.Entry = Entry;
			Resolved.Host = Result.Host;
			Resolved.Hash = Hash;
			Resolved.MaterialPick = MaterialPick;

			LocalPaths.Add(Entry->SkinnedAsset.ToSoftObjectPath());
			if (bApplyMaterials)
			{
				Entry->GetMaterialPaths(MaterialPick, LocalPaths);
			}

			if (!bAnimationIsConstant)
			{
				AnimationIndices[Index] = PCGEX_SV_READ(AnimationIndexSV, Index - Scope.Start);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnSkinnedMeshes::OnPointsProcessingComplete);

		TSet<FSoftObjectPath> UniquePaths;
		ScopedUniquePaths->Collapse(UniquePaths);
		ScopedUniquePaths.Reset();

		if (const int32 NumSkipped = NumNonSkinnedPicks.load(std::memory_order_relaxed); NumSkipped > 0 && !Settings->bQuietInvalidEntryWarnings)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("{0} points pick entries that are not Skinned Mesh entries. Skipped."), NumSkipped));
		}

		BuildPartitions(UniquePaths);

		if (Partitions.IsEmpty())
		{
			return;
		}

		TArray<FSoftObjectPath> PathsToLoad = UniquePaths.Array();

		// Success is not required: a partition whose asset is still missing is skipped with a warning at spawn time.
		PCGExHelpers::LoadTracked(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE, PathsToLoad = MoveTemp(PathsToLoad)]() -> TArray<FSoftObjectPath>
			{
				PCGEX_ASYNC_THIS_RET({})
				return PathsToLoad;
			},
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess)
			{
				PCGEX_ASYNC_THIS
				This->OnAssetsLoaded();
			});
	}

	void FProcessor::BuildPartitions(TSet<FSoftObjectPath>& InOutPathsToLoad)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnSkinnedMeshes::BuildPartitions);

		// The pick hash already tells entries and material picks apart, so it is the whole partition key.
		TMap<uint64, int32> PartitionIndexByHash;

		for (int32 i = 0; i < NumPoints; i++)
		{
			const FResolvedEntry& Resolved = ResolvedEntries[i];
			if (!Resolved.Entry)
			{
				continue;
			}

			int32 PartitionIndex = INDEX_NONE;
			if (const int32* Found = PartitionIndexByHash.Find(Resolved.Hash))
			{
				PartitionIndex = *Found;
			}
			else
			{
				PartitionIndex = Partitions.Num();
				FPartition& Partition = Partitions.Emplace_GetRef();
				Partition.Entry = Resolved.Entry;
				Partition.Host = Resolved.Host;
				Partition.Hash = Resolved.Hash;
				Partition.MaterialPick = Resolved.MaterialPick;
				Partition.List.AttributePartitionIndex = static_cast<int64>(Resolved.Hash);
				PartitionIndexByHash.Add(Resolved.Hash, PartitionIndex);
			}

			Partitions[PartitionIndex].List.InstancePointIndices.Add(i);
		}

		if (Partitions.IsEmpty())
		{
			return;
		}

		// One layout for the whole selection: a per-entry one would move a property between entries,
		// and the material reading that slot has no way to notice.
		PCGExCollections::FPCGExCollectionPropertyFloatPacker CustomDataPacker;
		bool bPackCustomPrimitiveData = false;

		if (!Settings->CustomPrimitiveDataLayout.IsEmpty())
		{
			TArray<const UPCGExAssetCollection*> Hosts;
			Context->CollectionUnpacker->GetCollectionsInStableOrder(Hosts);

			bPackCustomPrimitiveData = CustomDataPacker.Initialize(
				ExecutionContext, Settings->CustomPrimitiveDataLayout, Hosts, FCustomPrimitiveData::NumCustomPrimitiveDataFloats);

#if WITH_EDITOR
			if (bPackCustomPrimitiveData && Settings->bDebugCustomPrimitiveData)
			{
				PCGE_LOG_C(Log, LogOnly, ExecutionContext, FText::Format(
					           FTEXT("Custom Primitive Data layout ({0} floats): {1}"),
					           CustomDataPacker.GetNumFloats(), FText::FromString(CustomDataPacker.DescribeLayout())));
			}
#endif
		}

		const bool bPacksNatively = PacksNatively();
		const int32 ConstantAnimationIndex = AnimationIndices.IsEmpty() ? AnimationIndexSV->Read(0) : 0;

		for (FPartition& Partition : Partitions)
		{
			FPCGSoftSkinnedMeshComponentDescriptor& Descriptor = Partition.List.Descriptor;

			if (Settings->bUseTemplateDescriptor)
			{
				Descriptor = Settings->TemplateDescriptor;
				Descriptor.SkinnedAsset = Partition.Entry->SkinnedAsset;
				PCGExCollections::Tags::AppendOutputTags(*Partition.Entry, Partition.Host, Descriptor.ComponentTags);
			}
			else
			{
				// Globals via the host's seam; hosts without a block fall back to the local descriptor.
				Partition.Entry->InitPCGSoftSkinnedDescriptor(Partition.Host, Descriptor);
			}

			if (Settings->bForceDisableCollisions)
			{
				Descriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			if (!Descriptor.TransformProvider.IsNull())
			{
				InOutPathsToLoad.Add(Descriptor.TransformProvider.ToSoftObjectPath());
			}

			if (bPackCustomPrimitiveData)
			{
				CustomDataPacker.PackEntry(Partition.Entry, Partition.Host, Partition.CustomPrimitiveData);

#if WITH_EDITOR
				if (Settings->bDebugCustomPrimitiveData)
				{
					PCGE_LOG_C(Log, LogOnly, ExecutionContext, FText::Format(
						           FTEXT("Custom Primitive Data for entry '{0}': {1}"),
						           FText::FromString(Partition.Entry->Staging.Path.ToString()),
						           FText::FromString(PCGExCollections::DescribeFloats(Partition.CustomPrimitiveData))));
				}
#endif
			}

			const TArray<int32>& InstanceIndices = Partition.List.InstancePointIndices;
			Partition.List.Instances.Reserve(InstanceIndices.Num());
			for (const int32 i : InstanceIndices)
			{
				FPCGSkinnedMeshInstance& Instance = Partition.List.Instances.Emplace_GetRef();
				Instance.Transform = Transforms[i];
				Instance.AnimationIndex = AnimationIndices.IsEmpty() ? ConstantAnimationIndex : AnimationIndices[i];
			}

			// Blueprint packers run on the game thread, from the spawn loop.
			if (bPacksNatively)
			{
				PackCustomData(Partition);
			}
		}
	}

	bool FProcessor::PacksNatively() const
	{
		const UPCGSkinnedMeshInstanceDataPackerBase* Packer = Settings->InstanceDataPackerParameters;
		if (!Packer)
		{
			return true;
		}

		// A Blueprint subclass may override PackInstances even when it inherits static selectors.
		if (!Packer->GetClass()->IsNative())
		{
			return false;
		}

		return Packer->GetAttributeSelectors().IsSet() || Packer->IsA<UPCGSkinnedMeshInstanceDataPackerByRegex>();
	}

	void FProcessor::PackCustomData(FPartition& Partition) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnSkinnedMeshes::PackCustomData);

		const UPCGSkinnedMeshInstanceDataPackerBase* Packer = Settings->InstanceDataPackerParameters;
		if (!Packer)
		{
			return;
		}

		const UPCGBasePointData* InData = PointDataFacade->Source->GetIn();
		FPCGSkinnedMeshPackedCustomData& Packed = Partition.PackedCustomData;

		if (!PacksNatively())
		{
			Packer->PackInstances(*ExecutionContext, InData, Partition.List, Packed);
			return;
		}

		// The stock packers key their reads off the legacy point data of the instance list, and read point indices as
		// metadata entry keys without it. Their selectors are driven here instead, with keys built on the base point data.
		TArray<FPCGAttributePropertyInputSelector> Selectors;

		if (const TOptional<TConstArrayView<FPCGAttributePropertyInputSelector>> StaticSelectors = Packer->GetAttributeSelectors(); StaticSelectors.IsSet())
		{
			for (const FPCGAttributePropertyInputSelector& Selector : StaticSelectors.GetValue())
			{
				Selectors.Add(Selector);
			}
		}
		else if (const UPCGSkinnedMeshInstanceDataPackerByRegex* RegexPacker = Cast<UPCGSkinnedMeshInstanceDataPackerByRegex>(Packer))
		{
			if (!InData->Metadata)
			{
				PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Unable to get metadata from input."));
				return;
			}

			TArray<FName> AttributeNames;
			TArray<EPCGMetadataTypes> AttributeTypes;
			InData->Metadata->GetAttributes(AttributeNames, AttributeTypes);

			TSet<FName> Matched;
			for (const FString& Regex : RegexPacker->RegexPatterns)
			{
				const FRegexPattern Pattern(Regex);
				for (const FName& AttributeName : AttributeNames)
				{
					FRegexMatcher Matcher(Pattern, AttributeName.ToString());
					if (!Matcher.FindNext() || Matched.Contains(AttributeName))
					{
						continue;
					}

					Matched.Add(AttributeName);
					Selectors.Emplace_GetRef().SetAttributeName(AttributeName);
				}
			}
		}

		TArray<TUniquePtr<const IPCGAttributeAccessor>> Accessors;
		TArray<TUniquePtr<const IPCGAttributeAccessorKeys>> Keys;
		Accessors.Reserve(Selectors.Num());
		Keys.Reserve(Selectors.Num());

		for (const FPCGAttributePropertyInputSelector& Selector : Selectors)
		{
			const FPCGAttributePropertyInputSelector FixedSelector = Selector.CopyAndFixLast(InData);
			TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, FixedSelector);

			if (!Accessor.IsValid())
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Attribute/property '{0}' not found - skipped."), FixedSelector.GetDisplayText()));
				continue;
			}

			if (!Packer->AddTypeToPacking(Accessor->GetUnderlyingType(), Packed))
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Attribute/property '{0}' is not a valid type - skipped."), FixedSelector.GetDisplayText()));
				continue;
			}

			Accessors.Add(MoveTemp(Accessor));
			Keys.Add(PCGExMetaHelpers::MakeConstKeys(InData, Partition.List.InstancePointIndices));
		}

		Packer->PackCustomDataFromAccessors(Partition.List, MoveTemp(Accessors), MoveTemp(Keys), Packed);
	}

	void FProcessor::OnAssetsLoaded()
	{
		StartSpawnLoop();
	}

	void FProcessor::StartSpawnLoop()
	{
		MainThreadLoop = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(Partitions.Num());

		// Weak capture: the loop is owned by this processor, a strong one would keep both alive forever.
		MainThreadLoop->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			if (Index == 0 && !This->PrepareSpawn())
			{
				This->TargetActor = nullptr;
			}

			if (This->TargetActor && !This->bReused)
			{
				This->SpawnPartition(Index);
			}

			if (Index == This->Partitions.Num() - 1)
			{
				This->OnAllPartitionsVisited();
			}
		};

		PCGEX_ASYNC_HANDLE_CHKD_VOID(TaskManager, MainThreadLoop)
	}

	bool FProcessor::PrepareSpawn()
	{
		TargetActor = Settings->TargetActor.Get() ? Settings->TargetActor.Get() : ExecutionContext->GetTypedExecutionTarget<AActor>();
		if (!TargetActor)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Invalid target actor."));
			return false;
		}

		// Same guards as the engine's spawner, plus the owner: a component left by an interrupted run never got its CRC.
		TArray<UPCGManagedISKMComponent*> Matches;
		PCGExManagedHelpers::FindManagedResources<UPCGManagedISKMComponent>(
			ExecutionContext->GetMutableComponent(), InputCrc,
			[&](const UPCGManagedISKMComponent* Resource)
			{
				const UInstancedSkinnedMeshComponent* ISKMC = Resource->GetComponent();
				return ISKMC && ISKMC->GetOwner() == TargetActor;
			},
			Matches);

		if (Matches.IsEmpty())
		{
			return true;
		}

		for (UPCGManagedISKMComponent* Resource : Matches)
		{
			if (!Resource->IsMarkedUnused() && Settings->bWarnOnIdenticalSpawn)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Identical Instanced Skinned Mesh Component spawn occurred. It may be beneficial to re-check graph logic for identical spawn conditions (same entries at the same points) or repeated nodes."));
			}

			// Registers the component: game thread only.
			Resource->MarkAsReused();
		}

		bReused = true;
		return true;
	}

	void FProcessor::SpawnPartition(const int32 PartitionIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnSkinnedMeshes::SpawnPartition);

		FPartition& Partition = Partitions[PartitionIndex];
		if (Partition.List.Instances.IsEmpty())
		{
			return;
		}

		// Loaded in OnPointsProcessingComplete; the blocking load only covers a load that failed there.
		USkinnedAsset* Asset = Partition.List.Descriptor.SkinnedAsset.Get();
		if (!Asset)
		{
			PCGExHelpers::LoadBlocking_AnyThread(Partition.List.Descriptor.SkinnedAsset.ToSoftObjectPath(), ExecutionContext);
			Asset = Partition.List.Descriptor.SkinnedAsset.Get();
		}

		if (!Asset)
		{
			if (!Settings->bQuietInvalidEntryWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Unable to load skinned asset '{0}'."), FText::FromString(Partition.List.Descriptor.SkinnedAsset.ToString())));
			}
			return;
		}

		// Blueprint packers run here, on the game thread; the float count feeds the component match below.
		if (!PacksNatively())
		{
			PackCustomData(Partition);
		}

		FPCGSkinnedMeshComponentBuilderParams Params;
		Params.Descriptor = Partition.List.Descriptor;
		Params.NumCustomDataFloats = Partition.PackedCustomData.NumCustomDataFloats;

		// A movable root makes the component movable by default
		if (const USceneComponent* RootComponent = TargetActor->GetRootComponent())
		{
			Params.Descriptor.Mobility = RootComponent->Mobility;
		}

		Params.SettingsCrc = Settings->GetSettingsCrc();
		ensure(Params.SettingsCrc.IsValid());

		// The managed-component match compares descriptors, which see neither tags (the engine folds their hash for that
		// reason) nor the materials and custom primitive data written per entry below: the pick hash is folded too, so
		// every (entry, pick) gets its own component. So is the input index: reuse is decided per input, and a component
		// two inputs filled would carry only one of their CRCs.
		if (Params.Descriptor.HasTags())
		{
			const uint32 TagsArrayHash = Params.Descriptor.ComponentTags.IsEmpty() ? 1 : GetTypeHash(Params.Descriptor.ComponentTags);
			const uint32 AdditionalTagsHash = Params.Descriptor.AdditionalCommaSeparatedTags.IsEmpty() ? 1 : GetTypeHash(Params.Descriptor.AdditionalCommaSeparatedTags);
			Params.SettingsCrc.Combine(HashCombine(TagsArrayHash, AdditionalTagsHash));
		}
		Params.SettingsCrc.Combine(GetTypeHash(Partition.Hash));
		Params.SettingsCrc.Combine(static_cast<uint32>(BatchIndex));

		UPCGManagedISKMComponent* ManagedComponent = UPCGActorHelpers::GetOrCreateManagedABMC(TargetActor, ExecutionContext->ExecutionSource.Get(), Params, ExecutionContext);
		if (!ManagedComponent)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FText::Format(FTEXT("Could not create an Instanced Skinned Mesh Component for '{0}'."), FText::FromString(Partition.List.Descriptor.SkinnedAsset.ToString())));
			return;
		}

		Context->TouchedResources.Add(ManagedComponent);
		OwnedResources.Add(ManagedComponent);

		UInstancedSkinnedMeshComponent* ISKMC = ManagedComponent->GetComponent();
		check(ISKMC);

		// Re-applied on a matched component too: MarkAsUsed resets its instances, not its materials or primitive data.
		if (bApplyMaterials)
		{
			Partition.Entry->ApplyMaterials(Partition.MaterialPick, ISKMC);
		}

		if (!Partition.CustomPrimitiveData.IsEmpty())
		{
			ISKMC->SetDefaultCustomPrimitiveDataFloatArray(0, Partition.CustomPrimitiveData);
		}

		const int32 NumCustomDataFloats = Partition.PackedCustomData.NumCustomDataFloats;
		check((ISKMC->GetNumCustomDataFloats() == 0 && ISKMC->GetInstanceCount() == 0) || ISKMC->GetNumCustomDataFloats() == NumCustomDataFloats);
		ISKMC->SetNumCustomDataFloats(NumCustomDataFloats);

		const int32 NewInstanceCount = Partition.List.Instances.Num();

		TArray<FTransform> InstanceTransforms;
		TArray<int32> InstanceAnimationIndices;
		InstanceTransforms.Reserve(NewInstanceCount);
		InstanceAnimationIndices.Reserve(NewInstanceCount);

		for (const FPCGSkinnedMeshInstance& Instance : Partition.List.Instances)
		{
			InstanceTransforms.Add(Instance.Transform);
			InstanceAnimationIndices.Add(Instance.AnimationIndex);
		}

		const TArray<FPrimitiveInstanceId> NewIds = ISKMC->AddInstances(InstanceTransforms, InstanceAnimationIndices, /*bShouldReturnIds=*/true, /*bWorldSpace=*/true);

		if (NumCustomDataFloats > 0)
		{
			for (int32 NewIndex = 0; NewIndex < NewInstanceCount; ++NewIndex)
			{
				ISKMC->SetCustomData(NewIds[NewIndex], MakeArrayView(&Partition.PackedCustomData.CustomData[NewIndex * NumCustomDataFloats], NumCustomDataFloats));
			}
		}

		ISKMC->UpdateBounds();
		ISKMC->OptimizeInstanceData();

		PCGE_LOG_C(Verbose, LogOnly, ExecutionContext, FText::Format(
			           FTEXT("Added {0} instances of '{1}' on actor '{2}'"),
			           NewInstanceCount, FText::FromString(Asset->GetFName().ToString()), FText::FromString(TargetActor->GetFName().ToString())));
	}

	void FProcessor::OnAllPartitionsVisited()
	{
		// Only now: a resource whose run got interrupted keeps no CRC, so it can never be taken for a complete one.
		if (!InputCrc.IsValid())
		{
			return;
		}

		for (UPCGManagedISKMComponent* Resource : OwnedResources)
		{
			Resource->SetCrc(InputCrc);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
