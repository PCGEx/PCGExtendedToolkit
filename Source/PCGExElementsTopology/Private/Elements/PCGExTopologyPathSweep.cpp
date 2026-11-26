// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTopologyPathSweep.h"

#include "PCGExCoreSettingsCache.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Algo/BinarySearch.h"
#include "Core/PCGExBlendOperations.h"
#include "Data/PCGDynamicMeshData.h" // Redundant but required for build on Linux
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/MeshRepairFunctions.h"
#include "Core/PCGExMTCommon.h"
#include "Math/PCGExMathAxis.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExTopologyPathSweepElement"
#define PCGEX_NAMESPACE TopologyPathSweep

PCGEX_INITIALIZE_ELEMENT(TopologyPathSweep)
PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(TopologyPathSweep)

#pragma region PCGExTopologyPathSweep helpers

namespace PCGExTopologyPathSweep
{
	// Corner order per quad: (column, row), (column, row+1), (column+1, row), (column+1, row+1). Indexed by [flipped][triangle].
	constexpr int32 QuadTriangles[2][2][3] = {{{0, 1, 2}, {2, 1, 3}}, {{0, 2, 1}, {2, 3, 1}}};

	// A fold's crossing is searched within this many times the track's distance to the path, in path length.
	constexpr double FoldSearchRatio = 2.0;

	// Past the base window the search continues to this ratio, for smooth bends up to about 166 degrees.
	constexpr double FoldExtendedSearchRatio = 8.0;

	// A crossing found past the base window must join track points closer than this ratio of the track's offset.
	constexpr double FoldExtendedGapRatio = 1.0;

	// Clearance ignores sheets whose height ranges stay apart by more than this ratio of the larger facing reach.
	constexpr double ClearanceBandMargin = 0.5;

	FVector2D FProfile::GetPositionAt(const double Alpha) const
	{
		const double SafeAlpha = FMath::IsFinite(Alpha) ? FMath::Clamp(Alpha, 0.0, 1.0) : 0.0;
		// The last alpha of a closed profile is its seam, back on the first point.
		const int32 Next = FMath::Clamp(static_cast<int32>(Algo::UpperBound(Alphas, SafeAlpha)), 1, NumRows);
		const int32 Prev = Next - 1;

		const double Span = Alphas[Next] - Alphas[Prev];
		const double Ratio = Span > UE_DOUBLE_SMALL_NUMBER ? (SafeAlpha - Alphas[Prev]) / Span : 0.0;
		return FMath::Lerp(Positions[Prev], Positions[Next % Num], Ratio);
	}

	FVector2D FSheet::GetScale(const int32 Index) const
	{
		return FVector2D(Depth->Read(Index) * DepthScale, Width->Read(Index) * WidthScale);
	}

	FVector2D FSheet::GetAnchor(const int32 Index) const
	{
		return Reference ? Profile->GetPositionAt(Reference->Read(Index)) : FixedAnchor;
	}

	TSharedPtr<FProfile> BuildProfile(FPCGExContext* InContext, const FName InPinLabel)
	{
		const TSharedPtr<PCGExData::FPointIO> ProfileIO = PCGExData::TryGetSingleInput(InContext, InPinLabel, false, true);
		if (!ProfileIO)
		{
			return nullptr;
		}

		if (InContext->InputData.GetInputCountByPin(InPinLabel) > 1)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' received more than one data: only the first one with points is used."), FText::FromName(InPinLabel)));
		}

		const UPCGBasePointData* ProfileData = ProfileIO->GetIn();
		const TConstPCGValueRange<FTransform> Transforms = ProfileData->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector4> InColors = ProfileData->GetConstColorValueRange();

		const int32 Num = Transforms.Num();
		if (Num < 2)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' must have at least two points."), FText::FromName(InPinLabel)));
			return nullptr;
		}

		TSharedPtr<FProfile> Profile = MakeShared<FProfile>();
		// A two-point loop folds back onto itself: it is read as an open profile.
		Profile->bClosed = Num > 2 && PCGExPaths::Helpers::GetClosedLoop(ProfileData);
		Profile->Num = Num;
		Profile->NumRows = Profile->bClosed ? Num : Num - 1;
		Profile->NumUVRows = Profile->NumRows + 1;

		Profile->Positions.SetNumUninitialized(Num);
		Profile->Colors.SetNumUninitialized(Num);
		Profile->Alphas.SetNumUninitialized(Profile->NumUVRows);

		// Bounds of the positions alone: the point data's own bounds include each point's extents.
		FBox2D Bounds(ForceInit);
		double MinZ = MAX_dbl;
		double MaxZ = -MAX_dbl;

		for (int32 i = 0; i < Num; i++)
		{
			const FVector Location = Transforms[i].GetLocation();
			Profile->Positions[i] = FVector2D(Location.X, Location.Y);
			Profile->Colors[i] = InColors[i];

			Bounds += Profile->Positions[i];
			MinZ = FMath::Min(MinZ, Location.Z);
			MaxZ = FMath::Max(MaxZ, Location.Z);
		}

		// Alphas and RawLength are measured as authored, so they do not depend on the normalization below.
		double Length = 0;
		Profile->Alphas[0] = 0;
		for (int32 i = 1; i < Profile->NumUVRows; i++)
		{
			Length += FVector2D::Distance(Profile->Positions[i - 1], Profile->Positions[i % Num]);
			Profile->Alphas[i] = Length;
		}

		if (Length <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' has all its points collocated once flattened: profiles are read in their local XY plane."), FText::FromName(InPinLabel)));
			return nullptr;
		}

		for (double& Alpha : Profile->Alphas)
		{
			Alpha /= Length;
		}

		Profile->RawLength = Length;

		// One factor for both axes, from the longest side: equal Depth and Width then keep the drawn proportions.
		const FVector2D Size = Bounds.GetSize();
		const double LongestSide = FMath::Max(Size.X, Size.Y);
		const double InvSize = LongestSide > UE_DOUBLE_SMALL_NUMBER ? 1.0 / LongestSide : 0.0;

		for (FVector2D& Position : Profile->Positions)
		{
			Position = (Position - Bounds.Min) * InvSize;
		}

		Profile->Center = (Bounds.GetCenter() - Bounds.Min) * InvSize;
		Profile->Origin = (FVector2D::ZeroVector - Bounds.Min) * InvSize;
		Profile->Size = Size * InvSize;

		if (MaxZ - MinZ > UE_DOUBLE_KINDA_SMALL_NUMBER && FMath::Min(Size.X, Size.Y) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' is flat once its Z is dropped: profiles are read in their local XY plane."), FText::FromName(InPinLabel)));
		}

		return Profile;
	}
}

#pragma endregion

#pragma region UPCGExTopologyPathSweepSettings

TArray<FPCGPinProperties> UPCGExTopologyPathSweepSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExTopologyPathSweep::SourceProfile, "Single path used as the profile, read in its local XY plane with its longest side as one unit: X is scaled by Depth and Y by Width. A closed loop makes a tube.", Required)
	// Always declared, so a mode change never drops its wire.
	PCGEX_PIN_POINT(PCGExTopologyPathSweep::SourceSecondaryProfile, "Single path used as the secondary profile in Dual mode. The primary profile is used when unconnected.", Normal)
	if (Mode != EPCGExPathSweepMode::Dual)
	{
		PinProperties.Last().PinStatus = EPCGPinStatus::Advanced;
	}
	return PinProperties;
}

bool UPCGExTopologyPathSweepSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExTopologyPathSweep::SourceSecondaryProfile)
	{
		return Mode == EPCGExPathSweepMode::Dual;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExTopologyPathSweepSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::OutputMeshLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExTopologyPathSweepContext

void FPCGExTopologyPathSweepContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(TopologyPathSweep)

	FPCGExPathProcessorContext::RegisterAssetDependencies();

	if (Settings->Topology.Material.ToSoftObjectPath().IsValid())
	{
		AddAssetDependency(Settings->Topology.Material.ToSoftObjectPath());
	}

	if (Settings->Mode == EPCGExPathSweepMode::Dual && Settings->SecondaryMaterial.ToSoftObjectPath().IsValid())
	{
		AddAssetDependency(Settings->SecondaryMaterial.ToSoftObjectPath());
	}
}

#pragma endregion

#pragma region FPCGExTopologyPathSweepElement

bool FPCGExTopologyPathSweepElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPathSweep)

	const auto UnsupportedValue = [&](const TCHAR* SettingName)
	{
		PCGE_LOG(Error, GraphAndLog, FText::Format(FTEXT("Unsupported '{0}' value."), FText::FromString(SettingName)));
		return false;
	};

	Context->Profile = PCGExTopologyPathSweep::BuildProfile(Context, PCGExTopologyPathSweep::SourceProfile);
	if (!Context->Profile)
	{
		return false;
	}

	switch (Settings->Mode)
	{
	case EPCGExPathSweepMode::Single:
		break;
	case EPCGExPathSweepMode::Dual:
		{
			// Connectivity decides, not data: a connected pin that brings no profile is an error, never a fallback.
			const bool bSecondaryConnected = Context->Node
				                                 ? Context->Node->IsInputPinConnected(PCGExTopologyPathSweep::SourceSecondaryProfile)
				                                 : Context->InputData.GetInputCountByPin(PCGExTopologyPathSweep::SourceSecondaryProfile) > 0;

			if (bSecondaryConnected)
			{
				Context->SecondaryProfile = PCGExTopologyPathSweep::BuildProfile(Context, PCGExTopologyPathSweep::SourceSecondaryProfile);
				if (!Context->SecondaryProfile)
				{
					return false;
				}
			}
			else
			{
				Context->SecondaryProfile = Context->Profile;
			}
		}
		break;
	default:
		return UnsupportedValue(TEXT("Mode"));
	}

	switch (Settings->PrimaryAxis)
	{
	case EPCGExPathSweepAxis::Y:
		Context->bPrimaryOnY = true;
		break;
	case EPCGExPathSweepAxis::Z:
		Context->bPrimaryOnY = false;
		break;
	default:
		return UnsupportedValue(TEXT("Primary Axis"));
	}

	switch (Settings->UMode)
	{
	case EPCGExPathSweepUVMode::Normalized:
		Context->bUDistance = false;
		break;
	case EPCGExPathSweepUVMode::Distance:
		Context->bUDistance = true;
		break;
	default:
		return UnsupportedValue(TEXT("U Mode"));
	}

	switch (Settings->VMode)
	{
	case EPCGExPathSweepUVMode::Normalized:
		Context->bVDistance = false;
		break;
	case EPCGExPathSweepUVMode::Distance:
		Context->bVDistance = true;
		break;
	default:
		return UnsupportedValue(TEXT("V Mode"));
	}

	if (Context->SecondaryProfile)
	{
		switch (Settings->VLayout)
		{
		case EPCGExPathSweepVLayout::Continuous:
			Context->bVContinuous = true;
			break;
		case EPCGExPathSweepVLayout::Sequential:
			Context->bVContinuous = false;
			break;
		default:
			return UnsupportedValue(TEXT("V Layout"));
		}

		switch (Settings->VSplit)
		{
		case EPCGExPathSweepVSplit::Midpoint:
			// Overrides bypass ClampMin/ClampMax.
			Context->VSplit = FMath::IsFinite(Settings->VMidpoint) ? FMath::Clamp(Settings->VMidpoint, 0.0, 1.0) : 0.5;
			break;
		case EPCGExPathSweepVSplit::LengthRatio:
			Context->VSplit = Context->Profile->RawLength / (Context->Profile->RawLength + Context->SecondaryProfile->RawLength);
			break;
		default:
			return UnsupportedValue(TEXT("V Split"));
		}
	}

	switch (Settings->ColorBlending)
	{
	case EPCGExABBlendingType::None:
	case EPCGExABBlendingType::CopyTarget:
		// = A, the path point color
		break;
	case EPCGExABBlendingType::CopySource:
		Context->bProfileColorOnly = true;
		break;
	default:
		Context->ColorBlend = PCGExBlending::FBlendOperationFactory::Create(EPCGMetadataTypes::Vector4, Settings->ColorBlending);
		if (!Context->ColorBlend)
		{
			return UnsupportedValue(TEXT("Color Blending"));
		}
		Context->bColorBlendWeighted = PCGExBlending::RequiresWeight(Settings->ColorBlending);
		break;
	}

	if (Settings->bClearance)
	{
		switch (Settings->ClearanceSplit)
		{
		case EPCGExPathSweepClearanceSplit::Share:
			break;
		case EPCGExPathSweepClearanceSplit::ReachRatio:
			Context->bClearanceReachRatio = true;
			break;
		case EPCGExPathSweepClearanceSplit::SharePadding:
			// Overrides bypass ClampMin.
			Context->ClearancePadding = FMath::IsFinite(Settings->ClearancePadding) ? FMath::Max(Settings->ClearancePadding, 0.0) : 0.0;
			break;
		default:
			return UnsupportedValue(TEXT("Clearance"));
		}

		switch (Settings->ClearanceShrink)
		{
		case EPCGExPathSweepClearanceShrink::Squash:
			Context->bClearanceClamp = false;
			break;
		case EPCGExPathSweepClearanceShrink::Clamp:
			Context->bClearanceClamp = true;
			break;
		default:
			return UnsupportedValue(TEXT("Shrink"));
		}

		switch (Settings->ClearanceShrinks)
		{
		case EPCGExPathSweepClearanceShrinks::Both:
			Context->ClearanceShrinksSign = 0;
			break;
		case EPCGExPathSweepClearanceShrinks::Lower:
			Context->ClearanceShrinksSign = 1;
			break;
		case EPCGExPathSweepClearanceShrinks::Higher:
			Context->ClearanceShrinksSign = -1;
			break;
		default:
			return UnsupportedValue(TEXT("Shrinks"));
		}

		// Overrides bypass ClampMin/ClampMax.
		Context->ClearanceShare = FMath::IsFinite(Settings->ClearanceShare) ? FMath::Clamp(Settings->ClearanceShare, 0.0, 1.0) : 0.5;
		Context->ClearanceSmoothing = FMath::IsFinite(Settings->ClearanceSmoothing) ? FMath::Max(Settings->ClearanceSmoothing, 0.0) : 0.0;
		Context->ClearanceBlendHeight = FMath::IsFinite(Settings->ClearanceBlendHeight) ? FMath::Max(Settings->ClearanceBlendHeight, 0.0) : 0.0;
	}

	if (Settings->bWriteClearanceUV)
	{
		const int32 ClearanceChannel = FMath::Clamp(Settings->ClearanceUVChannel, 0, 7);
		if (ClearanceChannel == FMath::Clamp(Settings->UVChannel, 0, 7))
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(FTEXT("'Clearance UV Channel' and 'UV Channel' are both {0}: they must differ."), FText::AsNumber(ClearanceChannel)));
			return false;
		}
		Context->ClearanceUVChannel = ClearanceChannel;
	}

	const auto WarnAttributeChannel = [&](const int32 InChannel, const FText& InOutput)
	{
		for (const FPCGExUVInputDetails& Channel : Settings->Topology.UVChannels.UVs)
		{
			if (Channel.bEnabled && !Channel.AttributeName.IsNone() && Channel.Channel == InChannel)
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(FTEXT("UV channel {0} is also fed by '{1}': the {2} replace it."), FText::AsNumber(InChannel), FText::FromName(Channel.AttributeName), InOutput));
				break;
			}
		}
	};

	WarnAttributeChannel(Settings->UVChannel, FTEXT("generated UVs"));
	if (Context->ClearanceUVChannel != INDEX_NONE)
	{
		WarnAttributeChannel(Context->ClearanceUVChannel, FTEXT("Clearance UVs"));
	}

	return true;
}

bool FPCGExTopologyPathSweepElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTopologyPathSweepElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TopologyPathSweep)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				// Clearance needs every path before any is swept, so its sweep moves to CompleteWork.
				NewBatch->bSkipCompletion = !Settings->bClearance;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any path to sweep."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainBatch->Output();

	return Context->TryComplete();
}

#pragma endregion

#pragma region FProcessor

namespace PCGExTopologyPathSweep
{
	bool FProcessor::InitSheet(FSheet& Sheet, const FPCGExPathSweepProfileDetails& Details, const TSharedPtr<FProfile>& InProfile)
	{
		Sheet.Profile = InProfile;

		Sheet.Depth = Details.Depth.GetValueSetting();
		if (!Sheet.Depth->Init(PointDataFacade))
		{
			return false;
		}

		Sheet.Width = Details.Width.GetValueSetting();
		if (!Sheet.Width->Init(PointDataFacade))
		{
			return false;
		}

		Sheet.DepthScale = Details.DepthScale;
		Sheet.WidthScale = Details.WidthScale;

		switch (Details.Anchor)
		{
		case EPCGExPathSweepAnchor::Reference:
			{
				const TSharedPtr<PCGExDetails::TSettingValue<double>> ReferenceValue = Details.Reference.GetValueSetting();
				if (!ReferenceValue->Init(PointDataFacade))
				{
					return false;
				}

				if (ReferenceValue->IsConstant())
				{
					Sheet.FixedAnchor = InProfile->GetPositionAt(ReferenceValue->Read(0));
				}
				else
				{
					Sheet.Reference = ReferenceValue;
				}
			}
			break;
		case EPCGExPathSweepAnchor::Center:
			Sheet.FixedAnchor = InProfile->Center;
			break;
		case EPCGExPathSweepAnchor::Origin:
			Sheet.FixedAnchor = InProfile->Origin;
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Unsupported 'Anchor' value."));
			return false;
		}

		return true;
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::Process);

		// The mesh assembly reads every point once the parallel pass is over.
		PointDataFacade->bSupportsScopedGet = false;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		NumPoints = InPointData->GetNumPoints();

		// A two-point loop folds back onto itself, which a manifold mesh cannot hold: sweep it as an open path.
		bClosedLoop = NumPoints > 2 && PCGExPaths::Helpers::GetClosedLoop(InPointData);

		Path = MakeShared<PCGExPaths::FPath>(InPointData->GetConstTransformValueRange(), bClosedLoop);
		Path->IOIndex = PointDataFacade->Source->IOIndex;

		const int32 NumEdges = Path->NumEdges;
		EdgeDirs.SetNumUninitialized(NumEdges);

		FVector FirstDir = FVector::ZeroVector;
		FVector LastDir = FVector::ZeroVector;
		for (int32 i = 0; i < NumEdges; i++)
		{
			const FVector& Dir = Path->Edges[i].Dir;
			if (!Dir.IsZero())
			{
				if (FirstDir.IsZero())
				{
					FirstDir = Dir;
				}
				LastDir = Dir;
			}
			EdgeDirs[i] = LastDir;
		}

		if (LastDir.IsZero())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A path has all its points collocated and was skipped."));
			return false;
		}

		// Leading zero-length edges have no upstream edge yet: a loop wraps to its last one, an open path looks ahead.
		for (int32 i = 0; i < NumEdges && EdgeDirs[i].IsZero(); i++)
		{
			EdgeDirs[i] = bClosedLoop ? LastDir : FirstDir;
		}

		WorldUp = PCGEX_CORE_SETTINGS.WorldUp;

		switch (Settings->Orientation)
		{
		case EPCGExPathNormalDirection::Custom:
			UpGetter = Settings->UpVector.GetValueSetting();
			if (!UpGetter->Init(PointDataFacade))
			{
				return false;
			}
			break;
		case EPCGExPathNormalDirection::Normal:
			SideNormal = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeNormal>(false, WorldUp));
			break;
		case EPCGExPathNormalDirection::Binormal:
			SideNormal = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeBinormal>(false, WorldUp));
			break;
		case EPCGExPathNormalDirection::AverageNormal:
			SideNormal = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeAvgNormal>(false, WorldUp));
			break;
		case EPCGExPathNormalDirection::ParallelTransport:
			SideNormal = StaticCastSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>>(Path->AddExtra<PCGExPaths::FPathEdgeParallelTransportNormal>(false, WorldUp));
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Unsupported 'Orientation' value."));
			return false;
		}

		Path->ComputeAllEdgeExtra();

		// Overrides bypass ClampMin.
		MiterLimit = FMath::IsFinite(Settings->MiterLimit) ? FMath::Clamp(Settings->MiterLimit, 1.0, 100.0) : 1.0;

		const bool bDual = Context->SecondaryProfile.IsValid();
		Sheets.SetNum(bDual ? 2 : 1);

		if (!InitSheet(Sheets[0], Settings->Primary, Context->Profile))
		{
			return false;
		}
		Sheets[0].bDepthOnY = Context->bPrimaryOnY;

		if (bDual)
		{
			if (!InitSheet(Sheets[1], Settings->Secondary, Context->SecondaryProfile))
			{
				return false;
			}

			Sheets[1].bDepthOnY = !Context->bPrimaryOnY;
			Sheets[1].MaterialIndex = 1;
			// The secondary profile runs against the unfolded strip, so its faces are flipped to match the primary's.
			Sheets[1].bFlipWinding = true;
		}

		if (Context->bVDistance)
		{
			// Measured on the profiles as authored, never on their swept size: a row keeps its V along the whole path.
			const double TileV = FMath::Max(Settings->TileSize.Y, UE_DOUBLE_KINDA_SMALL_NUMBER);
			Sheets[0].VScale = Context->Profile->RawLength / TileV;

			if (bDual)
			{
				Sheets[1].VScale = Context->SecondaryProfile->RawLength / TileV;

				if (Context->bVContinuous)
				{
					// Both profiles leave V = 0, the primary toward negative V.
					Sheets[0].VScale = -Sheets[0].VScale;
				}
				else
				{
					Sheets[1].VStart = Sheets[0].VScale;
				}
			}
		}
		else if (bDual)
		{
			const double Split = Context->VSplit;
			Sheets[1].VStart = Split;
			Sheets[1].VScale = 1.0 - Split;

			if (Context->bVContinuous)
			{
				Sheets[0].VStart = Split;
				Sheets[0].VScale = -Split;
			}
			else
			{
				Sheets[0].VScale = Split;
			}
		}

		int64 NumVertices = 0;
		for (FSheet& Sheet : Sheets)
		{
			Sheet.VertexOffset = static_cast<int32>(NumVertices);
			NumVertices += static_cast<int64>(NumPoints) * Sheet.Profile->Num;
		}

		// Up to two triangles per vertex, and triangle ids are int32.
		if (NumVertices > MAX_int32 / 2)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("A path and its profiles make too many vertices for a single mesh and was skipped."));
			return false;
		}

		if (Context->bColorBlendWeighted)
		{
			ColorBlendWeight = Settings->ColorBlendWeight.GetValueSetting();
			if (!ColorBlendWeight->Init(PointDataFacade))
			{
				return false;
			}
		}

		InternalMeshData = Context->ManagedObjects->New<UPCGDynamicMeshData>();
		if (!InternalMeshData)
		{
			return false;
		}

		// The data owns its mesh, created empty with attributes and material ids enabled.
		InternalMesh = InternalMeshData->GetMutableDynamicMesh();

		// Processors run on workers: the context hands over the materials its load recorded, no lookup.
		UMaterialInterface* Material = Context->ResolveObject_AnyThreadTpl(Settings->Topology.Material);
		if (bDual)
		{
			UMaterialInterface* SecondaryMaterial = Context->ResolveObject_AnyThreadTpl(Settings->SecondaryMaterial);
			if (Material || SecondaryMaterial)
			{
				InternalMeshData->SetMaterials({Material, SecondaryMaterial ? SecondaryMaterial : Material});
			}
		}
		else if (Material)
		{
			InternalMeshData->SetMaterials({Material});
		}

		UVChannel = FMath::Clamp(Settings->UVChannel, 0, 7);
		ClearanceUVChannel = Context->ClearanceUVChannel;

		UVDetails = Settings->Topology.UVChannels;
		// Generated UVs own their channels: an attribute channel on the same index is not read at all.
		for (FPCGExUVInputDetails& Channel : UVDetails.UVs)
		{
			if (Channel.Channel == UVChannel || (ClearanceUVChannel != INDEX_NONE && Channel.Channel == ClearanceUVChannel))
			{
				Channel.bEnabled = false;
			}
		}
		UVDetails.Prepare(PointDataFacade);

		MeshTransform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		Positions.SetNumUninitialized(static_cast<int32>(NumVertices));
		Colors.SetNumUninitialized(static_cast<int32>(NumVertices));
		if (ClearanceUVChannel != INDEX_NONE)
		{
			ClearanceUVs.SetNumUninitialized(static_cast<int32>(NumVertices));
		}

		if (Settings->bClearance)
		{
			// Other paths query this one from their CompleteWork: everything they read is built here.
			Path->BuildEdgeOctree();
			ComputeReach();
			bClearanceReady = true;
			return true;
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	FVector FProcessor::GetTangent(const int32 Index) const
	{
		// An open path's endpoints only have one edge, which then stands for both sides.
		const int32 OutEdge = bClosedLoop ? Index : FMath::Min(Index, Path->LastEdge);
		const int32 InEdge = bClosedLoop ? (Index == 0 ? Path->LastEdge : Index - 1) : FMath::Max(Index - 1, 0);

		// Profiles sit in the plane bisecting the corner. A full reversal has no bisector.
		const FVector Tangent = (EdgeDirs[InEdge] + EdgeDirs[OutEdge]).GetSafeNormal();
		return Tangent.IsZero() ? EdgeDirs[OutEdge] : Tangent;
	}

	void FProcessor::ComputeFrame(const int32 Index, FVector& OutTangent, FVector& OutAxisY, FVector& OutAxisZ)
	{
		const int32 OutEdge = bClosedLoop ? Index : FMath::Min(Index, Path->LastEdge);
		const int32 InEdge = bClosedLoop ? (Index == 0 ? Path->LastEdge : Index - 1) : FMath::Max(Index - 1, 0);

		const FVector& DirIn = EdgeDirs[InEdge];
		const FVector& DirOut = EdgeDirs[OutEdge];

		OutTangent = GetTangent(Index);

		if (UpGetter)
		{
			OutAxisY = PCGExMath::SafeCrossNormal(UpGetter->Read(Index), OutTangent);
		}
		else
		{
			const FVector Side = SideNormal->Get(OutEdge);
			OutAxisY = (Side - FVector::DotProduct(Side, OutTangent) * OutTangent).GetSafeNormal();
			if (OutAxisY.IsZero())
			{
				OutAxisY = PCGExMath::SafeCrossNormal(WorldUp, OutTangent);
			}
		}

		OutAxisZ = FVector::CrossProduct(OutTangent, OutAxisY);

		// Miter: stretch across the bend so each segment keeps the profile's size once projected onto it.
		const FVector Bend = DirOut - DirIn;
		if (Bend.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
		{
			const FVector MiterAxis = Bend.GetSafeNormal();
			const double CosHalfAngle = FVector::DotProduct(OutTangent, DirOut);
			const double Stretch = CosHalfAngle * MiterLimit > 1.0 ? 1.0 / CosHalfAngle : MiterLimit;

			OutAxisY += MiterAxis * (FVector::DotProduct(OutAxisY, MiterAxis) * (Stretch - 1.0));
			OutAxisZ += MiterAxis * (FVector::DotProduct(OutAxisZ, MiterAxis) * (Stretch - 1.0));
		}
	}

	void FProcessor::ComputeReach()
	{
		SideDirs.SetNumUninitialized(NumPoints);
		UpDirs.SetNumUninitialized(NumPoints);
		Reach.SetNumUninitialized(NumPoints);
		UpBand.SetNumUninitialized(NumPoints);
		Allowed.SetNumUninitialized(NumPoints);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			FVector Tangent;
			FVector AxisY;
			FVector AxisZ;
			ComputeFrame(Index, Tangent, AxisY, AxisZ);

			// The miter only adds a component along the bend axis, which stays perpendicular to the tangent.
			const FVector SideDir = AxisY.GetSafeNormal();
			const FVector UpDir = FVector::CrossProduct(Tangent, SideDir).GetSafeNormal();

			SideDirs[Index] = SideDir;
			UpDirs[Index] = UpDir;

			if (SideDir.IsZero() || UpDir.IsZero())
			{
				Reach[Index] = FVector2D::ZeroVector;
				UpBand[Index] = FVector2D::ZeroVector;
				Allowed[Index] = FVector2D::ZeroVector;
				continue;
			}

			// Extremes of every sheet in profile units, along AxisY (side) and AxisZ (up). Starting at 0 keeps the path
			// itself inside both ranges.
			double SideMin = 0;
			double SideMax = 0;
			double UpMin = 0;
			double UpMax = 0;

			for (const FSheet& Sheet : Sheets)
			{
				const FVector2D Scale = Sheet.GetScale(Index);
				const FVector2D Anchor = Sheet.GetAnchor(Index);

				// Positions run 0..Size on each axis, so the extremes are corners of the bounds.
				const FVector2D CornerA = (FVector2D::ZeroVector - Anchor) * Scale;
				const FVector2D CornerB = (Sheet.Profile->Size - Anchor) * Scale;

				const int32 SideAxis = Sheet.bDepthOnY ? 0 : 1;
				const int32 UpAxis = 1 - SideAxis;

				SideMin = FMath::Min3(SideMin, CornerA[SideAxis], CornerB[SideAxis]);
				SideMax = FMath::Max3(SideMax, CornerA[SideAxis], CornerB[SideAxis]);
				UpMin = FMath::Min3(UpMin, CornerA[UpAxis], CornerB[UpAxis]);
				UpMax = FMath::Max3(UpMax, CornerA[UpAxis], CornerB[UpAxis]);
			}

			const double SideLength = AxisY.Size();
			const double UpHeight = FVector::DotProduct(AxisZ, UpDir);
			const double HeightA = UpMin * UpHeight;
			const double HeightB = UpMax * UpHeight;

			Reach[Index] = FVector2D(SideMax * SideLength, -SideMin * SideLength);
			UpBand[Index] = FVector2D(FMath::Min(HeightA, HeightB), FMath::Max(HeightA, HeightB));
			Allowed[Index] = Reach[Index];

			MaxReach = FMath::Max3(MaxReach, Reach[Index].X, Reach[Index].Y);
			MaxBand = FMath::Max3(MaxBand, -UpBand[Index].X, UpBand[Index].Y);
		}
	}

	double FProcessor::GetFacingReach(const int32 Index, const FVector& RayDir) const
	{
		// The side whose axis points back against the ray is the one facing it.
		return FVector::DotProduct(SideDirs[Index], RayDir) < 0 ? Reach[Index].X : Reach[Index].Y;
	}

	void FProcessor::CompleteWork()
	{
		if (!bClearanceReady)
		{
			return;
		}

		const TSharedPtr<PCGExPointsMT::IBatch> Parent = ParentBatch.Pin();
		ClearanceBatch = Parent ? static_cast<const FBatch*>(Parent.Get()) : nullptr;

		// Blocking: the queries are done once this returns.
		if (ClearanceBatch && ClearanceBatch->PathOctree)
		{
			StartParallelLoopForRange(NumPoints);
		}

		ClearanceBatch = nullptr;

		PCGEX_CHECK_WORK_HANDLE_VOID

		SmoothClearance();
		StartParallelLoopForPoints(PCGExData::EIOSide::In);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::Clearance);

		const FBatch* Batch = ClearanceBatch;
		check(Batch && Batch->PathOctree);

		const bool bReachRatio = Context->bClearanceReachRatio;
		const double Share = Context->ClearanceShare;
		const double Padding = Context->ClearancePadding;
		const double ShrinksSign = Context->ClearanceShrinksSign;
		const double BlendHeight = Context->ClearanceBlendHeight;

		// Free space kept between two facing sheets: each keeps its padding.
		const double PaddingGap = 2 * Padding;

		PCGEX_SCOPE_LOOP(Index)
		{
			const FVector Origin = Path->GetPos_Unsafe(Index);
			const FVector& UpDir = UpDirs[Index];
			const FVector2D& PointReach = Reach[Index];
			const FVector2D& Band = UpBand[Index];

			for (int32 s = 0; s < 2; s++)
			{
				const double SideReach = PointReach[s];
				if (SideReach <= 0)
				{
					continue;
				}

				const FVector RayDir = s == 0 ? SideDirs[Index] : -SideDirs[Index];
				const FVector RayFwd = FVector::CrossProduct(UpDir, RayDir);

				// Farthest a counted crossing can sit: past both reaches and the padding along the ray, past both height
				// ranges and the margin off it.
				const double RayLength = SideReach + Batch->MaxReach + PaddingGap;
				const double RayRise = FMath::Max(-Band.X, Band.Y) + Batch->MaxBand + ClearanceBandMargin * FMath::Max(SideReach, Batch->MaxReach);

				FBox RayBox(ForceInit);
				RayBox += Origin;
				RayBox += Origin + RayDir * RayLength;
				RayBox = RayBox.ExpandBy(RayRise + UE_KINDA_SMALL_NUMBER);

				double Limit = SideReach;

				Batch->PathOctree->FindElementsWithBoundsTest(RayBox, [&](const PCGExOctree::FItem& Item)
				{
					const FProcessor* Other = static_cast<const FProcessor*>(&Batch->Processors[Item.Index].Get());
					const PCGExPaths::FPath* OtherPath = Other->Path.Get();

					// Own edges meeting at the origin cross the ray right there.
					const double MinHit = Other == this ? UE_KINDA_SMALL_NUMBER : 0.0;

					OtherPath->GetEdgeOctree()->FindElementsWithBoundsTest(RayBox, [&](const PCGExPaths::FPathEdge* Edge)
					{
						const FVector A = OtherPath->GetPos_Unsafe(Edge->Start) - Origin;
						const FVector B = OtherPath->GetPos_Unsafe(Edge->End) - Origin;

						// 2D in the plane the ray sweeps: X along the ray, Y along the path, heights along UpDir.
						const double YA = FVector::DotProduct(A, RayFwd);
						const double YB = FVector::DotProduct(B, RayFwd);
						if ((YA > 0 && YB > 0) || (YA < 0 && YB < 0) || YA == YB)
						{
							return;
						}

						const double Alpha = YA / (YA - YB);
						const double Hit = FMath::Lerp(FVector::DotProduct(A, RayDir), FVector::DotProduct(B, RayDir), Alpha);
						if (Hit < MinHit)
						{
							return;
						}

						// The two sheets, padding included, must meet along the ray...
						const double OtherReach = FMath::Lerp(Other->GetFacingReach(Edge->Start, RayDir), Other->GetFacingReach(Edge->End, RayDir), Alpha);
						if (Hit > SideReach + OtherReach + PaddingGap)
						{
							return;
						}

						// ...and so must their height ranges, the other's carried onto this row's up axis.
						const FVector Crossing = FMath::Lerp(A, B, Alpha);
						const double Height = FVector::DotProduct(Crossing, UpDir);
						const FVector2D OtherBand = FMath::Lerp(Other->UpBand[Edge->Start], Other->UpBand[Edge->End], Alpha);
						const double Tilt = FVector::DotProduct(FMath::Lerp(Other->UpDirs[Edge->Start], Other->UpDirs[Edge->End], Alpha), UpDir);
						const double OtherLow = Height + FMath::Min(OtherBand.X * Tilt, OtherBand.Y * Tilt);
						const double OtherHigh = Height + FMath::Max(OtherBand.X * Tilt, OtherBand.Y * Tilt);

						// The margin keeps uneven ground counting while a path well above or below is ignored.
						const double Margin = ClearanceBandMargin * FMath::Max(SideReach, OtherReach);
						if (OtherLow > Band.Y + Margin || OtherHigh < Band.X - Margin)
						{
							return;
						}

						const double ShareLimit = bReachRatio ? Hit * SideReach / (SideReach + OtherReach) : FMath::Max(Share * Hit - Padding, 0.0);

						double HitLimit = ShareLimit;
						if (ShrinksSign != 0)
						{
							// Positive when this sheet is the one that shrinks: heights are world heights, whatever the frame.
							const double Lead = ShrinksSign * FVector::DotProduct(Crossing, WorldUp);
							const double Weight = BlendHeight > 0 ? FMath::Min(FMath::Abs(Lead) / BlendHeight, 1.0) : (FMath::Abs(Lead) > UE_KINDA_SMALL_NUMBER ? 1.0 : 0.0);

							// The shrinking sheet clears the other's full reach; the other keeps its own.
							const double Target = Lead > 0 ? FMath::Max(Hit - OtherReach - PaddingGap, 0.0) : SideReach;
							HitLimit = FMath::Lerp(ShareLimit, Target, Weight);
						}

						// A minimum over every hit: the visiting order cannot change the result.
						Limit = FMath::Min(Limit, HitLimit);
					});
				});

				Allowed[Index][s] = Limit;
			}
		}
	}

	void FProcessor::SmoothClearance()
	{
		const double Slope = Context->ClearanceSmoothing;
		if (Slope <= 0)
		{
			return;
		}

		const int32 NumEdges = Path->NumEdges;
		// A loop needs two laps each way to carry a pinch all the way around.
		const int32 NumSteps = bClosedLoop ? NumEdges * 2 : NumEdges;

		for (int32 Step = 0; Step < NumSteps; Step++)
		{
			const PCGExPaths::FPathEdge& Edge = Path->Edges[Step % NumEdges];
			const double Rise = Slope * Edge.Length;
			FVector2D& Next = Allowed[Edge.End];
			const FVector2D& Prev = Allowed[Edge.Start];
			Next.X = FMath::Min(Next.X, Prev.X + Rise);
			Next.Y = FMath::Min(Next.Y, Prev.Y + Rise);
		}

		for (int32 Step = NumSteps - 1; Step >= 0; Step--)
		{
			const PCGExPaths::FPathEdge& Edge = Path->Edges[Step % NumEdges];
			const double Rise = Slope * Edge.Length;
			FVector2D& Prev = Allowed[Edge.Start];
			const FVector2D& Next = Allowed[Edge.End];
			Prev.X = FMath::Min(Prev.X, Next.X + Rise);
			Prev.Y = FMath::Min(Prev.Y, Next.Y + Rise);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::ProcessPoints);

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector4> InColors = InPointData->GetConstColorValueRange();

		const PCGExBlending::IBlendOperation* ColorBlend = Context->ColorBlend.Get();
		const bool bProfileColorOnly = Context->bProfileColorOnly;

		const bool bClearance = bClearanceReady;
		const bool bClearanceClamp = Context->bClearanceClamp;
		const bool bClearanceUVs = ClearanceUVChannel != INDEX_NONE;

		PCGEX_SCOPE_LOOP(Index)
		{
			FVector Tangent;
			FVector AxisY;
			FVector AxisZ;
			ComputeFrame(Index, Tangent, AxisY, AxisZ);

			// Per side, in profile units along AxisY: the squash factor and the clamp limit.
			FVector2D SideFactor(1.0, 1.0);
			FVector2D SideLimit(MAX_dbl, MAX_dbl);
			// Clearance UVs per side: the fraction of the reach removed, then the same in world units.
			FVector2f SideUV[2] = {FVector2f::ZeroVector, FVector2f::ZeroVector};
			if (bClearance)
			{
				const double AxisYLength = AxisY.Size();
				const FVector2D& PointReach = Reach[Index];
				const FVector2D& PointAllowed = Allowed[Index];

				for (int32 s = 0; s < 2; s++)
				{
					if (PointReach[s] > 0)
					{
						SideFactor[s] = PointAllowed[s] / PointReach[s];
						SideLimit[s] = AxisYLength > UE_DOUBLE_SMALL_NUMBER ? PointAllowed[s] / AxisYLength : 0.0;
						SideUV[s] = FVector2f(static_cast<float>(1.0 - SideFactor[s]), static_cast<float>(PointReach[s] - PointAllowed[s]));
					}
				}
			}

			// A vertex on the path line belongs to both sides: it reports the more shrunk one.
			const FVector2f& PathLineUV = SideUV[0].X >= SideUV[1].X ? SideUV[0] : SideUV[1];

			// The mesh transform is translation-only, so the axes need none of it.
			const FVector Origin = MeshTransform.InverseTransformPosition(InTransforms[Index].GetLocation());

			const FVector4 PathColor = InColors[Index];
			const double ColorWeight = ColorBlendWeight ? ColorBlendWeight->Read(Index) : 1.0;

			for (const FSheet& Sheet : Sheets)
			{
				const FProfile& Profile = *Sheet.Profile;

				const FVector& DepthAxis = Sheet.bDepthOnY ? AxisY : AxisZ;
				const FVector& WidthAxis = Sheet.bDepthOnY ? AxisZ : AxisY;

				const FVector2D Scale = Sheet.GetScale(Index);
				const FVector2D Anchor = Sheet.GetAnchor(Index);

				const int32 Start = Sheet.VertexOffset + Index * Profile.Num;

				for (int32 i = 0; i < Profile.Num; i++)
				{
					FVector2D Local = (Profile.Positions[i] - Anchor) * Scale;
					double& Side = Sheet.bDepthOnY ? Local.X : Local.Y;

					if (bClearanceUVs)
					{
						ClearanceUVs[Start + i] = Side > 0 ? SideUV[0] : (Side < 0 ? SideUV[1] : PathLineUV);
					}

					if (bClearance)
					{
						if (Side > 0)
						{
							Side = bClearanceClamp ? FMath::Min(Side, SideLimit.X) : Side * SideFactor.X;
						}
						else
						{
							Side = bClearanceClamp ? FMath::Max(Side, -SideLimit.Y) : Side * SideFactor.Y;
						}
					}

					Positions[Start + i] = Origin + DepthAxis * Local.X + WidthAxis * Local.Y;

					FVector4 Color = PathColor;
					if (ColorBlend)
					{
						ColorBlend->Blend(&PathColor, &Profile.Colors[i], ColorWeight, &Color);
					}
					else if (bProfileColorOnly)
					{
						Color = Profile.Colors[i];
					}
					Colors[Start + i] = FVector4f(Color);
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// A cancelled loop leaves the vertex arrays partly unwritten.
		PCGEX_CHECK_WORK_HANDLE_VOID

		if (Settings->bCollapseFolds)
		{
			CollapseFolds();
		}

		BuildMesh();

		Positions.Empty();
		Colors.Empty();
		ClearanceUVs.Empty();
	}

	void FProcessor::CollapseFolds()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::CollapseFolds);

		// One track per sheet and profile point: tracks never share a vertex.
		TArray<FIntPoint> Tracks;
		for (int32 s = 0; s < Sheets.Num(); s++)
		{
			for (int32 i = 0; i < Sheets[s].Profile->Num; i++)
			{
				Tracks.Emplace(s, i);
			}
		}

		PCGExMT::ParallelOrSequential(Tracks.Num(), [&](const int32 t)
		{
			if (CollapseTrack(Sheets[Tracks[t].X], Tracks[t].Y))
			{
				bFoldsCollapsed = true;
			}
		});
	}

	bool FProcessor::CollapseTrack(const FSheet& Sheet, const int32 ProfileIndex)
	{
		const int32 NumEdges = Path->NumEdges;
		const int32 Stride = Sheet.Profile->Num;
		const int32 Base = Sheet.VertexOffset + ProfileIndex;

		// Edges are addressed unrolled: a loop wraps, an open path never leaves 0..NumEdges-1.
		const auto Wrap = [&](const int32 U) { return bClosedLoop ? ((U % NumEdges) + NumEdges) % NumEdges : U; };
		const auto EdgeAt = [&](const int32 U) -> const PCGExPaths::FPathEdge& { return Path->Edges[Wrap(U)]; };
		const auto Vertex = [&](const int32 Point) -> FVector& { return Positions[Base + Point * Stride]; };
		const auto PathOrigin = [&](const int32 Point) { return MeshTransform.InverseTransformPosition(Path->GetPos_Unsafe(Point)); };

		// The track runs backward along its edge: the inner side of a bend folding over itself.
		const auto IsReversed = [&](const int32 U)
		{
			const PCGExPaths::FPathEdge& Edge = EdgeAt(U);
			return FVector::DotProduct(Vertex(Edge.End) - Vertex(Edge.Start), EdgeDirs[Wrap(U)]) < 0;
		};

		// A loop is walked from a forward edge so no run is split across its seam.
		int32 First = 0;
		if (bClosedLoop)
		{
			First = INDEX_NONE;
			for (int32 e = 0; e < NumEdges; e++)
			{
				if (!IsReversed(e))
				{
					First = e;
					break;
				}
			}

			if (First == INDEX_NONE)
			{
				return false;
			}
		}

		const int32 Last = First + NumEdges; // Exclusive

		TArray<int32, TInlineAllocator<32>> Before;
		TArray<int32, TInlineAllocator<32>> After;

		bool bCollapsed = false;
		int32 U = First;

		while (U < Last)
		{
			if (!IsReversed(U))
			{
				U++;
				continue;
			}

			int32 RunEnd = U;
			while (RunEnd + 1 < Last && IsReversed(RunEnd + 1))
			{
				RunEnd++;
			}

			const int32 RunStart = U;
			U = RunEnd + 1;

			double Offset = 0;
			for (int32 R = RunStart; R <= RunEnd; R++)
			{
				const PCGExPaths::FPathEdge& Edge = EdgeAt(R);
				Offset = FMath::Max3(Offset, FVector::Distance(Vertex(Edge.Start), PathOrigin(Edge.Start)), FVector::Distance(Vertex(Edge.End), PathOrigin(Edge.End)));
			}

			const double Window = Offset * FoldSearchRatio;
			if (Window <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			// The fold lies in the plane of the path tangent and the track's offset from it.
			const int32 MidPoint = EdgeAt((RunStart + RunEnd) / 2).Start;
			const FVector Tangent = GetTangent(MidPoint);
			const FVector PlaneNormal = FVector::CrossProduct(Tangent, Vertex(MidPoint) - PathOrigin(MidPoint)).GetSafeNormal();
			if (PlaneNormal.IsZero())
			{
				continue;
			}

			const FVector AxisU = (Tangent - PlaneNormal * FVector::DotProduct(Tangent, PlaneNormal)).GetSafeNormal();
			const FVector AxisV = FVector::CrossProduct(PlaneNormal, AxisU);
			const auto To2D = [&](const FVector& P) { return FVector2D(FVector::DotProduct(P, AxisU), FVector::DotProduct(P, AxisV)); };

			// Forward edges on each side of the run, nearest first. Near* count the ones the base window alone yields.
			const double ExtendedWindow = Offset * FoldExtendedSearchRatio;

			Before.Reset();
			int32 NearBefore = 0;
			double Arc = 0;
			for (int32 J = RunStart - 1; Arc <= ExtendedWindow && (bClosedLoop || J >= 0) && RunEnd - J < NumEdges; J--)
			{
				if (Arc <= Window)
				{
					NearBefore++;
				}
				Before.Add(J);
				Arc += EdgeAt(J).Length;
			}

			After.Reset();
			int32 NearAfter = 0;
			Arc = 0;
			for (int32 L = RunEnd + 1; Arc <= ExtendedWindow && (bClosedLoop || L < NumEdges) && L - RunStart < NumEdges; L++)
			{
				if (Arc <= Window)
				{
					NearAfter++;
				}
				After.Add(L);
				Arc += EdgeAt(L).Length;
			}

			if (Before.IsEmpty() || After.IsEmpty())
			{
				continue;
			}

			// True ends the search for this run: the loop was collapsed, or a far crossing was refused.
			const auto TryPair = [&](const int32 J, const int32 L, const bool bExtended)
			{
				const PCGExPaths::FPathEdge& EdgeJ = EdgeAt(J);
				const PCGExPaths::FPathEdge& EdgeL = EdgeAt(L);
				const FVector P0 = Vertex(EdgeJ.Start);
				const FVector P1 = Vertex(EdgeJ.End);
				const FVector Q0 = Vertex(EdgeL.Start);
				const FVector Q1 = Vertex(EdgeL.End);

				const FVector2D A0 = To2D(P0);
				const FVector2D DA = To2D(P1) - A0;
				const FVector2D B0 = To2D(Q0);
				const FVector2D DB = To2D(Q1) - B0;

				const double Denom = FVector2D::CrossProduct(DA, DB);
				if (FMath::Abs(Denom) <= UE_DOUBLE_SMALL_NUMBER * DA.Size() * DB.Size())
				{
					return false;
				}

				const FVector2D Delta = B0 - A0;
				const double AlphaJ = FVector2D::CrossProduct(Delta, DB) / Denom;
				const double AlphaL = FVector2D::CrossProduct(Delta, DA) / Denom;
				if (AlphaJ < 0 || AlphaJ > 1 || AlphaL < 0 || AlphaL > 1)
				{
					return false;
				}

				const FVector PJ = FMath::Lerp(P0, P1, AlphaJ);
				const FVector PL = FMath::Lerp(Q0, Q1, AlphaL);

				if (bExtended)
				{
					// A far crossing must be real in 3D, not only in projection.
					const double TrackOffset = 0.5 * (FVector::Distance(P0, PathOrigin(EdgeJ.Start)) + FVector::Distance(Q1, PathOrigin(EdgeL.End)));
					if (FVector::Distance(PJ, PL) > TrackOffset * FoldExtendedGapRatio)
					{
						return true;
					}

					// Two legs' tracks only cross as a fold when the path turns less than half a turn between them.
					double Turn = 0;
					for (int32 E = J; E < L; E++)
					{
						const FVector2D DirA = To2D(EdgeDirs[Wrap(E)]);
						const FVector2D DirB = To2D(EdgeDirs[Wrap(E + 1)]);
						Turn += FMath::Atan2(FVector2D::CrossProduct(DirA, DirB), FVector2D::DotProduct(DirA, DirB));
					}

					if (FMath::Abs(Turn) >= UE_DOUBLE_PI)
					{
						return true;
					}
				}

				// Averaged in 3D: the two tracks only cross in projection when the path is not planar.
				const FVector Crossing = (PJ + PL) * 0.5;
				for (int32 K = J + 1; K <= L; K++)
				{
					Vertex(EdgeAt(K).Start) = Crossing;
				}

				bCollapsed = true;
				U = FMath::Max(U, L + 1);
				return true;
			};

			// Nearest pairs first, so the smallest loop is the one removed.
			bool bDone = false;
			const auto SearchPairs = [&](const int32 NumBefore, const int32 NumAfter, const bool bExtended)
			{
				const int32 MaxSum = NumBefore + NumAfter - 2;
				for (int32 Sum = 0; !bDone && Sum <= MaxSum; Sum++)
				{
					for (int32 Bi = FMath::Max(0, Sum - (NumAfter - 1)); !bDone && Bi <= FMath::Min(Sum, NumBefore - 1); Bi++)
					{
						const int32 Ai = Sum - Bi;
						if (bExtended && Bi < NearBefore && Ai < NearAfter)
						{
							// Already tested by the base pass.
							continue;
						}

						const int32 J = Before[Bi];
						const int32 L = After[Ai];
						if (L - J >= NumEdges)
						{
							continue;
						}

						bDone = TryPair(J, L, bExtended);
					}
				}
			};

			SearchPairs(NearBefore, NearAfter, false);
			if (!bDone && (Before.Num() > NearBefore || After.Num() > NearAfter))
			{
				SearchPairs(Before.Num(), After.Num(), true);
			}
		}

		return bCollapsed;
	}

	void FProcessor::BuildMesh()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::BuildMesh);

		const int32 NumVertices = Positions.Num();
		const int32 NumSheets = Sheets.Num();

		// One span per edge. A loop's last span wraps onto the first column, which then needs its own UVs.
		const int32 NumSpans = Path->NumEdges;
		const int32 NumUVColumns = NumSpans + 1;

		double UScale = 1.0 / FMath::Max(Settings->TileSize.X, UE_DOUBLE_KINDA_SMALL_NUMBER);
		if (!Context->bUDistance)
		{
			UScale = Path->TotalLength > UE_DOUBLE_SMALL_NUMBER ? 1.0 / Path->TotalLength : 0.0;
		}

		TArray<double> ColumnU;
		ColumnU.SetNumUninitialized(NumUVColumns);
		ColumnU[0] = 0;

		double PathDistance = 0;
		for (int32 i = 0; i < NumSpans; i++)
		{
			PathDistance += Path->Edges[i].Length;
			ColumnU[i + 1] = PathDistance * UScale;
		}

		TArray<int32> UVOffsets;
		UVOffsets.SetNumUninitialized(NumSheets);

		int32 NumUVElements = 0;
		int32 NumTriangles = 0;
		for (int32 s = 0; s < NumSheets; s++)
		{
			const FProfile& Profile = *Sheets[s].Profile;
			UVOffsets[s] = NumUVElements;
			NumUVElements += NumUVColumns * Profile.NumUVRows;
			NumTriangles += NumSpans * Profile.NumRows * 2;
		}

		// Read by attribute UV channels only: one value per path point, shared by its whole column.
		TArray<int32> TriangleIDs;
		TArray<int32> VertexPointIndices;
		if (UVDetails.NumChannels > 0)
		{
			TriangleIDs.SetNumUninitialized(NumTriangles);
			for (int32 i = 0; i < NumTriangles; i++)
			{
				TriangleIDs[i] = i;
			}

			VertexPointIndices.SetNumUninitialized(NumVertices);
			for (const FSheet& Sheet : Sheets)
			{
				const int32 NumProfilePoints = Sheet.Profile->Num;
				for (int32 Column = 0; Column < NumPoints; Column++)
				{
					const int32 Start = Sheet.VertexOffset + Column * NumProfilePoints;
					for (int32 i = 0; i < NumProfilePoints; i++)
					{
						VertexPointIndices[Start + i] = Column;
					}
				}
			}
		}

		// The mesh starts empty and hands out ids in append order: every vertex, element and triangle id below is its
		// own index. Appends are verified against that, and a triangle cannot be refused on a grid with unique corners.
		InternalMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& InMesh)
		{
			InMesh.EnableAttributes();
			InMesh.Attributes()->EnablePrimaryColors();
			InMesh.Attributes()->EnableMaterialID();

			UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = InMesh.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

			for (int32 i = 0; i < NumVertices; i++)
			{
				verify(InMesh.AppendVertex(Positions[i]) == i);
				verify(ColorOverlay->AppendElement(Colors[i]) == i);
			}

			int32 TriangleID = 0;
			for (int32 s = 0; s < NumSheets; s++)
			{
				const FSheet& Sheet = Sheets[s];
				const FProfile& Profile = *Sheet.Profile;
				const int32 (&Triangles)[2][3] = QuadTriangles[Sheet.bFlipWinding ? 1 : 0];

				for (int32 Span = 0; Span < NumSpans; Span++)
				{
					const int32 Column = Sheet.VertexOffset + Span * Profile.Num;
					const int32 NextColumn = Sheet.VertexOffset + ((Span + 1) % NumPoints) * Profile.Num;

					for (int32 Row = 0; Row < Profile.NumRows; Row++)
					{
						// A closed profile's last row wraps onto its first point.
						const int32 NextRow = (Row + 1) % Profile.Num;
						const int32 Corners[4] = {Column + Row, Column + NextRow, NextColumn + Row, NextColumn + NextRow};

						for (int32 t = 0; t < 2; t++)
						{
							const UE::Geometry::FIndex3i Triangle(Corners[Triangles[t][0]], Corners[Triangles[t][1]], Corners[Triangles[t][2]]);

							// One polygroup per sheet
							verify(InMesh.AppendTriangle(Triangle, s) == TriangleID);

							MaterialID->SetValue(TriangleID, Sheet.MaterialIndex);
							ColorOverlay->SetTriangle(TriangleID, Triangle);
							TriangleID++;
						}
					}
				}
			}

			// Attribute channels first: writing them resizes the UV layers to their own count.
			UVDetails.Write(TriangleIDs, VertexPointIndices, InMesh);

			if (InMesh.Attributes()->NumUVLayers() <= UVChannel)
			{
				InMesh.Attributes()->SetNumUVLayers(UVChannel + 1);
			}

			UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = InMesh.Attributes()->GetUVLayer(UVChannel);

			TArray<float> RowV;
			for (int32 s = 0; s < NumSheets; s++)
			{
				const FSheet& Sheet = Sheets[s];
				const FProfile& Profile = *Sheet.Profile;

				RowV.SetNumUninitialized(Profile.NumUVRows, EAllowShrinking::No);
				for (int32 Row = 0; Row < Profile.NumUVRows; Row++)
				{
					RowV[Row] = static_cast<float>(Sheet.VStart + Sheet.VScale * Profile.Alphas[Row]);
				}

				// A loop's extra UV column repeats the first column's V at the far end of U.
				for (int32 UVColumn = 0; UVColumn < NumUVColumns; UVColumn++)
				{
					const float U = static_cast<float>(ColumnU[UVColumn]);
					const int32 UVStart = UVOffsets[s] + UVColumn * Profile.NumUVRows;

					for (int32 Row = 0; Row < Profile.NumUVRows; Row++)
					{
						verify(UVOverlay->AppendElement(FVector2f(U, RowV[Row])) == UVStart + Row);
					}
				}
			}

			// Same walk as the triangle pass, so ids line up. UV rows and columns never wrap.
			TriangleID = 0;
			for (int32 s = 0; s < NumSheets; s++)
			{
				const FSheet& Sheet = Sheets[s];
				const FProfile& Profile = *Sheet.Profile;
				const int32 (&Triangles)[2][3] = QuadTriangles[Sheet.bFlipWinding ? 1 : 0];

				for (int32 Span = 0; Span < NumSpans; Span++)
				{
					const int32 UVColumn = UVOffsets[s] + Span * Profile.NumUVRows;
					const int32 NextUVColumn = UVColumn + Profile.NumUVRows;

					for (int32 Row = 0; Row < Profile.NumRows; Row++)
					{
						const int32 UVCorners[4] = {UVColumn + Row, UVColumn + Row + 1, NextUVColumn + Row, NextUVColumn + Row + 1};

						for (int32 t = 0; t < 2; t++)
						{
							UVOverlay->SetTriangle(TriangleID++, UE::Geometry::FIndex3i(UVCorners[Triangles[t][0]], UVCorners[Triangles[t][1]], UVCorners[Triangles[t][2]]));
						}
					}
				}
			}

			if (ClearanceUVChannel != INDEX_NONE)
			{
				if (InMesh.Attributes()->NumUVLayers() <= ClearanceUVChannel)
				{
					InMesh.Attributes()->SetNumUVLayers(ClearanceUVChannel + 1);
				}

				// One element per vertex, as for the colors: the values never split along a seam.
				UE::Geometry::FDynamicMeshUVOverlay* ClearanceOverlay = InMesh.Attributes()->GetUVLayer(ClearanceUVChannel);
				for (int32 i = 0; i < NumVertices; i++)
				{
					verify(ClearanceOverlay->AppendElement(ClearanceUVs[i]) == i);
				}

				for (int32 i = 0; i < NumTriangles; i++)
				{
					ClearanceOverlay->SetTriangle(i, InMesh.GetTriangle(i));
				}
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);

		if (bFoldsCollapsed)
		{
			// Collapsed vertices sit on the exact same position, so near-zero thresholds weld them without touching
			// small but legitimate triangles.
			FGeometryScriptDegenerateTriangleOptions RepairOptions;
			RepairOptions.Mode = EGeometryScriptRepairMeshMode::RepairOrDelete;
			RepairOptions.MinTriangleArea = UE_DOUBLE_SMALL_NUMBER;
			RepairOptions.MinEdgeLength = UE_DOUBLE_SMALL_NUMBER;
			RepairOptions.bCompactOnCompletion = true;
			UGeometryScriptLibrary_MeshRepairFunctions::RepairMeshDegenerateGeometry(InternalMesh, RepairOptions);
		}

		Settings->Topology.PostProcessMesh(InternalMesh);
	}

	void FProcessor::Output()
	{
		if (!this->bIsProcessorValid)
		{
			return;
		}

		if (InternalMeshData)
		{
			Context->StageOutput(
				InternalMeshData, PCGExTopology::Labels::OutputMeshLabel, PCGExData::EStaging::Managed,
				PointDataFacade->Source->Tags->Flatten());
		}
	}
}

#pragma endregion

#pragma region FBatch

namespace PCGExTopologyPathSweep
{
	FBatch::FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
		: TBatch(InContext, InPointsCollection)
	{
	}

	void FBatch::OnInitialPostProcess()
	{
		TBatch<FProcessor>::OnInitialPostProcess();

		const auto IsQueryable = [](const FProcessor& P)
		{
			return P.bIsProcessorValid && P.bClearanceReady && !P.Path->Bounds.ContainsNaN();
		};

		FBox World(ForceInit);
		for (const TSharedRef<PCGExPointsMT::IProcessor>& Processor : Processors)
		{
			const FProcessor& P = static_cast<const FProcessor&>(Processor.Get());
			if (IsQueryable(P))
			{
				World += P.Path->Bounds;
				MaxReach = FMath::Max(MaxReach, P.MaxReach);
				MaxBand = FMath::Max(MaxBand, P.MaxBand);
			}
		}

		if (!World.IsValid)
		{
			return;
		}

		// Padded so a flat path still has a volume to intersect.
		World = World.ExpandBy(UE_KINDA_SMALL_NUMBER);
		PathOctree = MakeShared<PCGExOctree::FItemOctree>(World.GetCenter(), World.GetExtent().Length());

		for (int32 Pi = 0; Pi < Processors.Num(); Pi++)
		{
			const FProcessor& P = static_cast<const FProcessor&>(Processors[Pi].Get());
			if (IsQueryable(P))
			{
				PathOctree->AddElement(PCGExOctree::FItem(Pi, P.Path->Bounds.ExpandBy(UE_KINDA_SMALL_NUMBER)));
			}
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
