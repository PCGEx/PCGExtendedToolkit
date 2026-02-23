// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tangents/PCGExTangentsInstancedFactory.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"

PCGEX_SETTING_VALUE_IMPL(FPCGExTangentsScalingDetails, ArriveScale, FVector, ArriveScaleInput, ArriveScaleAttribute, FVector(ArriveScaleConstant))
PCGEX_SETTING_VALUE_IMPL(FPCGExTangentsScalingDetails, LeaveScale, FVector, LeaveScaleInput, LeaveScaleAttribute, FVector(LeaveScaleConstant))

#if WITH_EDITOR
void FPCGExTangentsDetails::ApplyDeprecation(const bool bUseAttribute, const FName InArriveAttributeName, const FName InLeaveAttributeName)
{
	if (bDeprecationApplied) { return; }

	ArriveTangentAttribute = InArriveAttributeName;
	LeaveTangentAttribute = InLeaveAttributeName;

	Source = bUseAttribute ? EPCGExTangentSource::Attribute : EPCGExTangentSource::None;

	bDeprecationApplied = true;
}
#endif

bool FPCGExTangentsDetails::Init(FPCGExContext* InContext, const FPCGExTangentsDetails& InDetails)
{
	Source = InDetails.Source;
	Scaling = InDetails.Scaling;

	if (Source == EPCGExTangentSource::InPlace)
	{
		if (!InDetails.Tangents)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Main tangent processor must not be null."));
			return false;
		}

		Tangents = PCGEX_REGISTER_INSTANCED_FACTORY_C(InContext, UPCGExTangentsInstancedFactory, InDetails.Tangents, PCGExTangents::SourceOverridesTangents);

		if (InDetails.StartTangents)
		{
			StartTangents = PCGEX_REGISTER_INSTANCED_FACTORY_C(InContext, UPCGExTangentsInstancedFactory, InDetails.StartTangents, PCGExTangents::SourceOverridesTangentsStart);
			if (!StartTangents)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Optional Start tangents processor failed to initialized."));
				return false;
			}
		}
		else
		{
			StartTangents = Tangents;
		}

		if (InDetails.EndTangents)
		{
			EndTangents = PCGEX_REGISTER_INSTANCED_FACTORY_C(InContext, UPCGExTangentsInstancedFactory, InDetails.EndTangents, PCGExTangents::SourceOverridesTangentsEnd);
			if (!EndTangents)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Optional End tangents processor failed to initialized."));
				return false;
			}
		}
		else
		{
			EndTangents = Tangents;
		}
	}
	else if (Source == EPCGExTangentSource::Attribute)
	{
		ArriveTangentAttribute = InDetails.ArriveTangentAttribute;
		LeaveTangentAttribute = InDetails.LeaveTangentAttribute;

		PCGEX_VALIDATE_NAME_C(InContext, ArriveTangentAttribute)
		PCGEX_VALIDATE_NAME_C(InContext, LeaveTangentAttribute)
	}

	return true;
}

namespace PCGExTangents
{
	bool FTangentsHandler::Init(FPCGExContext* InContext, const FPCGExTangentsDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		Mode = InDetails.Source;
		PointData = InDataFacade->GetIn();
		LastIndex = InDataFacade->GetNum() - 1;

		StartScaleReader = InDetails.Scaling.GetValueSettingArriveScale();
		if (!StartScaleReader->Init(InDataFacade)) { return false; }

		EndScaleReader = InDetails.Scaling.GetValueSettingLeaveScale();
		if (!EndScaleReader->Init(InDataFacade)) { return false; }

		if (Mode == EPCGExTangentSource::InPlace)
		{
			Tangents = InDetails.Tangents->CreateOperation();
			Tangents->bClosedLoop = bClosedLoop;

			if (!Tangents->PrepareForData(InContext)) { return false; }

			if (InDetails.StartTangents)
			{
				StartTangents = InDetails.StartTangents->CreateOperation();
				StartTangents->bClosedLoop = bClosedLoop;
				StartTangents->PrimaryDataFacade = InDataFacade;

				if (!StartTangents->PrepareForData(InContext)) { return false; }
			}
			else { StartTangents = Tangents; }

			if (InDetails.EndTangents)
			{
				EndTangents = InDetails.EndTangents->CreateOperation();
				EndTangents->bClosedLoop = bClosedLoop;
				EndTangents->PrimaryDataFacade = InDataFacade;

				if (!EndTangents->PrepareForData(InContext)) { return false; }
			}
			else { EndTangents = Tangents; }
		}
		else if (Mode == EPCGExTangentSource::Attribute)
		{
			ArriveReader = InDataFacade->GetBroadcaster<FVector>(InDetails.ArriveTangentAttribute, true);
			if (!ArriveReader)
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Arrive Tangent Attribute, InDetails.ArriveTangentAttribute)
				return false;
			}

			LeaveReader = InDataFacade->GetBroadcaster<FVector>(InDetails.LeaveTangentAttribute, true);
			if (!LeaveReader)
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Leave Tangent Attribute, InDetails.ArriveTangentAttribute)
				return false;
			}
		}

		return true;
	}

	void FTangentsHandler::GetPointTangents(const int32 Index, FVector& OutArrive, FVector& OutLeave) const
	{
		OutArrive = FVector::ZeroVector;
		OutLeave = FVector::ZeroVector;

		if (Mode == EPCGExTangentSource::None) { return; }

		int32 PrevIndex = Index - 1;
		int32 NextIndex = Index + 1;

		const FVector ArriveScale = StartScaleReader->Read(Index);
		const FVector LeaveScale = EndScaleReader->Read(Index);

		if (Mode == EPCGExTangentSource::InPlace)
		{
			if (bClosedLoop)
			{
				if (PrevIndex < 0) { PrevIndex = LastIndex; }
				if (NextIndex > LastIndex) { NextIndex = 0; }

				Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, ArriveScale, OutArrive, LeaveScale, OutLeave);
			}
			else
			{
				if (Index == 0) { StartTangents->ProcessFirstPoint(PointData, ArriveScale, OutArrive, LeaveScale, OutLeave); }
				else if (Index == LastIndex) { EndTangents->ProcessLastPoint(PointData, ArriveScale, OutArrive, LeaveScale, OutLeave); }
				else { Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, ArriveScale, OutArrive, LeaveScale, OutLeave); }
			}
		}
		else
		{
			OutArrive = ArriveReader->Read(Index) * ArriveScale;
			OutLeave = LeaveReader->Read(Index) * LeaveScale;
		}
	}

	void FTangentsHandler::GetSegmentTangents(const int32 Index, FVector& OutStartTangent, FVector& OutEndTangent) const
	{
		OutStartTangent = FVector::ZeroVector;
		OutEndTangent = FVector::ZeroVector;

		if (Mode == EPCGExTangentSource::None) { return; }

		int32 NextIndex = Index + 1;
		if (bClosedLoop) { if (NextIndex > LastIndex) { NextIndex = 0; } }
		else if (NextIndex >= LastIndex) { NextIndex = LastIndex; }

		const FVector StartScale = StartScaleReader->Read(Index);
		const FVector EndScale = EndScaleReader->Read(NextIndex);

		if (Mode == EPCGExTangentSource::InPlace)
		{
			GetLeaveTangent(Index, OutStartTangent, StartScale);
			GetArriveTangent(NextIndex, OutEndTangent, EndScale);
		}
		else
		{
			OutStartTangent = LeaveReader->Read(Index) * StartScale;
			OutEndTangent = ArriveReader->Read(NextIndex) * EndScale;
		}
	}

	void FTangentsHandler::GetArriveTangent(const int32 Index, FVector& OutDir, const FVector& InScale) const
	{
		// Start tangent is current index' Leave
		// This is called with point "Current Index" as base index

		FVector Dummy = FVector::ZeroVector;

		int32 PrevIndex = Index - 1;
		int32 NextIndex = Index + 1;

		if (bClosedLoop)
		{
			if (PrevIndex < 0) { PrevIndex = LastIndex; }
			if (NextIndex > LastIndex) { NextIndex = 0; }

			Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, InScale, OutDir, InScale, Dummy);
		}
		else
		{
			if (Index <= 0) { StartTangents->ProcessFirstPoint(PointData, InScale, OutDir, InScale, Dummy); }
			else if (Index >= LastIndex) { EndTangents->ProcessLastPoint(PointData, InScale, OutDir, InScale, Dummy); }
			else { Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, InScale, OutDir, InScale, Dummy); }
		}
	}

	void FTangentsHandler::GetLeaveTangent(const int32 Index, FVector& OutDir, const FVector& InScale) const
	{
		// End Tangent is next index' Arrive
		// This is called with point "Next Index" as base index

		FVector Dummy = FVector::ZeroVector;

		int32 PrevIndex = Index - 1;
		int32 NextIndex = Index + 1;

		if (bClosedLoop)
		{
			if (PrevIndex < 0) { PrevIndex = LastIndex; }
			if (NextIndex > LastIndex) { NextIndex = 0; }

			Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, InScale, Dummy, InScale, OutDir);
		}
		else
		{
			if (Index == 0) { StartTangents->ProcessFirstPoint(PointData, InScale, Dummy, InScale, OutDir); }
			else if (Index == LastIndex) { EndTangents->ProcessLastPoint(PointData, InScale, Dummy, InScale, OutDir); }
			else { Tangents->ProcessPoint(PointData, Index, NextIndex, PrevIndex, InScale, Dummy, InScale, OutDir); }
		}
	}
}
