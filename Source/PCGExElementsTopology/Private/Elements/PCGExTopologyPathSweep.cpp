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
#include "Math/PCGExMathAxis.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExTopologyPathSweepElement"
#define PCGEX_NAMESPACE TopologyPathSweep

PCGEX_INITIALIZE_ELEMENT(TopologyPathSweep)
PCGEX_ELEMENT_BATCH_POINT_IMPL(TopologyPathSweep)

#pragma region PCGExTopologyPathSweep helpers

namespace PCGExTopologyPathSweep
{
	// Corner order per quad: (column, row), (column, row+1), (column+1, row), (column+1, row+1). Indexed by [flipped][triangle].
	constexpr int32 QuadTriangles[2][2][3] = {{{0, 1, 2}, {2, 1, 3}}, {{0, 2, 1}, {2, 3, 1}}};

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

	for (const FPCGExUVInputDetails& Channel : Settings->Topology.UVChannels.UVs)
	{
		if (Channel.bEnabled && !Channel.AttributeName.IsNone() && Channel.Channel == Settings->UVChannel)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(FTEXT("UV channel {0} is also fed by '{1}': the generated UVs replace it."), FText::AsNumber(Settings->UVChannel), FText::FromName(Channel.AttributeName)));
			break;
		}
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
				NewBatch->bSkipCompletion = true;
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

		UVDetails = Settings->Topology.UVChannels;
		// The generated UVs own their channel: an attribute channel on the same index is not read at all.
		for (FPCGExUVInputDetails& Channel : UVDetails.UVs)
		{
			if (Channel.Channel == UVChannel)
			{
				Channel.bEnabled = false;
			}
		}
		UVDetails.Prepare(PointDataFacade);

		MeshTransform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		Positions.SetNumUninitialized(static_cast<int32>(NumVertices));
		Colors.SetNumUninitialized(static_cast<int32>(NumVertices));

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::ProcessPoints);

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector4> InColors = InPointData->GetConstColorValueRange();

		const PCGExBlending::IBlendOperation* ColorBlend = Context->ColorBlend.Get();
		const bool bProfileColorOnly = Context->bProfileColorOnly;

		const int32 LastEdge = Path->LastEdge;

		PCGEX_SCOPE_LOOP(Index)
		{
			// An open path's endpoints only have one edge, which then stands for both sides.
			const int32 OutEdge = bClosedLoop ? Index : FMath::Min(Index, LastEdge);
			const int32 InEdge = bClosedLoop ? (Index == 0 ? LastEdge : Index - 1) : FMath::Max(Index - 1, 0);

			const FVector& DirIn = EdgeDirs[InEdge];
			const FVector& DirOut = EdgeDirs[OutEdge];

			// Profiles sit in the plane bisecting the corner. A full reversal has no bisector.
			FVector Tangent = (DirIn + DirOut).GetSafeNormal();
			if (Tangent.IsZero())
			{
				Tangent = DirOut;
			}

			FVector AxisY;
			if (UpGetter)
			{
				AxisY = PCGExMath::SafeCrossNormal(UpGetter->Read(Index), Tangent);
			}
			else
			{
				const FVector Side = SideNormal->Get(OutEdge);
				AxisY = (Side - FVector::DotProduct(Side, Tangent) * Tangent).GetSafeNormal();
				if (AxisY.IsZero())
				{
					AxisY = PCGExMath::SafeCrossNormal(WorldUp, Tangent);
				}
			}

			FVector AxisZ = FVector::CrossProduct(Tangent, AxisY);

			// Miter: stretch across the bend so each segment keeps the profile's size once projected onto it.
			const FVector Bend = DirOut - DirIn;
			if (Bend.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
			{
				const FVector MiterAxis = Bend.GetSafeNormal();
				const double CosHalfAngle = FVector::DotProduct(Tangent, DirOut);
				const double Stretch = CosHalfAngle * MiterLimit > 1.0 ? 1.0 / CosHalfAngle : MiterLimit;

				AxisY += MiterAxis * (FVector::DotProduct(AxisY, MiterAxis) * (Stretch - 1.0));
				AxisZ += MiterAxis * (FVector::DotProduct(AxisZ, MiterAxis) * (Stretch - 1.0));
			}

			// The mesh transform is translation-only, so the axes need none of it.
			const FVector Origin = MeshTransform.InverseTransformPosition(InTransforms[Index].GetLocation());

			const FVector4 PathColor = InColors[Index];
			const double ColorWeight = ColorBlendWeight ? ColorBlendWeight->Read(Index) : 1.0;

			for (const FSheet& Sheet : Sheets)
			{
				const FProfile& Profile = *Sheet.Profile;

				const FVector& DepthAxis = Sheet.bDepthOnY ? AxisY : AxisZ;
				const FVector& WidthAxis = Sheet.bDepthOnY ? AxisZ : AxisY;

				const FVector2D Scale(Sheet.Depth->Read(Index), Sheet.Width->Read(Index));
				const FVector2D Anchor = Sheet.Reference ? Profile.GetPositionAt(Sheet.Reference->Read(Index)) : Sheet.FixedAnchor;

				const int32 Start = Sheet.VertexOffset + Index * Profile.Num;

				for (int32 i = 0; i < Profile.Num; i++)
				{
					const FVector2D Local = (Profile.Positions[i] - Anchor) * Scale;
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

		BuildMesh();

		Positions.Empty();
		Colors.Empty();
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
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);

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

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
