// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExValueHashFilter.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExValueHashFilterFactory::WantsPreparation(FPCGExContext* InContext)
{
	return true;
}

PCGExFactories::EPreparationResult UPCGExValueHashFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success) { return Result; }

	PCGExData::TryGetFacades(InContext, FName("Sets"), SetSources, false, true);

	if (SetSources.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error) { PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No valid set found")) }
		return PCGExFactories::EPreparationResult::MissingData;
	}

	PCGExArrayHelpers::InitArray(Hashes, SetSources.Num());

	TWeakPtr<FPCGContextHandle> CtxHandle = InContext->GetOrCreateHandle();
	PCGEX_ASYNC_GROUP_CHKD_RET(TaskManager, GrabUniqueValues, PCGExFactories::EPreparationResult::Fail)

	GrabUniqueValues->OnCompleteCallback = [CtxHandle, this]()
	{
		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		if (Config.Mode == EPCGExValueHashMode::Merged)
		{
			TSet<PCGExValueHash>& MergedSet = Hashes[0];
			for (TSet<PCGExValueHash>& H : Hashes) { MergedSet.Append(H); }
			Hashes.SetNum(1);

			if (Hashes[0].IsEmpty())
			{
				if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error) { PCGEX_LOG_MISSING_INPUT(SharedContext.Get(), FTEXT("Merged sets are empty")) }
				PrepResult = PCGExFactories::EPreparationResult::MissingData;
			}
		}
		else
		{
			int32 WriteIndex = 0;
			for (TSet<PCGExValueHash>& H : Hashes)
			{
				if (H.IsEmpty()) { continue; }
				Hashes[WriteIndex++] = MoveTemp(H);
			}

			Hashes.SetNum(WriteIndex);

			if (Hashes.IsEmpty())
			{
				if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error) { PCGEX_LOG_MISSING_INPUT(SharedContext.Get(), FTEXT("Merged sets are empty")) }
				PrepResult = PCGExFactories::EPreparationResult::MissingData;
			}
		}

		SetSources.Empty();
	};

	GrabUniqueValues->OnIterationCallback = [CtxHandle, this](const int32 Index, const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPolyPathFilterFactory::CreatePolyPath);

		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		Hashes[Index] = TSet<PCGExValueHash>();
		TSet<PCGExValueHash>& UniqueValues = Hashes[Index];

		TSharedPtr<PCGExData::FFacade> SourceFacade = SetSources[Index];

		FPCGAttributeIdentifier Identifier;

		if (Config.SetAttributeName.IsNone())
		{
			TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(SourceFacade->GetIn()->Metadata);
			if (Infos->Attributes.IsEmpty()) { return; }

			Identifier = Infos->Identities[0].Identifier;
		}
		else
		{
			Identifier = PCGExMetaHelpers::GetAttributeIdentifier(Config.SetAttributeName, SourceFacade->GetIn());
		}

		TSharedPtr<PCGExData::IBuffer> Buffer = SourceFacade->GetDefaultReadable(Identifier, PCGExData::EIOSide::In, false);

		if (!Buffer)
		{
			PCGEX_LOG_INVALID_ATTR_C(SharedContext.Get(), SetAttributeName, Identifier.Name)
			return;
		}

		const int32 NumValues = Buffer->GetNumValues(PCGExData::EIOSide::In);
		for (int i = 0; i < NumValues; i++) { UniqueValues.Add(Buffer->ReadValueHash(i)); }
	};

	GrabUniqueValues->StartIterations(SetSources.Num(), 1);

	return Result;
}

bool UPCGExValueHashFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExValueHashFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FValueHashFilter>(this);
}

void UPCGExValueHashFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<double>(InContext, Config.OperandA);
}

bool UPCGExValueHashFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData)) { return false; }
	InContext->AddConsumableAttributeName(Config.OperandA);
	return true;
}

bool PCGExPointFilter::FValueHashFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade)) { return false; }

	bInvert = TypedFilterFactory->Config.bInvert;
	// In Merged mode, all sets are combined into one, so "any vs all" is meaningless -- force bAnyPass = true.
	bAnyPass = TypedFilterFactory->Config.Mode == EPCGExValueHashMode::Individual ? TypedFilterFactory->Config.Inclusion == EPCGExValueHashSetInclusionMode::Any : true;

	FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(TypedFilterFactory->Config.OperandA, PointDataFacade->GetIn());
	OperandA = InPointDataFacade->GetDefaultReadable(Identifier, PCGExData::EIOSide::In, true);

	if (!OperandA)
	{
		PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	return true;
}

bool PCGExPointFilter::FValueHashFilter::Test(const int32 PointIndex) const
{
	const PCGExValueHash H = OperandA->ReadValueHash(PointIndex);
	bool bPass = false;
	if (bAnyPass)
	{
		for (const TSet<PCGExValueHash>& HashSet : *Hashes)
		{
			if (HashSet.Contains(H))
			{
				bPass = true;
				break;
			}
		}
	}
	else
	{
		bPass = true;
		for (const TSet<PCGExValueHash>& HashSet : *Hashes)
		{
			if (!HashSet.Contains(H))
			{
				bPass = false;
				break;
			}
		}
	}

	return bPass ? !bInvert : bInvert;
}

bool PCGExPointFilter::FValueHashFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double H = 0;

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, H, PCGEX_QUIET_HANDLING)) { PCGEX_QUIET_HANDLING_RET }

	bool bPass = false;
	if (bAnyPass)
	{
		for (const TSet<PCGExValueHash>& HashSet : *Hashes)
		{
			if (HashSet.Contains(H))
			{
				bPass = true;
				break;
			}
		}
	}
	else
	{
		bPass = true;
		for (const TSet<PCGExValueHash>& HashSet : *Hashes)
		{
			if (!HashSet.Contains(H))
			{
				bPass = false;
				break;
			}
		}
	}

	return bPass ? !bInvert : bInvert;
}

TArray<FPCGPinProperties> UPCGExValueHashFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(FName("Sets"), "Data from value set will be extracted", Required)
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(ValueHash)

#if WITH_EDITOR
FString UPCGExValueHashFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Is ") + Config.OperandA.ToString();
	if (Config.Mode == EPCGExValueHashMode::Individual || Config.Inclusion == EPCGExValueHashSetInclusionMode::Any)
	{
		DisplayName += TEXT(" in any set");
	}
	else
	{
		DisplayName += TEXT(" in all set");
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
