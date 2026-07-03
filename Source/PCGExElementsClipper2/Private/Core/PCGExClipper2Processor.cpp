// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClipper2Processor.h"

#include "Async/ParallelFor.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Clipper2Lib/clipper.h"
#include "Core/PCGExClipper2Common.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExAsyncHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExMathDistances.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2ProcessorElement"

namespace PCGExClipper2
{
#pragma region FOpData

	FOpData::FOpData(const int32 InReserve)
	{
		AddReserve(InReserve);
	}

	void FOpData::AddReserve(const int32 InReserve)
	{
		const int32 Reserve = Paths.Num() + InReserve;
		Facades.Reserve(Reserve);
		Paths.Reserve(Reserve);
		IsClosedLoop.Reserve(Reserve);
		Projections.Reserve(Reserve);
		ProjectedZValues.Reserve(Reserve);
	}

	void FOpData::CollectPaths(const TArray<int32>& Indices, PCGExClipper2Lib::Paths64& OutClosed, PCGExClipper2Lib::Paths64& OutOpen) const
	{
		OutClosed.reserve(OutClosed.size() + Indices.Num());
		OutOpen.reserve(OutOpen.size() + Indices.Num());
		for (const int32 Idx : Indices)
		{
			if (!Paths.IsValidIndex(Idx))
			{
				continue;
			}

			if (IsClosedLoop[Idx])
			{
				OutClosed.push_back(Paths[Idx]);
			}
			else
			{
				OutOpen.push_back(Paths[Idx]);
			}
		}
	}

#pragma endregion

#pragma region FProcessingGroup

	void FProcessingGroup::Prepare(const TSharedPtr<FOpData>& AllOpData)
	{
		GroupTags = MakeShared<PCGExData::FTags>();

		// Cache subject paths
		AllOpData->CollectPaths(SubjectIndices, SubjectPaths, OpenSubjectPaths);
		for (const int32 Idx : SubjectIndices)
		{
			GroupTags->Append(AllOpData->Facades[Idx]->Source->Tags.ToSharedRef());
		}

		// Cache operand paths, unless a shared cache was assigned upfront (identical operand set across groups).
		if (!OperandPaths)
		{
			OperandPaths = MakeShared<PCGExClipper2Lib::Paths64>();
			OpenOperandPaths = MakeShared<PCGExClipper2Lib::Paths64>();
			AllOpData->CollectPaths(OperandIndices, *OperandPaths, *OpenOperandPaths);
		}

		for (const int32 Idx : OperandIndices)
		{
			GroupTags->Append(AllOpData->Facades[Idx]->Source->Tags.ToSharedRef());
		}
	}

	void FProcessingGroup::PreProcess(const UPCGExClipper2ProcessorSettings* InSettings)
	{
		if (InSettings->bUnionGroupBeforeOperation && SubjectPaths.size() > 1)
		{
			PCGExClipper2Lib::Paths64 Union;
			PCGExClipper2Lib::Clipper64 Clipper;
			Clipper.SetZCallback(CreateZCallback());
			Clipper.AddSubject(SubjectPaths);
			Clipper.AddOpenSubject(OpenSubjectPaths);
			Clipper.Execute(PCGExClipper2Lib::ClipType::Union, PCGExClipper2Lib::FillRule::NonZero, Union);
			SubjectPaths = Union;
		}

		if (InSettings->bUnionOperandsBeforeOperation && OperandPaths && OperandPaths->size() > 1)
		{
			PCGExClipper2Lib::Paths64 Union;
			PCGExClipper2Lib::Clipper64 Clipper;
			Clipper.SetZCallback(CreateZCallback());
			Clipper.AddSubject(*OperandPaths);
			Clipper.AddOpenSubject(*OpenOperandPaths);
			Clipper.Execute(PCGExClipper2Lib::ClipType::Union, PCGExClipper2Lib::FillRule::NonZero, Union);

			// Copy-on-write: the operand cache may be shared with sibling groups.
			OperandPaths = MakeShared<PCGExClipper2Lib::Paths64>(MoveTemp(Union));
		}
	}

	void FProcessingGroup::AddIntersectionBlendInfo(int64_t X, int64_t Y, const FIntersectionBlendInfo& Info)
	{
		const uint64 Key = PCGEx::H64(static_cast<uint32>(X & 0xFFFFFFFF), static_cast<uint32>(Y & 0xFFFFFFFF));
		FScopeLock Lock(&IntersectionLock);
		IntersectionBlendInfos.Add(Key, Info);
	}

	const FIntersectionBlendInfo* FProcessingGroup::GetIntersectionBlendInfo(int64_t X, int64_t Y) const
	{
		const uint64 Key = PCGEx::H64(static_cast<uint32>(X & 0xFFFFFFFF), static_cast<uint32>(Y & 0xFFFFFFFF));
		return IntersectionBlendInfos.Find(Key);
	}

	PCGExClipper2Lib::ZCallback64 FProcessingGroup::CreateZCallback()
	{
		// Lifetime contract: every Clipper instance holding this callback is created and executed synchronously
		// inside a scope that keeps the group alive (Prepare/PreProcess/Process). Raw capture avoids per-intersection
		// weak-pointer pinning (atomic refcount churn) in what can be a very hot callback.
		FProcessingGroup* Group = this;

		return [Group](
			const PCGExClipper2Lib::Point64& e1bot, const PCGExClipper2Lib::Point64& e1top,
			const PCGExClipper2Lib::Point64& e2bot, const PCGExClipper2Lib::Point64& e2top,
			PCGExClipper2Lib::Point64& pt)
		{
			// Decode the source info from each vertex
			uint32 E1BotPtIdx, E1BotSrcIdx;
			uint32 E1TopPtIdx, E1TopSrcIdx;
			uint32 E2BotPtIdx, E2BotSrcIdx;
			uint32 E2TopPtIdx, E2TopSrcIdx;

			PCGEx::H64(static_cast<uint64>(e1bot.z), E1BotPtIdx, E1BotSrcIdx);
			PCGEx::H64(static_cast<uint64>(e1top.z), E1TopPtIdx, E1TopSrcIdx);
			PCGEx::H64(static_cast<uint64>(e2bot.z), E2BotPtIdx, E2BotSrcIdx);
			PCGEx::H64(static_cast<uint64>(e2top.z), E2TopPtIdx, E2TopSrcIdx);

			// Calculate alpha along each edge
			auto CalcAlpha = [](const PCGExClipper2Lib::Point64& Bot, const PCGExClipper2Lib::Point64& Top, const PCGExClipper2Lib::Point64& Pt) -> double
			{
				const double DX = static_cast<double>(Top.x - Bot.x);
				const double DY = static_cast<double>(Top.y - Bot.y);
				const double Len = FMath::Sqrt(DX * DX + DY * DY);
				if (Len < SMALL_NUMBER)
				{
					return 0.5;
				}

				const double PtDX = static_cast<double>(Pt.x - Bot.x);
				const double PtDY = static_cast<double>(Pt.y - Bot.y);
				return FMath::Clamp((PtDX * DX + PtDY * DY) / (Len * Len), 0.0, 1.0);
			};

			FIntersectionBlendInfo Info;
			Info.E1BotPointIdx = E1BotPtIdx;
			Info.E1BotSourceIdx = E1BotSrcIdx;
			Info.E1TopPointIdx = E1TopPtIdx;
			Info.E1TopSourceIdx = E1TopSrcIdx;
			Info.E2BotPointIdx = E2BotPtIdx;
			Info.E2BotSourceIdx = E2BotSrcIdx;
			Info.E2TopPointIdx = E2TopPtIdx;
			Info.E2TopSourceIdx = E2TopSrcIdx;
			Info.E1Alpha = CalcAlpha(e1bot, e1top, pt);
			Info.E2Alpha = CalcAlpha(e2bot, e2top, pt);

			// Store intersection info
			Group->AddIntersectionBlendInfo(pt.x, pt.y, Info);

			// Encode intersection marker in Z - use a special pattern
			// We mark it as an intersection point; the actual blend info is stored in the map
			pt.z = static_cast<int64_t>(PCGEx::H64(INTERSECTION_MARKER, INTERSECTION_MARKER));
		};
	}

#pragma endregion
}

UPCGExClipper2ProcessorSettings::UPCGExClipper2ProcessorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (const UEnum* EnumClass = StaticEnum<EPCGExClipper2EndpointType>())
	{
		const int32 NumEnums = EnumClass->NumEnums() - 1; // Skip _MAX
		for (int32 i = 0; i < NumEnums; ++i)
		{
			const EPCGExClipper2EndpointType Value = static_cast<EPCGExClipper2EndpointType>(EnumClass->GetValueByIndex(i));
			JointTypeValueMapping.Add(Value, i);
		}
	}
}

bool UPCGExClipper2ProcessorSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExClipper2::Labels::SourceOperandsLabel)
	{
		return WantsOperands();
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExClipper2ProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(MainDataMatching, PinProperties);

	if (WantsOperands())
	{
		PCGEX_PIN_POINTS(PCGExClipper2::Labels::SourceOperandsLabel, "Operands", Required)
		PCGExMatching::Helpers::DeclareMatchingRulesInputs(OperandsDataMatching, PinProperties, PCGExClipper2::Labels::SourceOperandsMatchRulesLabel);
	}
	else
	{
		PCGEX_PIN_POINTS(PCGExClipper2::Labels::SourceOperandsLabel, "Operands", Advanced)

		FPCGExMatchingDetails OperandsDataMatchingCopy = OperandsDataMatching;
		OperandsDataMatchingCopy.Mode = EPCGExMapMatchMode::Disabled;
		PCGExMatching::Helpers::DeclareMatchingRulesInputs(OperandsDataMatchingCopy, PinProperties, PCGExClipper2::Labels::SourceOperandsMatchRulesLabel);
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExClipper2ProcessorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (OpenPathsOutput == EPCGExClipper2OpenPathOutput::OutputPin)
	{
		PCGEX_PIN_POINTS(FName("Open Paths"), "Open paths", Normal)
	}
	return PinProperties;
}

bool UPCGExClipper2ProcessorSettings::WantsOperands() const
{
	return false;
}

FPCGExGeo2DProjectionDetails UPCGExClipper2ProcessorSettings::GetProjectionDetails() const
{
	return FPCGExGeo2DProjectionDetails();
}

bool UPCGExClipper2ProcessorSettings::SupportOpenMainPaths() const
{
	return !bSkipOpenPaths;
}

bool UPCGExClipper2ProcessorSettings::SupportOpenOperandPaths() const
{
	return SupportOpenMainPaths();
}

bool UPCGExClipper2ProcessorSettings::OperandsAsBounds() const
{
	return false;
}

// File-local helpers (named namespace matching the file -- Unity-build safe).
namespace PCGExClipper2Processor
{
	// Deterministic plurality pick from a source-index -> count map. Highest count wins; ties broken by smaller
	// source index (TMap order is not meaningful). Returns INDEX_NONE for an empty map.
	int32 PickModalSource(const TMap<int32, int32>& Counts)
	{
		int32 BestIdx = INDEX_NONE;
		int32 BestCount = 0;
		for (const TPair<int32, int32>& Pair : Counts)
		{
			if (Pair.Value > BestCount || (Pair.Value == BestCount && Pair.Key < BestIdx))
			{
				BestCount = Pair.Value;
				BestIdx = Pair.Key;
			}
		}
		return BestIdx;
	}
}

void FPCGExClipper2ProcessorContext::OutputPaths64(
	PCGExClipper2Lib::Paths64& InPaths,
	const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
	TArray<TSharedPtr<PCGExData::FPointIO>>& OutPaths,
	const bool bClosedPaths,
	const int32 CallSiteIndex,
	PCGExClipper2::ETransformRestoration TransformMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClipper2ProcessorContext::OutputPaths64)

	const UPCGExClipper2ProcessorSettings* Settings = GetInputSettings<UPCGExClipper2ProcessorSettings>();

	if (InPaths.empty())
	{
		return;
	}

	const double InvScale = 1.0 / static_cast<double>(Settings->Precision);

	// Internal path-state markers are authored on outputs by this node alone (SetClosedLoop below, hole tagging),
	// and must NOT ride the blender's @Data carry-over: the reduce writes entry 0, which shadows the default slot
	// SetClosedLoop writes to -- a closed source's IsClosed=true would clobber the explicit "false" on paths that
	// get cut open (e.g. RectClip as-lines). Name-based exclusion, so user attributes with these reserved names
	// are skipped too.
	const TSet<FName> NodeAuthoredAttributes = {PCGExPaths::Labels::ClosedLoopIdentifier.Name, PCGExPaths::Labels::HoleIdentifier.Name};

	// Bounds-guarded source resolution helpers, shared by every path below.
	auto GetProjectedZ = [this](const int32 SrcIdx, const uint32 PtIdx) -> double
	{
		if (SrcIdx < 0 || SrcIdx >= AllOpData->ProjectedZValues.Num())
		{
			return 0.0;
		}

		const TArray<double>& ZValues = AllOpData->ProjectedZValues[SrcIdx];
		if (static_cast<int32>(PtIdx) >= ZValues.Num())
		{
			return 0.0;
		}

		return ZValues[PtIdx];
	};

	auto GetProjection = [this](const int32 SrcIdx) -> const FPCGExGeo2DProjectionDetails*
	{
		if (SrcIdx < 0 || SrcIdx >= AllOpData->Projections.Num())
		{
			return nullptr;
		}
		return &AllOpData->Projections[SrcIdx];
	};

	// Fetch a source point's original transform; false when the encoded indices don't resolve.
	auto GetSourceTransform = [this](const uint32 PtIdx, const uint32 SrcIdx, FTransform& OutT) -> bool
	{
		const int32 ArrayIdx = static_cast<int32>(SrcIdx);
		if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num())
		{
			return false;
		}

		const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[ArrayIdx];
		if (static_cast<int32>(PtIdx) >= SrcFacade->Source->GetNum(PCGExData::EIOSide::In))
		{
			return false;
		}

		TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
		OutT = SrcTransforms[PtIdx];
		return true;
	};

	// Phase 1: filter + optional simplification (cheap, sequential). LocalIndex keeps IOIndex deterministic
	// regardless of how phase 2 is scheduled.
	struct FPathJob
	{
		PCGExClipper2Lib::Path64* Path = nullptr;
		int32 LocalIndex = 0;
	};

	TArray<FPathJob> Jobs;
	Jobs.Reserve(static_cast<int32>(InPaths.size()));

	{
		int32 LocalPathIndex = 0;
		for (PCGExClipper2Lib::Path64& Path : InPaths)
		{
			if (Path.size() < 2)
			{
				continue;
			}

			// Simplify if requested. Open results must be simplified as open paths, or their endpoints become
			// eligible for removal.
			if (Settings->bSimplifyPaths)
			{
				Path = PCGExClipper2Lib::SimplifyPath(Path, Settings->Precision * 0.5, bClosedPaths);
				if (Path.size() < 2)
				{
					continue;
				}
			}

			Jobs.Add({&Path, LocalPathIndex++});
		}
	}

	if (Jobs.IsEmpty())
	{
		return;
	}

	// Phase 2: build one output IO per path. Paths are independent, so they run in parallel: the IO collection
	// emplace is internally locked, IOIndex is explicit (collections sort on it before staging), and everything
	// else is path-local. The per-point loop inside only goes wide when there's a single path, so the two levels
	// never compete.
	TArray<TSharedPtr<PCGExData::FPointIO>> NewIOs;
	NewIOs.SetNum(Jobs.Num());

	const bool bSingleJob = Jobs.Num() == 1;

	ParallelFor(
		Jobs.Num(),
		[&](const int32 JobIndex)
	{
		PCGExClipper2Lib::Path64& Path = *Jobs[JobIndex].Path;

		// Determine if this is a hole (counter-clockwise winding)
		const bool bIsHole = bClosedPaths && !PCGExClipper2Lib::IsPositive(Path);

		// Collect relevant sources for this specific path (optimization: only initialize blenders for sources actually in the path)
		TSet<int32> RelevantSourceIndices;
		TMap<int32, int32> SourceCounts;

		for (const PCGExClipper2Lib::Point64& Pt : Path)
		{
			uint32 PointIdx, SourceIdx;
			PCGEx::H64(static_cast<uint64>(Pt.z), PointIdx, SourceIdx);

			if (PointIdx == PCGExClipper2::INTERSECTION_MARKER)
			{
				// Intersection point - add all 4 contributing sources
				if (const PCGExClipper2::FIntersectionBlendInfo* BlendInfo = Group->GetIntersectionBlendInfo(Pt.x, Pt.y))
				{
					RelevantSourceIndices.Add(BlendInfo->E1BotSourceIdx);
					RelevantSourceIndices.Add(BlendInfo->E1TopSourceIdx);
					RelevantSourceIndices.Add(BlendInfo->E2BotSourceIdx);
					RelevantSourceIndices.Add(BlendInfo->E2TopSourceIdx);
				}
			}
			else
			{
				// Regular point - add its source
				const int32 SrcIdx = static_cast<int32>(SourceIdx);
				if (SrcIdx < AllOpData->Facades.Num())
				{
					RelevantSourceIndices.Add(SrcIdx);
					SourceCounts.FindOrAdd(SrcIdx)++;
				}
			}
		}

		// Build per-path BlendSources and Allocations from only relevant sources
		TArray<TSharedRef<PCGExData::FFacade>> BlendSources;
		BlendSources.Reserve(RelevantSourceIndices.Num());

		EPCGPointNativeProperties Allocations = EPCGPointNativeProperties::None;

		for (const int32 SrcIdx : RelevantSourceIndices)
		{
			if (SrcIdx != INDEX_NONE && SrcIdx < AllOpData->Facades.Num())
			{
				const TSharedPtr<PCGExData::FFacade>& Facade = AllOpData->Facades[SrcIdx];
				Allocations |= Facade->GetAllocations();
				BlendSources.Add(Facade.ToSharedRef());
			}
		}

		// Dominant source as the output template; fall back to first relevant source when the path is all
		// intersection points (SourceCounts empty).
		int32 DominantSourceIdx = PCGExClipper2Processor::PickModalSource(SourceCounts);
		if (DominantSourceIdx == INDEX_NONE)
		{
			DominantSourceIdx = RelevantSourceIndices.Num() > 0 ? *RelevantSourceIndices.CreateConstIterator() : INDEX_NONE;
		}

		// Create new output point data from template
		TSharedPtr<PCGExData::FPointIO> NewPointIO;

		if (DominantSourceIdx != INDEX_NONE && DominantSourceIdx < AllOpData->Facades.Num())
		{
			const TSharedPtr<PCGExData::FFacade>& TemplateFacade = AllOpData->Facades[DominantSourceIdx];
			NewPointIO = MainPoints->Emplace_GetRef(TemplateFacade->Source, PCGExData::EIOInit::New);
		}

		if (!NewPointIO)
		{
			NewPointIO = MainPoints->Emplace_GetRef(PCGExData::EIOInit::New);
		}

		if (!NewPointIO)
		{
			PCGE_LOG_C(Error, GraphAndLog, this, FTEXT("Failed to create output data for a path."));
			return; // Skip this path only
		}

		// Deterministic IOIndex for stable output ordering:
		NewPointIO->IOIndex = Group->GroupIndex * 10000000 + CallSiteIndex * 100000 + Jobs[JobIndex].LocalIndex;

		const int32 NumPoints = static_cast<int32>(Path.size());
		UPCGBasePointData* OutPoints = NewPointIO->GetOut();

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, NumPoints, Allocations);

		const TSharedPtr<PCGExData::FFacade> OutputFacade = MakeShared<PCGExData::FFacade>(NewPointIO.ToSharedRef());

		const TSharedPtr<PCGExBlending::FUnionBlender> Blender = MakeShared<PCGExBlending::FUnionBlender>(&BlendingDetails, &CarryOverDetails, PCGExMath::GetDistances());
		Blender->AddSources(BlendSources, &NodeAuthoredAttributes, [](const TSharedRef<PCGExData::FFacade>& InFacade)
		{
			return InFacade->Idx;
		});

		// Valid metadata entry keys are only needed when attribute values get written -- blended/carried-over
		// attributes or the flag buffers below.
		if (Blender->HasAnyAttributeBlenders() || Settings->bFlagIntersections || Settings->bFlagJoints)
		{
			NewPointIO->GetOutKeys(true);
		}

		TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange(false);

		const TSharedPtr<PCGExData::FUnionMetadata> UnionMetadata = MakeShared<PCGExData::FUnionMetadata>();
		UnionMetadata->SetNum(NumPoints);

		if (!Blender->Init(this, OutputFacade, UnionMetadata, PCGExData::EProxyFlags::Direct)) // might want to switch to cache here?
		{
			PCGE_LOG_C(Error, GraphAndLog, this, FTEXT("Error while initializing data blending"));
			NewPointIO->Disable(); // Exclude the half-built IO from outputs, keep processing the other paths
			return;
		}

		// Interpolate rotation/scale for an intersection point from its 4 contributing source points.
		auto BlendFromInfo = [&GetSourceTransform](const PCGExClipper2::FIntersectionBlendInfo* BlendInfo, FTransform& OutTransform)
		{
			FTransform E1Bot, E1Top, E2Bot, E2Top;
			const bool bHasE1Bot = GetSourceTransform(BlendInfo->E1BotPointIdx, BlendInfo->E1BotSourceIdx, E1Bot);
			const bool bHasE1Top = GetSourceTransform(BlendInfo->E1TopPointIdx, BlendInfo->E1TopSourceIdx, E1Top);
			const bool bHasE2Bot = GetSourceTransform(BlendInfo->E2BotPointIdx, BlendInfo->E2BotSourceIdx, E2Bot);
			const bool bHasE2Top = GetSourceTransform(BlendInfo->E2TopPointIdx, BlendInfo->E2TopSourceIdx, E2Top);

			FTransform E1Interp = E1Bot;
			if (bHasE1Bot && bHasE1Top)
			{
				E1Interp.Blend(E1Bot, E1Top, BlendInfo->E1Alpha);
			}

			FTransform E2Interp = E2Bot;
			if (bHasE2Bot && bHasE2Top)
			{
				E2Interp.Blend(E2Bot, E2Top, BlendInfo->E2Alpha);
			}

			OutTransform.Blend(E1Interp, E2Interp, 0.5);
		};

		// Process each point in the path
		ParallelFor(
			NumPoints,
			[&](const int32 i)
			{
				const PCGExClipper2Lib::Point64& Pt = Path[i];
				FTransform& OutTransform = OutTransforms[i];

				// Decode source info from Z
				uint32 OriginalPointIdx, SourceIdx;
				PCGEx::H64(static_cast<uint64>(Pt.z), OriginalPointIdx, SourceIdx);

				const bool bIsIntersection = (OriginalPointIdx == PCGExClipper2::INTERSECTION_MARKER);

				if (bIsIntersection)
				{
					// This is an intersection point - get blend info and interpolate transform
					const PCGExClipper2::FIntersectionBlendInfo* BlendInfo = Group->GetIntersectionBlendInfo(Pt.x, Pt.y);

					if (BlendInfo)
					{
						// Rotation/scale from the 4 contributing source points, in both modes.
						BlendFromInfo(BlendInfo, OutTransform);

						if (TransformMode == PCGExClipper2::ETransformRestoration::Unproject)
						{
							// Interpolate projected Z along each edge, average, then unproject the clipper coords.
							const double Z1 = FMath::Lerp(
								GetProjectedZ(static_cast<int32>(BlendInfo->E1BotSourceIdx), BlendInfo->E1BotPointIdx),
								GetProjectedZ(static_cast<int32>(BlendInfo->E1TopSourceIdx), BlendInfo->E1TopPointIdx),
								BlendInfo->E1Alpha);
							const double Z2 = FMath::Lerp(
								GetProjectedZ(static_cast<int32>(BlendInfo->E2BotSourceIdx), BlendInfo->E2BotPointIdx),
								GetProjectedZ(static_cast<int32>(BlendInfo->E2TopSourceIdx), BlendInfo->E2TopPointIdx),
								BlendInfo->E2Alpha);

							// Get projection from first valid source
							const FPCGExGeo2DProjectionDetails* Projection = GetProjection(static_cast<int32>(BlendInfo->E1BotSourceIdx));
							if (!Projection)
							{
								Projection = GetProjection(static_cast<int32>(BlendInfo->E2BotSourceIdx));
							}

							FVector UnprojectedPos(
								static_cast<double>(Pt.x) * InvScale,
								static_cast<double>(Pt.y) * InvScale,
								(Z1 + Z2) * 0.5);

							if (Projection)
							{
								Projection->UnprojectInPlace(UnprojectedPos);
							}

							OutTransform.SetLocation(UnprojectedPos);
						}

						// Add all 4 vertices to union for metadata blending
						TSharedPtr<PCGExData::IUnionData> Union = UnionMetadata->NewEntryAt_Unsafe(i);

						auto AddToUnion = [&](uint32 PtIdx, uint32 SrcIdx)
						{
							const int32 ArrayIdx = static_cast<int32>(SrcIdx);
							if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num())
							{
								return;
							}

							const TSharedPtr<PCGExData::FFacade>& SourceFacade = AllOpData->Facades[ArrayIdx];
							const int32 NumPts = SourceFacade->Source->GetNum(PCGExData::EIOSide::In);

							if (static_cast<int32>(PtIdx) >= NumPts)
							{
								return;
							}

							Union->Add_Unsafe(static_cast<int32>(PtIdx), SourceFacade->Idx);
						};

						AddToUnion(BlendInfo->E1BotPointIdx, BlendInfo->E1BotSourceIdx);
						AddToUnion(BlendInfo->E1TopPointIdx, BlendInfo->E1TopSourceIdx);
						AddToUnion(BlendInfo->E2BotPointIdx, BlendInfo->E2BotSourceIdx);
						AddToUnion(BlendInfo->E2TopPointIdx, BlendInfo->E2TopSourceIdx);
					}
					else
					{
						// No blend info for this intersection point: it's pass-through clip geometry (e.g. rect
						// corners) or a map miss. Walk prev/next for the nearest source-backed neighbors -- they
						// provide rotation/scale, metadata and a Z estimate; the point's own clipper coords provide
						// the position.
						struct FNeighbor
						{
							FTransform Transform;
							int32 SrcIdx = INDEX_NONE;
							uint32 PtIdx = 0;
						};

						auto FindNeighbor = [&](const int32 Dir) -> FNeighbor
						{
							FNeighbor Neighbor;
							for (int32 Step = 1; Step < NumPoints; Step++)
							{
								const int32 Ni = (i + Dir * Step + NumPoints) % NumPoints;
								uint32 NPtIdx, NSrcIdx;
								PCGEx::H64(static_cast<uint64>(Path[Ni].z), NPtIdx, NSrcIdx);

								if (NPtIdx == PCGExClipper2::INTERSECTION_MARKER)
								{
									continue;
								}

								if (GetSourceTransform(NPtIdx, NSrcIdx, Neighbor.Transform))
								{
									Neighbor.SrcIdx = static_cast<int32>(NSrcIdx);
									Neighbor.PtIdx = NPtIdx;
								}
								break; // First non-intersection neighbor decides, resolvable or not
							}
							return Neighbor;
						};

						const FNeighbor Prev = FindNeighbor(-1);
						const FNeighbor Next = FindNeighbor(+1);

						// Rotation/scale from the neighbors.
						if (Prev.SrcIdx != INDEX_NONE && Next.SrcIdx != INDEX_NONE)
						{
							OutTransform.Blend(Prev.Transform, Next.Transform, 0.5);
						}
						else if (Prev.SrcIdx != INDEX_NONE)
						{
							OutTransform = Prev.Transform;
						}
						else if (Next.SrcIdx != INDEX_NONE)
						{
							OutTransform = Next.Transform;
						}

						// Position from the point's own clipper coords: average the neighbors' projected Z, then
						// unproject with the first available neighbor's projection. Applies in both transform modes
						// -- this geometry has no source point to restore from.
						double FallbackZ = 0;
						int32 ZContributors = 0;
						if (Prev.SrcIdx != INDEX_NONE)
						{
							FallbackZ += GetProjectedZ(Prev.SrcIdx, Prev.PtIdx);
							ZContributors++;
						}
						if (Next.SrcIdx != INDEX_NONE)
						{
							FallbackZ += GetProjectedZ(Next.SrcIdx, Next.PtIdx);
							ZContributors++;
						}
						if (ZContributors > 0)
						{
							FallbackZ /= ZContributors;
						}

						FVector UnprojectedPos(
							static_cast<double>(Pt.x) * InvScale,
							static_cast<double>(Pt.y) * InvScale,
							FallbackZ);

						if (const FPCGExGeo2DProjectionDetails* Projection = GetProjection(Prev.SrcIdx != INDEX_NONE ? Prev.SrcIdx : Next.SrcIdx))
						{
							Projection->UnprojectInPlace(UnprojectedPos);
						}
						OutTransform.SetLocation(UnprojectedPos);

						// Add both neighbors to union for metadata blending
						TSharedPtr<PCGExData::IUnionData> Union = UnionMetadata->NewEntryAt_Unsafe(i);
						if (Prev.SrcIdx != INDEX_NONE)
						{
							Union->Add_Unsafe(static_cast<int32>(Prev.PtIdx), AllOpData->Facades[Prev.SrcIdx]->Idx);
						}
						if (Next.SrcIdx != INDEX_NONE)
						{
							Union->Add_Unsafe(static_cast<int32>(Next.PtIdx), AllOpData->Facades[Next.SrcIdx]->Idx);
						}
					}
				}
				else
				{
					// Regular point: rotation/scale (and position, in FromSource mode) from the source point.
					const int32 SourceArrayIdx = static_cast<int32>(SourceIdx);

					FTransform SrcTransform;
					if (GetSourceTransform(OriginalPointIdx, SourceIdx, SrcTransform))
					{
						OutTransform = SrcTransform;
					}

					if (TransformMode == PCGExClipper2::ETransformRestoration::Unproject)
					{
						// Unproject mode: position from Clipper2 X/Y + stored projected Z
						FVector UnprojectedPos(
							static_cast<double>(Pt.x) * InvScale,
							static_cast<double>(Pt.y) * InvScale,
							GetProjectedZ(SourceArrayIdx, OriginalPointIdx));

						if (const FPCGExGeo2DProjectionDetails* Projection = GetProjection(SourceArrayIdx))
						{
							Projection->UnprojectInPlace(UnprojectedPos);
						}

						OutTransform.SetLocation(UnprojectedPos);
					}

					// Add to union for blending
					TSharedPtr<PCGExData::IUnionData> Union = UnionMetadata->NewEntryAt_Unsafe(i);

					if (SourceArrayIdx >= 0 && SourceArrayIdx < AllOpData->Facades.Num())
					{
						const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[SourceArrayIdx];
						const int32 SrcNumPts = SrcFacade->Source->GetNum(PCGExData::EIOSide::In);
						const int32 Pt1 = FMath::Clamp(static_cast<int32>(OriginalPointIdx), 0, SrcNumPts - 1);

						Union->Add_Unsafe(Pt1, SrcFacade->Idx);
					}
				}
			}, !bSingleJob || NumPoints < 128);

		{
			// Perform blending
			TRACE_CPUPROFILER_EVENT_SCOPE(OutputPaths64::Blending)

			TArray<PCGExData::FWeightedPoint> WeightedPoints;
			TArray<PCGEx::FOpStats> Trackers;
			Blender->InitTrackers(Trackers);

			for (int32 i = 0; i < NumPoints; i++)
			{
				WeightedPoints.Reset();
				Blender->MergeSingle(i, WeightedPoints, Trackers);
			}

			// -- Flag writing (after blending, before WriteFastest) --
			TSharedPtr<PCGExData::TBuffer<bool>> IntersectionWriter;
			TSharedPtr<PCGExData::TBuffer<int32>> JointWriter;

			if (Settings->bFlagIntersections)
			{
				IntersectionWriter = OutputFacade->GetWritable<bool>(Settings->IntersectionFlagName, false, false, PCGExData::EBufferInit::New);
			}
			if (Settings->bFlagJoints)
			{
				JointWriter = OutputFacade->GetWritable<int32>(Settings->JointFlagName, Settings->JointTypeValueMapping[EPCGExClipper2EndpointType::None], false, PCGExData::EBufferInit::New);
			}

			// Per-point source endpoint classification for joint arc boundary detection
			// 0 = not from a source endpoint, 1 = from source start (idx 0), 2 = from source end (idx N-1)
			TArray<int8> SourceEndpointClass;
			if (JointWriter)
			{
				SourceEndpointClass.SetNumZeroed(NumPoints);
			}

			if (IntersectionWriter || JointWriter)
			{
				// Pass 1: write intersection flags, build joint classification
				for (int32 i = 0; i < NumPoints; i++)
				{
					const PCGExClipper2Lib::Point64& FlagPt = Path[i];
					uint32 FlagPtIdx, FlagSrcIdx;
					PCGEx::H64(static_cast<uint64>(FlagPt.z), FlagPtIdx, FlagSrcIdx);

					const bool bIsIntersectionPt = (FlagPtIdx == PCGExClipper2::INTERSECTION_MARKER);

					if (IntersectionWriter)
					{
						IntersectionWriter->SetValue(i, bIsIntersectionPt);
					}
					if (JointWriter && !bIsIntersectionPt)
					{
						const int32 SrcArrayIdx = static_cast<int32>(FlagSrcIdx);
						if (SrcArrayIdx >= 0 && SrcArrayIdx < AllOpData->Facades.Num() && !AllOpData->IsClosedLoop[SrcArrayIdx])
						{
							const int32 SrcNumPts = AllOpData->Facades[SrcArrayIdx]->Source->GetNum(PCGExData::EIOSide::In);
							if (static_cast<int32>(FlagPtIdx) == 0)
							{
								SourceEndpointClass[i] = 1;
							}
							else if (static_cast<int32>(FlagPtIdx) == SrcNumPts - 1)
							{
								SourceEndpointClass[i] = 2;
							}
						}
					}
				}

				// Pass 2: detect joint arc boundaries -- only flag the first/last point of each arc run
				if (JointWriter)
				{
					for (int32 i = 0; i < NumPoints; i++)
					{
						const int8 Current = SourceEndpointClass[i];
						if (Current == 0)
						{
							continue;
						}

						const int8 Prev = (i > 0) ? SourceEndpointClass[i - 1] : (bClosedPaths ? SourceEndpointClass[NumPoints - 1] : static_cast<int8>(0));
						const int8 Next = (i < NumPoints - 1) ? SourceEndpointClass[i + 1] : (bClosedPaths ? SourceEndpointClass[0] : static_cast<int8>(0));

						const bool bIsArcStart = (Prev != Current);
						const bool bIsArcEnd = (Next != Current);

						if (bIsArcStart)
						{
							JointWriter->SetValue(i, Settings->JointTypeValueMapping[EPCGExClipper2EndpointType::Start]);
						}
						else if (bIsArcEnd)
						{
							JointWriter->SetValue(i, Settings->JointTypeValueMapping[EPCGExClipper2EndpointType::End]);
						}
					}
				}
			}

			OutputFacade->WriteFastest(GetTaskManager());
		}

		// Tag as hole if applicable
		if (bIsHole && Settings->bTagHoles)
		{
			NewPointIO->Tags->AddRaw(Settings->HoleTag);
		}

		PCGExPaths::Helpers::SetClosedLoop(NewPointIO, bClosedPaths);

		if (!bClosedPaths && Settings->OpenPathsOutput == EPCGExClipper2OpenPathOutput::OutputPin)
		{
			NewPointIO->OutputPin = FName("Open Paths");
		}

		// Forward tags from the sources that contribute to THIS path only.
		for (const int32 SrcIdx : RelevantSourceIndices)
		{
			if (SrcIdx >= 0 && SrcIdx < AllOpData->Facades.Num())
			{
				NewPointIO->Tags->Append(AllOpData->Facades[SrcIdx]->Source->Tags.ToSharedRef());
			}
		}

		CarryOverDetails.Prune(NewPointIO->Tags.Get());

		NewIOs[JobIndex] = NewPointIO;
	}, bSingleJob);

	// Deterministic OutPaths order regardless of the parallel scheduling above.
	OutPaths.Reserve(OutPaths.Num() + NewIOs.Num());
	for (const TSharedPtr<PCGExData::FPointIO>& IO : NewIOs)
	{
		if (IO)
		{
			OutPaths.Add(IO);
		}
	}
}

void FPCGExClipper2ProcessorContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	// Base implementation does nothing - derived classes override this
}


bool FPCGExClipper2ProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Processor)

	Context->CarryOverDetails = Settings->CarryOverDetails;
	Context->CarryOverDetails.Init();

	// Setup default blending details
	Context->BlendingDetails = Settings->BlendingDetails;

	// Initialize AllOpData early
	Context->AllOpData = MakeShared<PCGExClipper2::FOpData>(0);

	// Initialize projection
	Context->ProjectionDetails = Settings->GetProjectionDetails();

	// Build main data
	TArray<int32> MainIndices;
	int32 NumInputs = BuildDataFromCollection(Context, Settings, Context->MainPoints, Settings->SupportOpenMainPaths(), MainIndices);

	if (!NumInputs)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("No valid paths found in main input."));
		return false;
	}

	// Build operand data if needed
	TArray<int32> OperandIndices;
	if (Settings->WantsOperands())
	{
		Context->OperandsCollection = MakeShared<PCGExData::FPointIOCollection>(InContext, PCGExClipper2::Labels::SourceOperandsLabel, PCGExData::EIOInit::NoInit, false);

		if (Context->OperandsCollection->IsEmpty())
		{
			PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Operands input is required for this operation mode."));
			return false;
		}

		NumInputs = BuildDataFromCollection(Context, Settings, Context->OperandsCollection, Settings->SupportOpenOperandPaths(), OperandIndices);

		if (!NumInputs)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("No valid operands found in operands input."));
			return false;
		}
	}

	// Build processing groups
	BuildProcessingGroups(Context, Settings, MainIndices, OperandIndices);

	if (Context->ProcessingGroups.IsEmpty())
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("No valid processing groups could be formed."));
		return false;
	}

	return true;
}

bool FPCGExClipper2ProcessorElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClipper2ProcessorElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Processor)

	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_Processing);
		PCGEX_ASYNC_GROUP_CHKD_RET(Context->GetTaskManager(), WorkTasks, true)

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();

		for (int32 i = 0; i < Context->ProcessingGroups.Num(); i++)
		{
			WorkTasks->AddSimpleCallback([Settings, WeakHandle, Index = i]
			{
				FPCGContext::FSharedContext<FPCGExClipper2ProcessorContext> SharedContext(WeakHandle);
				if (!SharedContext.Get())
				{
					return;
				}

				const TSharedPtr<PCGExClipper2::FProcessingGroup> Group = SharedContext.Get()->ProcessingGroups[Index];
				Group->PreProcess(Settings);
				SharedContext.Get()->Process(Group);
			});
		}

		WorkTasks->StartSimpleCallbacks();
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_Processing)
	{
		OutputWork(Context, Settings);
		Context->Done();
	}

	return Context->TryComplete();
}

void FPCGExClipper2ProcessorElement::OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(Clipper2Processor)
	PCGEX_OUTPUT_VALID_PATHS(MainPoints)
}

int32 FPCGExClipper2ProcessorElement::BuildDataFromCollection(
	FPCGExClipper2ProcessorContext* Context,
	const UPCGExClipper2ProcessorSettings* Settings,
	const TSharedPtr<PCGExData::FPointIOCollection>& Collection,
	const bool bSupportOpenPaths, TArray<int32>& OutIndices) const
{
	if (!Collection)
	{
		return 0;
	}

	const int32 NumInputs = Collection->Num();
	if (NumInputs == 0)
	{
		return 0;
	}

	struct FBuildResult
	{
		bool bValid = false;
		PCGExClipper2Lib::Path64 Path64;
		TArray<double> ProjectedZValues;
		TSharedPtr<PCGExData::FFacade> Facade;
		bool bIsClosedLoop = false;
		FPCGExGeo2DProjectionDetails Projection;
	};

	TArray<FBuildResult> Results;
	Results.SetNum(NumInputs);

	Context->AllOpData->AddReserve(NumInputs);

	// Phase 1: Build paths async (without final index assignment)
	{
		PCGExAsyncHelpers::FAsyncExecutionScope BuildScope(NumInputs);

		for (int32 i = 0; i < NumInputs; ++i)
		{
			BuildScope.Execute([&, IO = (*Collection)[i], i]()
			{
				FBuildResult& Result = Results[i];


				// Check if closed (required for boolean ops)
				const bool bIsClosed = PCGExPaths::Helpers::GetClosedLoop(IO->GetIn());
				if (!bIsClosed && !bSupportOpenPaths)
				{
					return;
				}

				// Skip paths with insufficient points
				if (IO->GetNum() < 2 && !Settings->OperandsAsBounds())
				{
					PCGEX_LOG_INVALID_INPUT(Context, FTEXT("Some inputs have less than 2 points and won't be processed."))
					return;
				}

				TSharedPtr<PCGExData::FFacade> Facade = MakeShared<PCGExData::FFacade>(IO.ToSharedRef());

				// Mark Facade->Idx as unassigned for now (will be set during collection)
				Facade->Idx = -1;

				// Initialize projection for this path
				FPCGExGeo2DProjectionDetails LocalProjection = Context->ProjectionDetails;
				if (!LocalProjection.Init(Facade))
				{
					return;
				}

				const int32 Scale = Settings->Precision;

				TConstPCGValueRange<FTransform> InTransforms = IO->GetIn()->GetConstTransformValueRange();

				// Build path - Z values will be updated during collection phase
				// Also store projected Z for later unprojection
				const int32 NumPoints = InTransforms.Num();
				Result.Path64.reserve(NumPoints);
				Result.ProjectedZValues.SetNum(NumPoints);

				for (int32 j = 0; j < NumPoints; j++)
				{
					FVector ProjectedLocation = LocalProjection.Project(InTransforms[j].GetLocation());

					// Store projected Z for unprojection later
					Result.ProjectedZValues[j] = ProjectedLocation.Z;

					// Round (not truncate) into clipper units: truncation is asymmetric around zero and doubles
					// the worst-case quantization error.
					Result.Path64.emplace_back(
						FMath::RoundToInt64(ProjectedLocation.X * Scale),
						FMath::RoundToInt64(ProjectedLocation.Y * Scale),
						static_cast<int64_t>(j) // Temporary: just store point index, will encode with source idx later
						);
				}

				Result.bValid = true;
				Result.Facade = Facade;
				Result.Projection = LocalProjection;
				Result.bIsClosedLoop = bIsClosed;
			});
		}
	}

	int32 TotalDataNum = 0;
	OutIndices.Reserve(Results.Num());

	// Phase 2: Collect results sequentially and assign final indices
	for (FBuildResult& Result : Results)
	{
		if (!Result.bValid)
		{
			continue;
		}

		// ArrayIndex is the position in AllOpData arrays
		const int32 ArrayIndex = Context->AllOpData->Facades.Num();
		OutIndices.Add(ArrayIndex);

		// KEY: Facade->Idx now equals ArrayIndex
		// This eliminates the need for FindSourceIndex lookups
		Result.Facade->Idx = ArrayIndex;

		// Update Z values in path to encode (PointIndex, ArrayIndex)
		for (auto& Pt : Result.Path64)
		{
			const int32 PointIndex = static_cast<int32>(Pt.z);
			Pt.z = static_cast<int64_t>(PCGEx::H64(PointIndex, ArrayIndex));
		}

		Context->AllOpData->Facades.Add(Result.Facade);
		Context->AllOpData->Paths.Add(MoveTemp(Result.Path64));
		Context->AllOpData->Projections.Add(MoveTemp(Result.Projection));
		Context->AllOpData->IsClosedLoop.Add(Result.bIsClosedLoop);
		Context->AllOpData->ProjectedZValues.Add(MoveTemp(Result.ProjectedZValues));

		TotalDataNum++;
	}

	return TotalDataNum;
}

// File-local helpers for EPCGExGroupingPolicy::Auto. Named namespace matching the file (Unity-build safe).
namespace PCGExClipper2Processor
{
	// ZCallback for the grouping union: tag newly-created intersection verts so the source scan skips them.
	// Pass-through verts keep their original H64(PointIdx, SourceIdx) z -- that maps a contour back to its source.
	void MarkIntersectionZ(
		const PCGExClipper2Lib::Point64& /*E1Bot*/, const PCGExClipper2Lib::Point64& /*E1Top*/,
		const PCGExClipper2Lib::Point64& /*E2Bot*/, const PCGExClipper2Lib::Point64& /*E2Top*/,
		PCGExClipper2Lib::Point64& Pt)
	{
		Pt.z = static_cast<int64_t>(PCGEx::H64(PCGExClipper2::INTERSECTION_MARKER, PCGExClipper2::INTERSECTION_MARKER));
	}

	// Most frequent source index among a contour's verts, skipping union-created intersection verts. Returns
	// INDEX_NONE if none are source-backed. Clean non-touching nested rings map each contour to one source ring.
	int32 ModalSourceIndex(const PCGExClipper2Lib::Path64& Contour)
	{
		TMap<int32, int32> Counts;
		for (const PCGExClipper2Lib::Point64& Pt : Contour)
		{
			uint32 PointIdx, SourceIdx;
			PCGEx::H64(static_cast<uint64>(Pt.z), PointIdx, SourceIdx);
			if (PointIdx == PCGExClipper2::INTERSECTION_MARKER)
			{
				continue;
			}
			Counts.FindOrAdd(static_cast<int32>(SourceIdx))++;
		}

		return PickModalSource(Counts);
	}

	// One PolyTree outer node + its direct holes -> one partition (outer source first); islands (solids nested
	// inside a hole) recurse as their own footprints.
	void CollectFootprint(const PCGExClipper2Lib::PolyPath64* OuterNode, TArray<TArray<int32>>& OutPartitions, TSet<int32>& OutClaimed)
	{
		TArray<int32> Partition;

		// Claim sources GLOBALLY (OutClaimed): one source ring maps to exactly one footprint, so if the union
		// split it across contours (self-intersecting / touching input) first claim wins -- else the ring would
		// triangulate into two groups, producing duplicate/overlapping output.
		const int32 OuterSrc = ModalSourceIndex(OuterNode->Polygon());
		if (OuterSrc != INDEX_NONE && !OutClaimed.Contains(OuterSrc))
		{
			Partition.Add(OuterSrc);
			OutClaimed.Add(OuterSrc);
		}

		for (size_t h = 0; h < OuterNode->Count(); h++)
		{
			const PCGExClipper2Lib::PolyPath64* Hole = OuterNode->Child(h);

			const int32 HoleSrc = ModalSourceIndex(Hole->Polygon());
			if (HoleSrc != INDEX_NONE && !OutClaimed.Contains(HoleSrc))
			{
				Partition.Add(HoleSrc);
				OutClaimed.Add(HoleSrc);
			}

			// Islands inside this hole are new, solid footprints.
			for (size_t g = 0; g < Hole->Count(); g++)
			{
				CollectFootprint(Hole->Child(g), OutPartitions, OutClaimed);
			}
		}

		if (!Partition.IsEmpty())
		{
			OutPartitions.Add(MoveTemp(Partition));
		}
	}

	// Partition closed main paths by spatial nesting (union -> PolyTree): each outer + rings it contains is one
	// footprint, islands restart one. Open/dropped paths become singletons so nothing is lost (failed union
	// degrades to Split). Appends one block sorted by representative index, so it is safe to call once per pre-group.
	void BuildNestedPartitions(const TSharedPtr<PCGExClipper2::FOpData>& AllOpData, const TArray<int32>& MainIndices, TArray<TArray<int32>>& OutPartitions)
	{
		PCGExClipper2Lib::Paths64 ClosedSubjects;
		ClosedSubjects.reserve(MainIndices.Num());
		TArray<int32> OpenSingletons;

		for (const int32 Idx : MainIndices)
		{
			if (AllOpData->IsClosedLoop.IsValidIndex(Idx) && AllOpData->IsClosedLoop[Idx])
			{
				ClosedSubjects.push_back(AllOpData->Paths[Idx]); // copy -- AllOpData->Paths is reused by Prepare
			}
			else
			{
				OpenSingletons.Add(Idx); // open paths can't nest
			}
		}

		// Build this call's partitions in a local array so we sort (and own the ordering of) only the footprints
		// produced from THIS index set, then append them as one contiguous block. This keeps the helper safe to
		// call once per pre-group (matching + Auto) without re-sorting partitions contributed by earlier calls.
		TArray<TArray<int32>> Partitions;
		TSet<int32> Claimed;

		if (!ClosedSubjects.empty())
		{
			PCGExClipper2Lib::Clipper64 Clipper;
			Clipper.SetZCallback(&MarkIntersectionZ);
			Clipper.AddSubject(ClosedSubjects);

			// EvenOdd exposes the full containment hierarchy as nested contours regardless of input winding --
			// the nesting we want. Per-group triangulation still honors the node's own FillRule later.
			PCGExClipper2Lib::PolyTree64 Tree;
			Clipper.Execute(PCGExClipper2Lib::ClipType::Union, PCGExClipper2Lib::FillRule::EvenOdd, Tree);

			for (size_t i = 0; i < Tree.Count(); i++)
			{
				CollectFootprint(Tree.Child(i), Partitions, Claimed);
			}
		}

		// Any closed path the union dropped (degenerate / fully coincident / union failure) -> its own group.
		for (const int32 Idx : MainIndices)
		{
			if (AllOpData->IsClosedLoop.IsValidIndex(Idx) && AllOpData->IsClosedLoop[Idx] && !Claimed.Contains(Idx))
			{
				Partitions.Add({Idx});
			}
		}

		// Open paths as singletons.
		for (const int32 Idx : OpenSingletons)
		{
			Partitions.Add({Idx});
		}

		// Deterministic ordering within this call's block, by each partition's representative (outer / first) index.
		Partitions.Sort([](const TArray<int32>& A, const TArray<int32>& B)
		{
			return A[0] < B[0];
		});

		OutPartitions.Append(MoveTemp(Partitions));
	}
}

void FPCGExClipper2ProcessorElement::BuildProcessingGroups(
	FPCGExClipper2ProcessorContext* Context,
	const UPCGExClipper2ProcessorSettings* Settings,
	const TArray<int32>& MainIndices,
	const TArray<int32>& OperandIndices) const
{
	const TArray<TSharedPtr<PCGExData::FFacade>>& AllFacades = Context->AllOpData->Facades;

	// Collect main facades for matching
	TArray<TSharedPtr<PCGExData::FFacade>> MainFacades;
	MainFacades.Reserve(MainIndices.Num());
	for (const int32 Idx : MainIndices)
	{
		if (Idx < AllFacades.Num())
		{
			MainFacades.Add(AllFacades[Idx]);
		}
	}

	// Main partitioning runs in two passes so data matching and the grouping policy COMPOSE instead of being
	// mutually exclusive:
	//   1. Matching (optional) splits the main inputs into pre-groups.
	//   2. The grouping policy is applied WITHIN each pre-group.
	// With matching disabled there is a single pre-group holding every main input, so the policy behaves exactly
	// as before. With the policy at its default (Consolidate) each pre-group also stays whole, so "matching ->
	// one group per match" is unchanged too; only Auto/Split now refine inside a matched pre-group.

	// Pass 1 -- pre-groups, in AllOpData index space.
	TArray<TArray<int32>> PreGroups;

	bool bDoMainMatching = false;
	// Matching requires bExposeGroupingPolicy (the control is hidden otherwise -> no matching).
	if (Settings->bExposeGroupingPolicy && Settings->MainDataMatching.IsEnabled() && Settings->MainDataMatching.Mode != EPCGExMapMatchMode::Disabled)
	{
		auto Matcher = MakeShared<PCGExMatching::FDataMatcher>();
		Matcher->SetDetails(&Settings->MainDataMatching);

		bDoMainMatching = Matcher->Init(Context, MainFacades, true);

		if (bDoMainMatching)
		{
			PCGExMatching::Helpers::GetMatchingSourcePartitions(Matcher, MainFacades, PreGroups, true);

			// GetMatchingSourcePartitions works in MainFacades index space; convert to AllOpData indices
			// (Facade->Idx == ArrayIndex) so the policy pass + group building can index AllOpData directly.
			for (TArray<int32>& PreGroup : PreGroups)
			{
				for (int32& Idx : PreGroup)
				{
					if (Idx < MainFacades.Num())
					{
						Idx = MainFacades[Idx]->Idx;
					}
				}
			}
		}
	}

	if (!bDoMainMatching)
	{
		// No matching -> a single pre-group containing every main input.
		PreGroups.Emplace(MainIndices);
	}

	// Pass 2 -- apply the grouping policy within each pre-group (one policy per node, shared by every pre-group).
	const EPCGExGroupingPolicy GroupingPolicy = Settings->GetEffectiveGroupingPolicy();

	TArray<TArray<int32>> MainPartitions;
	MainPartitions.Reserve(MainIndices.Num());

	for (TArray<int32>& PreGroup : PreGroups)
	{
		if (PreGroup.IsEmpty())
		{
			continue;
		}

		switch (GroupingPolicy)
		{
		case EPCGExGroupingPolicy::Split:
			// Each input in the pre-group becomes its own group.
			for (const int32 Index : PreGroup)
			{
				MainPartitions.Add({Index});
			}
			break;
		case EPCGExGroupingPolicy::Consolidate:
			// The whole pre-group is one group.
			MainPartitions.Add(MoveTemp(PreGroup));
			break;
		case EPCGExGroupingPolicy::Auto:
			// Partition the pre-group by spatial nesting so nested rings become holes; unrelated footprints stay separate.
			PCGExClipper2Processor::BuildNestedPartitions(Context->AllOpData, PreGroup, MainPartitions);
			break;
		}
	}

	// Handle operand matching if we have operands
	TArray<TArray<int32>> OperandPartitions;

	if (!OperandIndices.IsEmpty())
	{
		TArray<TSharedPtr<PCGExData::FFacade>> OperandFacades;
		OperandFacades.Reserve(OperandIndices.Num());
		for (const int32 Idx : OperandIndices)
		{
			if (Idx < AllFacades.Num())
			{
				OperandFacades.Add(AllFacades[Idx]);
			}
		}

		bool bDoOperandMatching = false;
		if (Settings->OperandsDataMatching.IsEnabled() && Settings->OperandsDataMatching.Mode != EPCGExMapMatchMode::Disabled)
		{
			auto Matcher = MakeShared<PCGExMatching::FDataMatcher>();
			Matcher->SetDetails(&Settings->OperandsDataMatching);

			bDoOperandMatching = Matcher->Init(Context, OperandFacades, true, PCGExClipper2::Labels::SourceOperandsMatchRulesLabel);

			if (bDoOperandMatching)
			{
				PCGExMatching::FScope Scope(OperandFacades.Num(), true);
				OperandPartitions.Reserve(MainPartitions.Num());

				for (int i = 0; i < MainPartitions.Num(); i++)
				{
					const TArray<int32>& MainPartition = MainPartitions[i];
					TArray<int32>& Matches = OperandPartitions.Emplace_GetRef();

					for (const int32 MainIndex : MainPartition)
					{
						if (MainIndex < AllFacades.Num())
						{
							Matcher->GetMatchingSourcesIndices(AllFacades[MainIndex]->Source->GetTaggedData(), Scope, Matches);
						}
					}

					// Convert to AllOpData indices (Facade->Idx == ArrayIndex)
					for (int32& Idx : Matches)
					{
						if (Idx < OperandFacades.Num())
						{
							Idx = OperandFacades[Idx]->Idx;
						}
					}

					if (Matches.IsEmpty())
					{
						// Remove this partition - no matching operands
						OperandPartitions.Pop();
						MainPartitions.RemoveAt(i);
						i--;
					}
				}
			}
		}

		if (!bDoOperandMatching)
		{
			// All operands match all main groups
			OperandPartitions.Reserve(MainPartitions.Num());
			for (int i = 0; i < MainPartitions.Num(); i++)
			{
				OperandPartitions.Add(OperandIndices);
			}
		}
	}

	// When every group consumes the identical operand set, cache the operand paths ONCE and share them --
	// otherwise each group's Prepare() would deep-copy the full operand geometry (O(groups x operands)).
	// PreProcess copies-on-write before its debug pre-union, so sharing is safe.
	TSharedPtr<PCGExClipper2Lib::Paths64> SharedOperandPaths;
	TSharedPtr<PCGExClipper2Lib::Paths64> SharedOpenOperandPaths;

	if (!OperandIndices.IsEmpty() && !OperandPartitions.IsEmpty() && OperandPartitions[0] == OperandIndices)
	{
		SharedOperandPaths = MakeShared<PCGExClipper2Lib::Paths64>();
		SharedOpenOperandPaths = MakeShared<PCGExClipper2Lib::Paths64>();
		Context->AllOpData->CollectPaths(OperandIndices, *SharedOperandPaths, *SharedOpenOperandPaths);
	}

	// Build processing groups
	Context->ProcessingGroups.Reserve(MainPartitions.Num());

	for (int32 i = 0; i < MainPartitions.Num(); i++)
	{
		TSharedPtr<PCGExClipper2::FProcessingGroup> Group = MakeShared<PCGExClipper2::FProcessingGroup>();
		Group->SubjectIndices = MoveTemp(MainPartitions[i]);

		if (i < OperandPartitions.Num())
		{
			Group->OperandIndices = MoveTemp(OperandPartitions[i]);
		}

		if (SharedOperandPaths && Group->OperandIndices == OperandIndices)
		{
			Group->OperandPaths = SharedOperandPaths;
			Group->OpenOperandPaths = SharedOpenOperandPaths;
		}

		Group->Prepare(Context->AllOpData);
		Context->CarryOverDetails.Prune(Group->GroupTags.Get());

		if (Group->IsValid())
		{
			Group->GroupIndex = Context->ProcessingGroups.Num();
			Context->ProcessingGroups.Add(Group);
		}
	}
}

#undef LOCTEXT_NAMESPACE
