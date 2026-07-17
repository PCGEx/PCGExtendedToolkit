// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExNearestPointCheckFilter.h"

#include "PCGExVersion.h"
#include "HAL/PlatformAtomics.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExTargetsHandler.h"


#define LOCTEXT_NAMESPACE "PCGExNearestPointCheckFilter"
#define PCGEX_NAMESPACE NearestPointCheckFilter

bool UPCGExNearestPointCheckFilterFactory::Init(FPCGExContext* InContext)
{
	NearestConfig = &Config;
	return Super::Init(InContext);
}

void UPCGExNearestPointCheckFilterFactory::RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	// Sub-filters read target points, so preload their attribute dependencies on each target facade.
	PCGExPointFilter::RegisterBuffersDependencies(InContext, FilterFactories, FacadePreloader);
}

bool UPCGExNearestPointCheckFilterFactory::BuildTargetCaches(FPCGExContext* InContext)
{
	TargetFilterManagers = MakeShared<TArray<TSharedPtr<PCGExPointFilter::FManager>>>();
	TargetFilterManagers->Reserve(TargetsHandler->Num());

	// "Any In Range" reuses verdicts across overlapping source ranges; Nearest mode needs no cache.
	const bool bAnyMode = Config.Mode == EPCGExNearestCheckMode::Any;
	if (bAnyMode)
	{
		TargetResultCache = MakeShared<TArray<TArray<int8>>>();
		TargetResultCache->Reserve(TargetsHandler->Num());
	}

	const bool bError = TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32 TargetIndex, bool& bBreak)
	{
		// Always add (even on failure) so the array stays index-aligned with the target's IO.
		TSharedPtr<PCGExPointFilter::FManager> Manager = MakeShared<PCGExPointFilter::FManager>(Target);
		Manager->SetSupportedTypes(&PCGExFactories::PointFilters());

		const bool bManagerOk = Manager->Init(InContext, FilterFactories);
		TargetFilterManagers->Add(bManagerOk ? Manager : nullptr);

		if (bAnyMode)
		{
			// Index-aligned slot; -1 == not yet evaluated. Left empty on manager failure (unused).
			TArray<int8>& Slot = TargetResultCache->Emplace_GetRef();
			if (bManagerOk) { Slot.Init(-1, Target->Source->GetNum()); }
		}

		if (!bManagerOk)
		{
			bBreak = true;
		}
	});

	return !bError;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNearestPointCheckFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FNearestPointCheckFilter>(this);
}

bool PCGExPointFilter::FNearestPointCheckFilter::InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!TargetFilterManagers || TargetFilterManagers->Num() != TargetsHandler->Num())
	{
		return false;
	}

	bAnyMode = TypedFilterFactory->Config.Mode == EPCGExNearestCheckMode::Any;
	if (bAnyMode && (!TargetResultCache || TargetResultCache->Num() != TargetsHandler->Num()))
	{
		return false;
	}

	bNoTargetResult = (TypedFilterFactory->Config.NoTargetFallback == EPCGExFilterFallback::Pass);
	return true;
}

bool PCGExPointFilter::FNearestPointCheckFilter::Test(const int32 PointIndex) const
{
	TSet<const UPCGData*> Scratch;
	bool bShortCircuit = false;
	bool bShortCircuitResult = false;
	const TSet<const UPCGData*>* ExcludePtr = ResolveExclude(PointIndex, Scratch, bShortCircuit, bShortCircuitResult);
	if (bShortCircuit)
	{
		return bShortCircuitResult;
	}

	const PCGExData::FConstPoint SourcePt = PointDataFacade->GetInPoint(PointIndex);
	const double MaxDist = GetMaxDistance(PointIndex);

	if (bAnyMode)
	{
		// "Any In Range" requires a positive range: no distance means no candidates.
		if (MaxDist <= 0)
		{
			return bNoTargetResult;
		}

		const double MaxDistSquared = MaxDist * MaxDist;
		// Pad the box for bounds-based source modes (Center needs none); the GetDistSquared trim stays exact.
		const double QueryExtent = bInflateQueryBounds ? MaxDist + SourcePt.GetScaledExtents().Length() : MaxDist;
		const FBoxCenterAndExtent QueryBounds(SourcePt.GetLocation(), FVector(QueryExtent));

		TArray<TArray<int8>>& Cache = *TargetResultCache;
		bool bAnyCandidate = false;
		bool bAnyPass = false;

		TargetsHandler->FindElementsWithBoundsTest(
			QueryBounds,
			[&](const PCGExData::FConstPoint& Target)
			{
				if (bAnyPass) { return; } // first pass wins
				if (TargetsHandler->GetDistSquared(SourcePt, Target) > MaxDistSquared) { return; } // trim AABB to true radius

				bAnyCandidate = true;

				// Cache the verdict. Benign race: deterministic value, atomic store, single-byte read.
				int8 Cached = Cache[Target.IO][Target.Index];
				if (Cached == -1)
				{
					Cached = (*TargetFilterManagers)[Target.IO]->Test(Target.Index) ? 1 : 0;
					FPlatformAtomics::AtomicStore(&Cache[Target.IO][Target.Index], Cached);
				}

				if (Cached == 1) { bAnyPass = true; }
			},
			ExcludePtr);

		if (!bAnyCandidate)
		{
			// No target in range. Not affected by Invert.
			return bNoTargetResult;
		}

		return TypedFilterFactory->Config.bInvert ? !bAnyPass : bAnyPass;
	}

	// Nearest mode: find the single closest target (optionally within range) and test it.
	const PCGExData::FConstPoint TargetPt = FindNearestInRange(SourcePt, MaxDist, ExcludePtr);
	if (!TargetPt.IsValid())
	{
		// No qualifying target. The fallback is explicit and not affected by Invert.
		return bNoTargetResult;
	}

	// Test the nearest target via its dataset's manager (TargetPt.IO). FManager::Test is const/stateless
	// here, so concurrent calls across source instances are safe.
	const TSharedPtr<PCGExPointFilter::FManager>& Manager = (*TargetFilterManagers)[TargetPt.IO];
	check(Manager);
	const bool bPass = Manager->Test(TargetPt.Index);

	return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
}

TArray<FPCGPinProperties> UPCGExNearestPointCheckFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Target points to find the nearest of."), Required)
	PCGEX_PIN_FILTERS(PCGExFilters::Labels::SourcePointFiltersLabel, TEXT("Filters to run on the candidate target point(s)."), Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

UPCGExFactoryData* UPCGExNearestPointCheckFilterProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNearestPointCheckFilterFactory* NewFactory = InContext->ManagedObjects->New<UPCGExNearestPointCheckFilterFactory>();

	NewFactory->InitializationFailurePolicy = InitializationFailurePolicy;
	NewFactory->MissingDataPolicy = MissingDataPolicy;
	NewFactory->Config = Config;

	Super::CreateFactory(InContext, NewFactory);

	if (!PCGExFactories::GetInputFactories(InContext, PCGExFilters::Labels::SourcePointFiltersLabel, NewFactory->FilterFactories, PCGExFactories::PointFilters()))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	if (!NewFactory->Init(InContext))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	return NewFactory;
}

#if WITH_EDITOR
void UPCGExNearestPointCheckFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNearestPointCheckFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExNearestPointCheckFilterProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
