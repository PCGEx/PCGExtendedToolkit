// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExWriteGUID.h"


#include "PCGComponent.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/Guid.h"

#define LOCTEXT_NAMESPACE "PCGExWriteGUIDElement"
#define PCGEX_NAMESPACE WriteGUID

PCGEX_SETTING_VALUE_IMPL(FPCGExGUIDDetails, UniqueKey, int32, UniqueKeyInput, UniqueKeyAttribute, UniqueKeyConstant)

bool FPCGExGUIDDetails::Init(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade>& InFacade)
{
	switch (Format)
	{
	default: case EPCGExGUIDFormat::Digits: GUIDFormat = EGuidFormats::Digits;
		break;
	case EPCGExGUIDFormat::DigitsLower: GUIDFormat = EGuidFormats::DigitsLower;
		break;
	case EPCGExGUIDFormat::DigitsWithHyphens: GUIDFormat = EGuidFormats::DigitsWithHyphens;
		break;
	case EPCGExGUIDFormat::DigitsWithHyphensLower: GUIDFormat = EGuidFormats::DigitsWithHyphensLower;
		break;
	case EPCGExGUIDFormat::DigitsWithHyphensInBraces: GUIDFormat = EGuidFormats::DigitsWithHyphensInBraces;
		break;
	case EPCGExGUIDFormat::DigitsWithHyphensInParentheses: GUIDFormat = EGuidFormats::DigitsWithHyphensInParentheses;
		break;
	case EPCGExGUIDFormat::HexValuesInBraces: GUIDFormat = EGuidFormats::HexValuesInBraces;
		break;
	case EPCGExGUIDFormat::UniqueObjectGuid: GUIDFormat = EGuidFormats::UniqueObjectGuid;
		break;
	case EPCGExGUIDFormat::Short: GUIDFormat = EGuidFormats::Short;
		break;
	case EPCGExGUIDFormat::Base36Encoded: GUIDFormat = EGuidFormats::Base36Encoded;
		break;
	}

	AdjustedGridHashCollision = PCGEx::SafeTolerance(GridHashCollision);
	AdjustedPositionHashCollision = PCGEx::SafeTolerance(PositionHashCollision);

	bUseIndex = (Uniqueness & static_cast<uint8>(EPCGExGUIDUniquenessFlags::Index)) != 0;
	bUseSeed = (Uniqueness & static_cast<uint8>(EPCGExGUIDUniquenessFlags::Seed)) != 0;
	bUsePosition = (Uniqueness & static_cast<uint8>(EPCGExGUIDUniquenessFlags::Position)) != 0;

	UniqueKeyReader = GetValueSettingUniqueKey();
	if (!UniqueKeyReader->Init(InFacade)) { return false; }

	const uint32 BaseUniqueKey = UniqueKeyReader->IsConstant() ? 0 : UniqueKeyConstant;
	if ((Uniqueness & static_cast<uint8>(EPCGExGUIDUniquenessFlags::Grid)) != 0)
	{
		const FBox RefBounds = PCGHelpers::GetGridBounds(InContext->GetTargetActor(InFacade->Source->GetIn()), InContext->GetComponent());
		GridHash = HashCombine(BaseUniqueKey, PCGEx::GH3(RefBounds.Min, AdjustedGridHashCollision));
		GridHash = HashCombine(GridHash, PCGEx::GH3(RefBounds.Max, AdjustedGridHashCollision));
	}
	else
	{
		GridHash = BaseUniqueKey;
	}

	DefaultGUID = FGuid(GridHash, 0, 0, 0);

	return true;
}

void FPCGExGUIDDetails::GetGUID(const int32 Index, const PCGExData::FConstPoint& InPoint, FGuid& OutGUID) const
{
	const uint32 SeededBase = bUseSeed ? InPoint.Data->GetSeed(InPoint.Index) : 0;
	OutGUID = FGuid(
		GridHash, bUseIndex ? Index : -1,
		UniqueKeyReader->IsConstant() ? HashCombine(SeededBase, static_cast<uint32>(UniqueKeyReader->Read(Index))) : SeededBase,
		bUsePosition ? PCGEx::GH3(InPoint.GetLocation() + PositionHashOffset, AdjustedPositionHashCollision) : 0);
}

PCGEX_INITIALIZE_ELEMENT(WriteGUID)

PCGExData::EIOInit UPCGExWriteGUIDSettings::GetMainDataInitializationPolicy() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }

PCGEX_ELEMENT_BATCH_POINT_IMPL(WriteGUID)

bool FPCGExWriteGUIDElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(WriteGUID)

	PCGEX_VALIDATE_NAME(Settings->Config.OutputAttributeName)

	return true;
}

bool FPCGExWriteGUIDElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteGUIDElement::Execute);

	PCGEX_CONTEXT(WriteGUID)
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

	return Context->TryComplete();
}

namespace PCGExWriteGUID
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteGUID::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		Config = Settings->Config;

		if (!Config.Init(Context, PointDataFacade)) { return false; }

		if (Config.OutputType == EPCGExGUIDOutputType::Integer)
		{
			IntegerGUIDWriter = PointDataFacade->GetWritable<int32>(Config.OutputAttributeName, -1, Config.bAllowInterpolation, PCGExData::EBufferInit::New);
		}
		else
		{
			StringGUIDWriter = PointDataFacade->GetWritable<FString>(Config.OutputAttributeName, TEXT(""), Config.bAllowInterpolation, PCGExData::EBufferInit::New);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::WriteGUID::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			FGuid GUID = FGuid();
			Config.GetGUID(Index, PointDataFacade->GetInPoint(Index), GUID);

			if (IntegerGUIDWriter) { IntegerGUIDWriter->SetValue(Index, GetTypeHash(GUID.ToString(Config.GUIDFormat))); }
			else { StringGUIDWriter->SetValue(Index, GUID.ToString(Config.GUIDFormat)); }
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
