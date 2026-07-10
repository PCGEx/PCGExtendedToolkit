// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorCascade.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExRandomHelpers.h"

#pragma region FPCGExEntryCascadePickerOp

bool FPCGExEntryCascadePickerOp::PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection)
{
	if (!FPCGExEntryPickerOperation::PrepareForData(InContext, InDataFacade, InTarget, InOwningCollection))
	{
		return false;
	}

	// Composite shared data is built by the cascade factory whenever children exist -- both the
	// cached and the direct (no-cache) helper paths route through BuildSharedData.
	const TSharedPtr<FPCGExCascadeSharedData> Composite = StaticCastSharedPtr<FPCGExCascadeSharedData>(SharedData);
	if (!Composite || Composite->PerChild.Num() != ChildFactories.Num())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector Modifier : Cascade -- no child selectors to dispatch to."))
		return false;
	}

	ChildOps.Reset();
	ChildSalts.Reset();

	for (int32 i = 0; i < ChildFactories.Num(); ++i)
	{
		const UPCGExSelectorFactoryData* ChildFactory = ChildFactories[i];
		if (!ChildFactory)
		{
			continue;
		}

		TSharedPtr<FPCGExEntryPickerOperation> ChildOp = ChildFactory->CreateEntryOperation(InContext);
		if (!ChildOp)
		{
			continue;
		}

		ChildOp->SharedData = Composite->PerChild[i];
		if (!ChildOp->PrepareForData(InContext, InDataFacade, InTarget, InOwningCollection))
		{
			// Child couldn't bind (missing attribute, no valid entries for its criteria...).
			// Skip it -- the cascade semantic is precisely "fall through to the next".
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::Format(FTEXT("Selector Modifier : Cascade -- child selector {0} failed to initialize and will be skipped."), FText::AsNumber(i)));
			continue;
		}

		ChildOps.Add(ChildOp);
		ChildSalts.Add(i);
	}

	if (ChildOps.IsEmpty())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Selector Modifier : Cascade -- no child selector could initialize; nothing to pick from."));
		return false;
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryCascadePickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	bool bAny = false;
	TSharedPtr<FPCGExCascadeScratch> Composite = MakeShared<FPCGExCascadeScratch>();
	Composite->PerChild.SetNum(ChildOps.Num());
	for (int32 i = 0; i < ChildOps.Num(); ++i)
	{
		Composite->PerChild[i] = ChildOps[i]->CreateScratchForScope(MaxPointsInScope);
		bAny |= Composite->PerChild[i].IsValid();
	}
	return bAny ? Composite : nullptr;
}

int32 FPCGExEntryCascadePickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const FPCGExCascadeScratch* Composite = static_cast<const FPCGExCascadeScratch*>(Scratch);

	for (int32 i = 0; i < ChildOps.Num(); ++i)
	{
		// Child 0 gets the raw seed (single-child cascade == that child standalone); siblings
		// are salted so their random rolls don't correlate on the same point.
		const int32 ChildSeed = ChildSalts[i] == 0 ? Seed : PCGExRandomHelpers::GetSeed(Seed, ChildSalts[i]);
		FPCGExPickerScratchBase* ChildScratch = Composite ? Composite->PerChild[i].Get() : nullptr;

		const int32 Raw = ChildOps[i]->Pick(PointIndex, ChildSeed, ChildScratch);
		if (Raw != -1)
		{
			return Raw;
		}
	}

	return -1;
}

int32 FPCGExEntryCascadePickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const FPCGExCascadeScratch* Composite = static_cast<const FPCGExCascadeScratch*>(Scratch);

	for (int32 i = 0; i < ChildOps.Num(); ++i)
	{
		const int32 ChildSeed = ChildSalts[i] == 0 ? Seed : PCGExRandomHelpers::GetSeed(Seed, ChildSalts[i]);
		FPCGExPickerScratchBase* ChildScratch = Composite ? Composite->PerChild[i].Get() : nullptr;

		const int32 Raw = ChildOps[i]->PickFiltered(PointIndex, ChildSeed, InAvailability, ChildScratch);
		if (Raw != -1)
		{
			return Raw;
		}
	}

	return -1;
}

#pragma endregion

#pragma region UPCGExSelectorCascadeFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorCascadeFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	if (Children.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr<FPCGExEntryCascadePickerOp> NewOp = MakeShared<FPCGExEntryCascadePickerOp>();
	NewOp->ChildFactories.Reserve(Children.Num());
	for (const TObjectPtr<const UPCGExSelectorFactoryData>& Child : Children)
	{
		NewOp->ChildFactories.Add(Child.Get());
	}
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorCascadeFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target || Children.IsEmpty())
	{
		return nullptr;
	}

	// Always return a composite when children exist (even if every slot is null) so the op can
	// distinguish "no children" from "children that don't use shared data".
	TSharedPtr<FPCGExCascadeSharedData> NewShared = MakeShared<FPCGExCascadeSharedData>();
	NewShared->PerChild.SetNum(Children.Num());
	for (int32 i = 0; i < Children.Num(); ++i)
	{
		if (const UPCGExSelectorFactoryData* Child = Children[i].Get())
		{
			NewShared->PerChild[i] = Child->BuildSharedData(Collection, Target);
		}
	}
	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorCascadeFactoryProviderSettings

TArray<FPCGPinProperties> UPCGExSelectorCascadeFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExCollections::Labels::SourceSelectorsLabel, "Child selectors, tried in Priority order (lowest first).", Required, FPCGExDataTypeInfoSelector::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExSelectorCascadeFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGExSelectorFactoryData>> Children;
	if (!PCGExFactories::GetInputFactories(InContext, PCGExCollections::Labels::SourceSelectorsLabel, Children, {FPCGExDataTypeInfoSelector::AsId()}, true))
	{
		return nullptr;
	}

	UPCGExSelectorCascadeFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorCascadeFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Children = MoveTemp(Children);
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorCascadeFactoryProviderSettings::GetDisplayName() const
{
	return TEXT("Modify : Cascade");
}
#endif

#pragma endregion
