// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorAntiRepeat.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Selectors/PCGExSelectorCascade.h"
#include "Selectors/PCGExSelectorHelpers.h"

#pragma region FPCGExEntryAntiRepeatPickerOp

bool FPCGExEntryAntiRepeatPickerOp::PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection)
{
	if (!FPCGExEntryPickerOperation::PrepareForData(InContext, InDataFacade, InTarget, InOwningCollection))
	{
		return false;
	}

	if (ChildFactory)
	{
		ChildOp = ChildFactory->CreateEntryOperation(InContext);
		if (ChildOp)
		{
			// Composite shared data mirrors Cascade (single slot).
			if (const TSharedPtr<FPCGExCascadeSharedData> Composite = StaticCastSharedPtr<FPCGExCascadeSharedData>(SharedData);
				Composite && Composite->PerChild.Num() == 1)
			{
				ChildOp->SharedData = Composite->PerChild[0];
			}
			if (!ChildOp->PrepareForData(InContext, InDataFacade, InTarget, InOwningCollection))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Selector Modifier : Anti-Repeat -- inner selector failed to initialize; falling back to weighted random."));
				ChildOp = nullptr;
			}
		}
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryAntiRepeatPickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	// Scratch is required for history -- always produce one.
	TSharedPtr<FPCGExAntiRepeatScratch> Scratch = MakeShared<FPCGExAntiRepeatScratch>();
	Scratch->Ring.Reserve(HistoryLength);
	if (ChildOp)
	{
		Scratch->ChildScratch = ChildOp->CreateScratchForScope(MaxPointsInScope);
	}
	return Scratch;
}

void FPCGExEntryAntiRepeatPickerOp::RecordPick(FPCGExAntiRepeatScratch& S, const int32 PointIndex, const int32 Raw) const
{
	if (S.LastPointIndex == PointIndex && S.LastSlot != INDEX_NONE)
	{
		// Same point re-invoked by a wrapper retry -- replace its record, don't consume a new slot.
		S.Ring[S.LastSlot] = Raw;
		return;
	}

	S.LastPointIndex = PointIndex;
	if (S.Ring.Num() < HistoryLength)
	{
		S.LastSlot = S.Ring.Add(Raw);
	}
	else
	{
		S.Ring[S.Head] = Raw;
		S.LastSlot = S.Head;
		S.Head = (S.Head + 1) % HistoryLength;
	}
}

int32 FPCGExEntryAntiRepeatPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	// Without a per-scope scratch there is no history to check against -- the pick degrades
	// gracefully to the inner selector. All wired consumers (StagingDistribute, SplineMesh)
	// pass scratch; scratch-less callers (unit tests, legacy paths) get plain inner picks.
	FPCGExAntiRepeatScratch* S = static_cast<FPCGExAntiRepeatScratch*>(Scratch);

	auto InnerPick = [&](const int32 InSeed)
	{
		return ChildOp ? ChildOp->Pick(PointIndex, InSeed, S ? S->ChildScratch.Get() : nullptr) : Target->GetPickRandomWeighted(InSeed);
	};

	int32 Raw = InnerPick(Seed);

	if (S && Raw != -1)
	{
		for (int32 Attempt = 1; Attempt < MaxAttempts && S->Ring.Contains(Raw); ++Attempt)
		{
			const int32 Salted = PCGExRandomHelpers::GetSeed(Seed, Attempt);
			const int32 Retry = InnerPick(Salted);
			if (Retry == -1)
			{
				break; // Inner ran dry on the salted seed -- keep the previous valid pick.
			}
			Raw = Retry;
		}

		// Record the final pick (repeat or not) so runs of forced repeats still age out.
		RecordPick(*S, PointIndex, Raw);
	}

	return Raw;
}

int32 FPCGExEntryAntiRepeatPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	FPCGExAntiRepeatScratch* S = static_cast<FPCGExAntiRepeatScratch*>(Scratch);

	auto InnerPick = [&](const int32 InSeed)
	{
		return ChildOp
			? ChildOp->PickFiltered(PointIndex, InSeed, InAvailability, S ? S->ChildScratch.Get() : nullptr)
			: PCGExCollections::Selectors::FilteredWeightedPick(Target, InAvailability, InSeed);
	};

	int32 Raw = InnerPick(Seed);

	if (S && Raw != -1)
	{
		for (int32 Attempt = 1; Attempt < MaxAttempts && S->Ring.Contains(Raw); ++Attempt)
		{
			const int32 Salted = PCGExRandomHelpers::GetSeed(Seed, Attempt);
			const int32 Retry = InnerPick(Salted);
			if (Retry == -1)
			{
				break;
			}
			Raw = Retry;
		}

		RecordPick(*S, PointIndex, Raw);
	}

	return Raw;
}

#pragma endregion

#pragma region UPCGExSelectorAntiRepeatFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorAntiRepeatFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryAntiRepeatPickerOp> NewOp = MakeShared<FPCGExEntryAntiRepeatPickerOp>();
	NewOp->HistoryLength = FMath::Max(1, Config.HistoryLength);
	NewOp->MaxAttempts = FMath::Max(1, Config.MaxAttempts);
	NewOp->ChildFactory = Child.Get();
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorAntiRepeatFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	const UPCGExSelectorFactoryData* ChildPtr = Child.Get();
	if (!Collection || !Target || !ChildPtr)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExCascadeSharedData> NewShared = MakeShared<FPCGExCascadeSharedData>();
	NewShared->PerChild.SetNum(1);
	NewShared->PerChild[0] = ChildPtr->BuildSharedData(Collection, Target);
	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorAntiRepeatFactoryProviderSettings

TArray<FPCGPinProperties> UPCGExSelectorAntiRepeatFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExCollections::Labels::SourceSelectorLabel, "Optional inner selector. When unconnected, Anti-Repeat wraps a plain weighted-random pick. If multiple are connected, the lowest Priority wins.", Normal, FPCGExDataTypeInfoSelector::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExSelectorAntiRepeatFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGExSelectorFactoryData>> Children;
	PCGExFactories::GetInputFactories(InContext, PCGExCollections::Labels::SourceSelectorLabel, Children, {FPCGExDataTypeInfoSelector::AsId()}, false);

	UPCGExSelectorAntiRepeatFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorAntiRepeatFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	if (!Children.IsEmpty())
	{
		if (Children.Num() > 1)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Selector Modifier : Anti-Repeat -- multiple inner selectors connected; using the lowest Priority. Use Selector Modifier : Cascade to combine several."));
		}
		NewFactory->Child = Children[0];
	}
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorAntiRepeatFactoryProviderSettings::GetDisplayName() const
{
	return FString::Printf(TEXT("Modify : Anti-Repeat (%d)"), Config.HistoryLength);
}
#endif

#pragma endregion
