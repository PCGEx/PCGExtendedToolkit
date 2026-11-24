// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTopologyPathSweep.h"

#include "PCGExCoreSettingsCache.h"
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
#include "Paths/PCGExPathProfile.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExTopologyPathSweepElement"
#define PCGEX_NAMESPACE TopologyPathSweep

PCGEX_INITIALIZE_ELEMENT(TopologyPathSweep)
PCGEX_ELEMENT_BATCH_POINT_IMPL(TopologyPathSweep)

#pragma region PCGExTopologyPathSweep helpers

namespace PCGExTopologyPathSweep
{
	// Corner order per quad: (c, j), (c, j+1), (c+1, j), (c+1, j+1). Indexed by [flipped][triangle].
	constexpr int32 QuadTriangles[2][2][3] = {{{0, 1, 2}, {2, 1, 3}}, {{0, 2, 1}, {2, 3, 1}}};

	FVector2D FProfile::GetPositionAt(const double Alpha) const
	{
		const double SafeAlpha = FMath::IsFinite(Alpha) ? FMath::Clamp(Alpha, 0.0, 1.0) : 0.0;
		const int32 Next = FMath::Clamp(static_cast<int32>(Algo::UpperBound(Alphas, SafeAlpha)), 1, Num - 1);
		const int32 Prev = Next - 1;

		const double Span = Alphas[Next] - Alphas[Prev];
		const double Ratio = Span > UE_DOUBLE_SMALL_NUMBER ? (SafeAlpha - Alphas[Prev]) / Span : 0.0;
		return FMath::Lerp(Positions[Prev], Positions[Next], Ratio);
	}

	TSharedPtr<FProfile> BuildProfile(FPCGExContext* InContext, const FName InPinLabel, TSharedPtr<PCGExData::FFacade>& OutFacade)
	{
		TArray<FVector> Normalized;
		if (!PCGExPaths::Profile::TryBuildCustomProfile(InContext, InPinLabel, OutFacade, Normalized))
		{
			return nullptr;
		}

		const UPCGBasePointData* ProfileData = OutFacade->GetIn();
		const TConstPCGValueRange<FTransform> Transforms = ProfileData->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector4> InColors = ProfileData->GetConstColorValueRange();

		const int32 Num = Normalized.Num();

		TSharedPtr<FProfile> Profile = MakeShared<FProfile>();
		Profile->Num = Num;
		Profile->Positions.SetNumUninitialized(Num);
		Profile->Alphas.SetNumUninitialized(Num);
		Profile->Colors.SetNumUninitialized(Num);

		double Length = 0;
		double MaxSideways = 0;
		double MaxDropped = 0;

		for (int32 i = 0; i < Num; i++)
		{
			const FVector& Position = Normalized[i];
			Profile->Positions[i] = FVector2D(Position.X, Position.Y);
			Profile->Colors[i] = InColors[i];

			if (i > 0)
			{
				Length += FVector2D::Distance(Profile->Positions[i - 1], Profile->Positions[i]);
			}
			Profile->Alphas[i] = Length;

			MaxSideways = FMath::Max(MaxSideways, FMath::Abs(Position.Y));
			MaxDropped = FMath::Max(MaxDropped, FMath::Abs(Position.Z));
		}

		// Length >= 1: the normalized chord alone spans 0..1 on X.
		for (double& Alpha : Profile->Alphas)
		{
			Alpha /= Length;
		}

		Profile->RawLength = Length * FVector::Dist(Transforms[0].GetLocation(), Transforms[Num - 1].GetLocation());

		if (MaxDropped > UE_KINDA_SMALL_NUMBER && MaxSideways <= UE_KINDA_SMALL_NUMBER)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' has no sideways extent once flattened: profiles are read in their local XY plane, and Z is dropped."), FText::FromName(InPinLabel)));
		}

		return Profile;
	}
}

#pragma endregion

#pragma region UPCGExTopologyPathSweepSettings

TArray<FPCGPinProperties> UPCGExTopologyPathSweepSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExTopologyPathSweep::SourceProfile, "Single path used as the profile, drawn in its local XY plane. Its first-to-last span is the depth axis.", Required)
	if (Mode == EPCGExPathSweepMode::Dual)
	{
		PCGEX_PIN_POINT(PCGExTopologyPathSweep::SourceSecondaryProfile, "Single path used as the secondary profile. The primary profile is used when unconnected.", Normal)
	}
	return PinProperties;
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

	if (Settings->SecondaryMaterial.ToSoftObjectPath().IsValid())
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

	Context->Profile = PCGExTopologyPathSweep::BuildProfile(Context, PCGExTopologyPathSweep::SourceProfile, Context->ProfileFacade);
	if (!Context->Profile)
	{
		return false;
	}

	switch (Settings->Mode)
	{
	case EPCGExPathSweepMode::Single:
		break;
	case EPCGExPathSweepMode::Dual:
		if (Context->InputData.GetInputCountByPin(PCGExTopologyPathSweep::SourceSecondaryProfile) > 0)
		{
			Context->SecondaryProfile = PCGExTopologyPathSweep::BuildProfile(Context, PCGExTopologyPathSweep::SourceSecondaryProfile, Context->SecondaryProfileFacade);
			if (!Context->SecondaryProfile)
			{
				return false;
			}
		}
		else
		{
			Context->SecondaryProfile = Context->Profile;
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

		Sheet.Reference = Details.Reference.GetValueSetting();
		if (!Sheet.Reference->Init(PointDataFacade))
		{
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
		Sheets[0].bChordOnY = Context->bPrimaryOnY;

		if (bDual)
		{
			if (!InitSheet(Sheets[1], Settings->Secondary, Context->SecondaryProfile))
			{
				return false;
			}

			Sheets[1].bChordOnY = !Context->bPrimaryOnY;
			Sheets[1].MaterialIndex = 1;
			// The secondary profile runs against the unfolded strip, so its faces are flipped to match the primary's.
			Sheets[1].bFlipWinding = true;

			const double Split = Context->VSplit;
			Sheets[1].VStart = Split;
			Sheets[1].VScale = 1.0 - Split;

			if (Context->bVContinuous)
			{
				Sheets[0].VStart = Split;
				Sheets[0].VScale = -Split;
				Sheets[0].VDistanceSign = -1;
			}
			else
			{
				Sheets[0].VScale = Split;
				Sheets[1].bVAfterPrimary = true;
			}
		}

		int64 NumVertices = 0;
		for (FSheet& Sheet : Sheets)
		{
			Sheet.VertexOffset = static_cast<int32>(NumVertices);
			NumVertices += static_cast<int64>(NumPoints) * Sheet.Profile->Num;
		}

		// Two triangles per vertex, and triangle ids are int32.
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

		InternalMesh = Context->ManagedObjects->New<UDynamicMesh>();
		InternalMesh->InitializeMesh();

		InternalMeshData->Initialize(InternalMesh, true);
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

		UVDetails = Settings->Topology.UVChannels;
		UVDetails.Prepare(PointDataFacade);

		MeshTransform = PCGExTopology::GetCoordinateSpaceTransform(Settings->Topology.CoordinateSpace, Context);

		Positions.SetNumUninitialized(static_cast<int32>(NumVertices));
		Colors.SetNumUninitialized(static_cast<int32>(NumVertices));
		if (Context->bVDistance)
		{
			VDistances.SetNumUninitialized(static_cast<int32>(NumVertices));
		}

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
		const bool bVDistance = Context->bVDistance;

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

				const FVector& ChordAxis = Sheet.bChordOnY ? AxisY : AxisZ;
				const FVector& SidewaysAxis = Sheet.bChordOnY ? AxisZ : AxisY;

				const FVector2D Scale(Sheet.Depth->Read(Index), Sheet.Width->Read(Index));
				const FVector2D Anchor = Profile.GetPositionAt(Sheet.Reference->Read(Index));

				const int32 Start = Sheet.VertexOffset + Index * Profile.Num;

				FVector2D PrevLocal = FVector2D::ZeroVector;
				double ProfileDistance = 0;

				for (int32 i = 0; i < Profile.Num; i++)
				{
					const FVector2D Local = (Profile.Positions[i] - Anchor) * Scale;
					Positions[Start + i] = Origin + ChordAxis * Local.X + SidewaysAxis * Local.Y;

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

					if (bVDistance)
					{
						if (i > 0)
						{
							ProfileDistance += FVector2D::Distance(PrevLocal, Local);
						}
						VDistances[Start + i] = static_cast<float>(ProfileDistance);
						PrevLocal = Local;
					}
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		BuildMesh();

		Positions.Empty();
		Colors.Empty();
		VDistances.Empty();
	}

	void FProcessor::BuildMesh()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExTopologyPathSweep::BuildMesh);

		const int32 NumVertices = Positions.Num();
		const int32 NumSheets = Sheets.Num();

		// One span per edge. A loop's last span wraps onto the first column, which then needs its own UVs.
		const int32 NumSpans = Path->NumEdges;
		const int32 NumUVColumns = NumSpans + 1;

		const FVector2D TileSize(FMath::Max(Settings->TileSize.X, UE_DOUBLE_KINDA_SMALL_NUMBER), FMath::Max(Settings->TileSize.Y, UE_DOUBLE_KINDA_SMALL_NUMBER));
		const bool bVDistance = Context->bVDistance;

		double UScale = 1.0 / TileSize.X;
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
			const int32 NumProfilePoints = Sheets[s].Profile->Num;
			UVOffsets[s] = NumUVElements;
			NumUVElements += NumUVColumns * NumProfilePoints;
			NumTriangles += NumSpans * (NumProfilePoints - 1) * 2;
		}

		// UV channels read from attributes get one value per path point, shared by its whole column.
		TArray<int32> VertexPointIndices;
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

		TArray<int32> VertexIDs;
		TArray<int32> ColorIDs;
		TArray<int32> UVIDs;
		TArray<int32> TriangleIDs;
		TArray<UE::Geometry::FIndex3i> TriangleUVs;

		VertexIDs.SetNumUninitialized(NumVertices);
		ColorIDs.SetNumUninitialized(NumVertices);
		UVIDs.SetNumUninitialized(NumUVElements);
		TriangleIDs.Reserve(NumTriangles);
		TriangleUVs.Reserve(NumTriangles);

		const int32 UVChannel = FMath::Clamp(Settings->UVChannel, 0, 7);

		InternalMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& InMesh)
		{
			InMesh.EnableAttributes();
			InMesh.Attributes()->EnablePrimaryColors();
			InMesh.Attributes()->EnableMaterialID();

			UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = InMesh.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialID = InMesh.Attributes()->GetMaterialID();

			for (int32 i = 0; i < NumVertices; i++)
			{
				VertexIDs[i] = InMesh.AppendVertex(Positions[i]);
				ColorIDs[i] = ColorOverlay->AppendElement(Colors[i]);
			}

			for (int32 s = 0; s < NumSheets; s++)
			{
				const FSheet& Sheet = Sheets[s];
				const int32 NumProfilePoints = Sheet.Profile->Num;
				const int32 (&Triangles)[2][3] = QuadTriangles[Sheet.bFlipWinding ? 1 : 0];

				for (int32 Span = 0; Span < NumSpans; Span++)
				{
					const int32 Column = Sheet.VertexOffset + Span * NumProfilePoints;
					const int32 NextColumn = Sheet.VertexOffset + ((Span + 1) % NumPoints) * NumProfilePoints;

					const int32 UVColumn = UVOffsets[s] + Span * NumProfilePoints;
					const int32 NextUVColumn = UVColumn + NumProfilePoints;

					for (int32 i = 0; i < NumProfilePoints - 1; i++)
					{
						const int32 Corners[4] = {Column + i, Column + i + 1, NextColumn + i, NextColumn + i + 1};
						const int32 UVCorners[4] = {UVColumn + i, UVColumn + i + 1, NextUVColumn + i, NextUVColumn + i + 1};

						for (int32 t = 0; t < 2; t++)
						{
							const int32 A = Triangles[t][0];
							const int32 B = Triangles[t][1];
							const int32 C = Triangles[t][2];

							const int32 TriangleID = InMesh.AppendTriangle(VertexIDs[Corners[A]], VertexIDs[Corners[B]], VertexIDs[Corners[C]]);
							if (TriangleID < 0)
							{
								continue;
							}

							MaterialID->SetValue(TriangleID, Sheet.MaterialIndex);
							ColorOverlay->SetTriangle(TriangleID, UE::Geometry::FIndex3i(ColorIDs[Corners[A]], ColorIDs[Corners[B]], ColorIDs[Corners[C]]));

							TriangleIDs.Add(TriangleID);
							TriangleUVs.Emplace(UVCorners[A], UVCorners[B], UVCorners[C]);
						}
					}
				}
			}

			// Attribute channels first: they size the UV layers, and the generated channel wins a shared index.
			UVDetails.Write(TriangleIDs, VertexPointIndices, InMesh);

			if (InMesh.Attributes()->NumUVLayers() <= UVChannel)
			{
				InMesh.Attributes()->SetNumUVLayers(UVChannel + 1);
			}

			UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = InMesh.Attributes()->GetUVLayer(UVChannel);
			UVOverlay->ClearElements();

			const FSheet& PrimarySheet = Sheets[0];
			const int32 PrimaryLast = PrimarySheet.Profile->Num - 1;

			for (int32 s = 0; s < NumSheets; s++)
			{
				const FSheet& Sheet = Sheets[s];
				const FProfile& Profile = *Sheet.Profile;

				for (int32 UVColumn = 0; UVColumn < NumUVColumns; UVColumn++)
				{
					// A loop's extra UV column sits on the first point's vertices.
					const int32 Column = UVColumn % NumPoints;
					const int32 Start = Sheet.VertexOffset + Column * Profile.Num;
					const int32 UVStart = UVOffsets[s] + UVColumn * Profile.Num;

					double VOffset = 0;
					if (bVDistance && Sheet.bVAfterPrimary)
					{
						VOffset = VDistances[PrimarySheet.VertexOffset + Column * PrimarySheet.Profile->Num + PrimaryLast];
					}

					for (int32 i = 0; i < Profile.Num; i++)
					{
						const double V = bVDistance
							                 ? (VOffset + Sheet.VDistanceSign * VDistances[Start + i]) / TileSize.Y
							                 : Sheet.VStart + Sheet.VScale * Profile.Alphas[i];

						UVIDs[UVStart + i] = UVOverlay->AppendElement(FVector2f(static_cast<float>(ColumnU[UVColumn]), static_cast<float>(V)));
					}
				}
			}

			for (int32 i = 0; i < TriangleIDs.Num(); i++)
			{
				const UE::Geometry::FIndex3i& Elements = TriangleUVs[i];
				UVOverlay->SetTriangle(TriangleIDs[i], UE::Geometry::FIndex3i(UVIDs[Elements.A], UVIDs[Elements.B], UVIDs[Elements.C]));
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
