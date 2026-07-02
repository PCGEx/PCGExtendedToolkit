// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClipper2Boolean.h"

#include "Clipper2Lib/clipper.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2BooleanElement"
#define PCGEX_NAMESPACE Clipper2Boolean

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExClipper2BooleanSettings::GetPreconfiguredInfo() const
{
	const TSet<EPCGExClipper2BooleanOp> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExClipper2BooleanOp>(ValuesToSkip, FTEXT("Clipper2 Boolean : {0}"));
}
#endif

void UPCGExClipper2BooleanSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExClipper2BooleanOp>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Operation = static_cast<EPCGExClipper2BooleanOp>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

PCGEX_INITIALIZE_ELEMENT(Clipper2Boolean)

bool UPCGExClipper2BooleanSettings::WantsOperands() const
{
	return Operation != EPCGExClipper2BooleanOp::Union || bUseOperandPin;
}

FPCGExGeo2DProjectionDetails UPCGExClipper2BooleanSettings::GetProjectionDetails() const
{
	return ProjectionDetails;
}

bool UPCGExClipper2BooleanSettings::SupportOpenOperandPaths() const
{
	return false;
}

#if WITH_EDITOR
FString UPCGExClipper2BooleanSettings::GetDisplayName() const
{
	switch (Operation)
	{
	case EPCGExClipper2BooleanOp::Intersection:
		return TEXT("PCGEx | Clipper2 : Intersection");
	default:
	case EPCGExClipper2BooleanOp::Union:
		return TEXT("PCGEx | Clipper2 : Union");
	case EPCGExClipper2BooleanOp::Difference:
		return TEXT("PCGEx | Clipper2 : Difference");
	case EPCGExClipper2BooleanOp::Xor:
		return TEXT("PCGEx | Clipper2 : Xor");
	}
}
#endif

void FPCGExClipper2BooleanContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	const UPCGExClipper2BooleanSettings* Settings = GetInputSettings<UPCGExClipper2BooleanSettings>();

	if (!Group->IsValid())
	{
		return;
	}

	// Create clipper and set up ZCallback for intersection tracking
	PCGExClipper2Lib::Clipper64 Clipper;
	Clipper.SetZCallback(Group->CreateZCallback());

	// Add subject paths
	if (!Group->SubjectPaths.empty())
	{
		Clipper.AddSubject(Group->SubjectPaths);
	}
	if (!Group->OpenSubjectPaths.empty())
	{
		Clipper.AddOpenSubject(Group->OpenSubjectPaths);
	}

	// Add operand paths as clips if available
	if (Group->OperandPaths && !Group->OperandPaths->empty())
	{
		Clipper.AddClip(*Group->OperandPaths);
	}
	if (Group->OpenOperandPaths && !Group->OpenOperandPaths->empty())
	{
		Clipper.AddClip(*Group->OpenOperandPaths);
	}

	// Determine clip type
	PCGExClipper2Lib::ClipType ClipType;
	switch (Settings->Operation)
	{
	case EPCGExClipper2BooleanOp::Intersection:
		ClipType = PCGExClipper2Lib::ClipType::Intersection;
		break;
	case EPCGExClipper2BooleanOp::Union:
		ClipType = PCGExClipper2Lib::ClipType::Union;
		break;
	case EPCGExClipper2BooleanOp::Difference:
		ClipType = PCGExClipper2Lib::ClipType::Difference;
		break;
	case EPCGExClipper2BooleanOp::Xor:
		ClipType = PCGExClipper2Lib::ClipType::Xor;
		break;
	default:
		ClipType = PCGExClipper2Lib::ClipType::Union;
		break;
	}

	// Execute the boolean operation
	PCGExClipper2Lib::Paths64 ClosedResults;
	PCGExClipper2Lib::Paths64 OpenResults;

	if (!Clipper.Execute(ClipType, PCGExClipper2::ConvertFillRule(Settings->FillRule), ClosedResults, OpenResults))
	{
		PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Clipper2 boolean operation failed; the group was skipped."));
		return;
	}

	if (!ClosedResults.empty())
	{
		TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
		OutputPaths64(ClosedResults, Group, OutputPaths, true, 0);
	}

	if (Settings->OpenPathsOutput != EPCGExClipper2OpenPathOutput::Ignore && !OpenResults.empty())
	{
		TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
		OutputPaths64(OpenResults, Group, OutputPaths, false, 1);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
