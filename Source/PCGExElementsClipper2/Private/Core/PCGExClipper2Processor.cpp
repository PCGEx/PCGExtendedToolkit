// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClipper2Processor.h"

#include "Core/PCGExClipper2Common.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExAsyncHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Clipper2Lib/clipper.h"
#include "Core/PCGExUnionData.h"
#include "Math/PCGExMathDistances.h"
#include "Async/ParallelFor.h"

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

#pragma endregion

#pragma region FProcessingGroup

	void FProcessingGroup::Prepare(const TSharedPtr<FOpData>& AllOpData)
	{
		GroupTags = MakeShared<PCGExData::FTags>();

		// Cache subject paths
		SubjectPaths.reserve(SubjectIndices.Num());
		OpenSubjectPaths.reserve(SubjectIndices.Num());
		for (const int32 Idx : SubjectIndices)
		{
			if (AllOpData->IsClosedLoop[Idx]) { SubjectPaths.push_back(AllOpData->Paths[Idx]); }
			else { OpenSubjectPaths.push_back(AllOpData->Paths[Idx]); }

			GroupTags->Append(AllOpData->Facades[Idx]->Source->Tags.ToSharedRef());
		}

		// Cache operand paths
		OperandPaths.reserve(OperandIndices.Num());
		OpenOperandPaths.reserve(OperandIndices.Num());
		for (const int32 Idx : OperandIndices)
		{
			if (Idx < AllOpData->Paths.Num())
			{
				if (AllOpData->IsClosedLoop[Idx]) { OperandPaths.push_back(AllOpData->Paths[Idx]); }
				else { OpenOperandPaths.push_back(AllOpData->Paths[Idx]); }
			}

			GroupTags->Append(AllOpData->Facades[Idx]->Source->Tags.ToSharedRef());
		}

		// Build combined source indices
		AllSourceIndices.Reserve(SubjectIndices.Num() + OperandIndices.Num());
		AllSourceIndices.Append(SubjectIndices);
		AllSourceIndices.Append(OperandIndices);
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

		if (InSettings->bUnionOperandsBeforeOperation && OperandPaths.size() > 1)
		{
			PCGExClipper2Lib::Paths64 Union;
			PCGExClipper2Lib::Clipper64 Clipper;
			Clipper.SetZCallback(CreateZCallback());
			Clipper.AddSubject(OperandPaths);
			Clipper.AddOpenSubject(OpenOperandPaths);
			Clipper.Execute(PCGExClipper2Lib::ClipType::Union, PCGExClipper2Lib::FillRule::NonZero, Union);
			OperandPaths = Union;
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
		TWeakPtr<FProcessingGroup> WeakSelf = AsWeak();

		return [WeakSelf](
			const PCGExClipper2Lib::Point64& e1bot, const PCGExClipper2Lib::Point64& e1top,
			const PCGExClipper2Lib::Point64& e2bot, const PCGExClipper2Lib::Point64& e2top,
			PCGExClipper2Lib::Point64& pt)
		{
			TSharedPtr<FProcessingGroup> Group = WeakSelf.Pin();
			if (!Group) { return; }

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
				if (Len < SMALL_NUMBER) { return 0.5; }

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
	if (InPin->Properties.Label == PCGExClipper2::Labels::SourceOperandsLabel) { return WantsOperands(); }
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
	if (OpenPathsOutput == EPCGExClipper2OpenPathOutput::OutputPin) { PCGEX_PIN_POINTS(FName("Open Paths"), "Open paths", Normal) }
	return PinProperties;
}

bool UPCGExClipper2ProcessorSettings::WantsDataMatching() const
{
	return MainDataMatching.IsEnabled();
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

/*
class FOutputPaths64 final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FOutputDelaunaySites2D)

		FOutputPaths64(const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<FProcessor>& InProcessor)
			: FTask(), PointIO(InPointIO), Processor(InProcessor)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<FProcessor> Processor;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDelaunaySites2D::ExecuteTask);

			
		}
	};
	*/

TSharedPtr<PCGExData::FTags> FPCGExClipper2ProcessorContext::GatherTags(const TArray<int8>& VisitedSources)
{
	auto OutTags = MakeShared<PCGExData::FTags>();

	for (int i = 0; i < VisitedSources.Num(); i++)
	{
		if (!VisitedSources[i]) { continue; }
		const TSharedPtr<PCGExData::FFacade> Facade = AllOpData->Facades[i];
		OutTags->Append(Facade->Source->Tags.ToSharedRef());
	}

	return OutTags;
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

	if (InPaths.empty()) { return; }

	const double InvScale = 1.0 / static_cast<double>(Settings->Precision);

	TArray<int8> VisitedSources;
	VisitedSources.Init(0, AllOpData->Num());

	// Process each output path
	OutPaths.Reserve(InPaths.size());

	int32 LocalPathIndex = 0;
	for (PCGExClipper2Lib::Path64& Path : InPaths)
	{
		if (Path.size() < 2) { continue; }

		// Simplify if requested
		if (Settings->bSimplifyPaths)
		{
			Path = PCGExClipper2Lib::SimplifyPath(Path, Settings->Precision * 0.5, true);
			if (Path.size() < 2) { continue; }
		}

		// Determine if this is a hole (counter-clockwise winding)
		const bool bIsHole = !PCGExClipper2Lib::IsPositive(Path);

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

		// Find dominant source (most points from same source)
		int32 DominantSourceIdx = RelevantSourceIndices.Num() > 0 ? *RelevantSourceIndices.CreateConstIterator() : INDEX_NONE;
		int32 MaxCount = 0;
		for (const auto& Pair : SourceCounts)
		{
			if (Pair.Value > MaxCount)
			{
				MaxCount = Pair.Value;
				DominantSourceIdx = Pair.Key;
			}
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

		if (!NewPointIO) { return; }

		// Deterministic IOIndex for stable output ordering:
		NewPointIO->IOIndex = Group->GroupIndex * 10000000 + CallSiteIndex * 100000 + LocalPathIndex;

		const int32 NumPoints = static_cast<int32>(Path.size());
		UPCGBasePointData* OutPoints = NewPointIO->GetOut();


		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, NumPoints, Allocations);
		NewPointIO->GetOutKeys(true); // Force valid entry keys for metadata -- TODO : Only do this if there are attributes to carry over

		TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange(false);

		TSharedPtr<PCGExData::FFacade> OutputFacade;
		TSharedPtr<PCGExBlending::FUnionBlender> Blender;
		TSharedPtr<PCGExData::FUnionMetadata> UnionMetadata;

		OutputFacade = MakeShared<PCGExData::FFacade>(NewPointIO.ToSharedRef());

		Blender = MakeShared<PCGExBlending::FUnionBlender>(&BlendingDetails, &CarryOverDetails, PCGExMath::GetDistances());
		Blender->AddSources(BlendSources, nullptr, [](const TSharedRef<PCGExData::FFacade>& InFacade) { return InFacade->Idx; });
		UnionMetadata = MakeShared<PCGExData::FUnionMetadata>();
		UnionMetadata->SetNum(NumPoints);

		if (!Blender->Init(this, OutputFacade, UnionMetadata, PCGExData::EProxyFlags::Direct)) // might want to switch to cache here?
		{
			PCGE_LOG_C(Error, GraphAndLog, this, FTEXT("Error while initializing data blending"));
			Blender.Reset();
			return;
		}

		// Helper to get projected Z from source
		auto GetProjectedZ = [this](uint32 SrcIdx, uint32 PtIdx) -> double
		{
			const int32 ArrayIdx = static_cast<int32>(SrcIdx);
			if (ArrayIdx < 0 || ArrayIdx >= AllOpData->ProjectedZValues.Num()) { return 0.0; }

			const TArray<double>& ZValues = AllOpData->ProjectedZValues[ArrayIdx];
			if (static_cast<int32>(PtIdx) >= ZValues.Num()) { return 0.0; }

			return ZValues[PtIdx];
		};

		// Helper to get projection details for a source
		auto GetProjection = [this](uint32 SrcIdx) -> const FPCGExGeo2DProjectionDetails*
		{
			const int32 ArrayIdx = static_cast<int32>(SrcIdx);
			if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Projections.Num()) { return nullptr; }
			return &AllOpData->Projections[ArrayIdx];
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
						if (TransformMode == PCGExClipper2::ETransformRestoration::Unproject)
						{
							// For unproject mode: interpolate projected Z from the 4 source points
							const double Z1Bot = GetProjectedZ(BlendInfo->E1BotSourceIdx, BlendInfo->E1BotPointIdx);
							const double Z1Top = GetProjectedZ(BlendInfo->E1TopSourceIdx, BlendInfo->E1TopPointIdx);
							const double Z2Bot = GetProjectedZ(BlendInfo->E2BotSourceIdx, BlendInfo->E2BotPointIdx);
							const double Z2Top = GetProjectedZ(BlendInfo->E2TopSourceIdx, BlendInfo->E2TopPointIdx);

							// Interpolate Z along each edge, then average
							const double Z1 = FMath::Lerp(Z1Bot, Z1Top, BlendInfo->E1Alpha);
							const double Z2 = FMath::Lerp(Z2Bot, Z2Top, BlendInfo->E2Alpha);
							const double ProjectedZ = (Z1 + Z2) * 0.5;

							// Get projection from first valid source
							const FPCGExGeo2DProjectionDetails* Projection = GetProjection(BlendInfo->E1BotSourceIdx);
							if (!Projection) { Projection = GetProjection(BlendInfo->E2BotSourceIdx); }

							// Build projected position and unproject
							FVector UnprojectedPos(
								static_cast<double>(Pt.x) * InvScale,
								static_cast<double>(Pt.y) * InvScale,
								ProjectedZ
							);

							if (Projection) { Projection->UnprojectInPlace(UnprojectedPos); }

							// Interpolate rotation/scale from source transforms
							auto GetSourceTransform = [this](uint32 PtIdx, uint32 SrcIdx, FTransform& OutT) -> bool
							{
								const int32 ArrayIdx = static_cast<int32>(SrcIdx);
								if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num()) { return false; }

								const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[ArrayIdx];
								const int32 NumPts = SrcFacade->Source->GetNum(PCGExData::EIOSide::In);
								if (static_cast<int32>(PtIdx) >= NumPts) { return false; }

								TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
								OutT = SrcTransforms[PtIdx];
								return true;
							};

							FTransform E1Bot, E1Top, E2Bot, E2Top;
							bool bHasE1Bot = GetSourceTransform(BlendInfo->E1BotPointIdx, BlendInfo->E1BotSourceIdx, E1Bot);
							bool bHasE1Top = GetSourceTransform(BlendInfo->E1TopPointIdx, BlendInfo->E1TopSourceIdx, E1Top);
							bool bHasE2Bot = GetSourceTransform(BlendInfo->E2BotPointIdx, BlendInfo->E2BotSourceIdx, E2Bot);
							bool bHasE2Top = GetSourceTransform(BlendInfo->E2TopPointIdx, BlendInfo->E2TopSourceIdx, E2Top);

							// Interpolate for rotation/scale
							FTransform E1Interp = E1Bot;
							if (bHasE1Bot && bHasE1Top) { E1Interp.Blend(E1Bot, E1Top, BlendInfo->E1Alpha); }

							FTransform E2Interp = E2Bot;
							if (bHasE2Bot && bHasE2Top) { E2Interp.Blend(E2Bot, E2Top, BlendInfo->E2Alpha); }

							OutTransform.Blend(E1Interp, E2Interp, 0.5);
							OutTransform.SetLocation(UnprojectedPos);
						}
						else
						{
							// FromSource mode: use original approach - interpolate transforms directly
							auto GetSourceTransform = [this](uint32 PtIdx, uint32 SrcIdx, FTransform& OutT) -> bool
							{
								const int32 ArrayIdx = static_cast<int32>(SrcIdx);
								if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num()) { return false; }

								const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[ArrayIdx];
								const int32 NumPts = SrcFacade->Source->GetNum(PCGExData::EIOSide::In);
								if (static_cast<int32>(PtIdx) >= NumPts) { return false; }

								TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
								OutT = SrcTransforms[PtIdx];
								return true;
							};

							FTransform E1Bot, E1Top, E2Bot, E2Top;
							bool bHasE1Bot = GetSourceTransform(BlendInfo->E1BotPointIdx, BlendInfo->E1BotSourceIdx, E1Bot);
							bool bHasE1Top = GetSourceTransform(BlendInfo->E1TopPointIdx, BlendInfo->E1TopSourceIdx, E1Top);
							bool bHasE2Bot = GetSourceTransform(BlendInfo->E2BotPointIdx, BlendInfo->E2BotSourceIdx, E2Bot);
							bool bHasE2Top = GetSourceTransform(BlendInfo->E2TopPointIdx, BlendInfo->E2TopSourceIdx, E2Top);

							FTransform E1Interp = E1Bot;
							if (bHasE1Bot && bHasE1Top) { E1Interp.Blend(E1Bot, E1Top, BlendInfo->E1Alpha); }

							FTransform E2Interp = E2Bot;
							if (bHasE2Bot && bHasE2Top) { E2Interp.Blend(E2Bot, E2Top, BlendInfo->E2Alpha); }

							OutTransform.Blend(E1Interp, E2Interp, 0.5);
						}

						// Add all 4 vertices to union for metadata blending
						TSharedPtr<PCGExData::IUnionData> Union = UnionMetadata->NewEntryAt_Unsafe(i);

						auto AddToUnion = [&](uint32 PtIdx, uint32 SrcIdx)
						{
							const int32 ArrayIdx = static_cast<int32>(SrcIdx);
							if (ArrayIdx < 0 || ArrayIdx >= AllOpData->Facades.Num()) { return; }

							const TSharedPtr<PCGExData::FFacade>& SourceFacade = AllOpData->Facades[ArrayIdx];
							const int32 NumPts = SourceFacade->Source->GetNum(PCGExData::EIOSide::In);

							if (static_cast<int32>(PtIdx) >= NumPts) { return; }

							Union->Add_Unsafe(static_cast<int32>(PtIdx), SourceFacade->Idx);
						};

						AddToUnion(BlendInfo->E1BotPointIdx, BlendInfo->E1BotSourceIdx);
						AddToUnion(BlendInfo->E1TopPointIdx, BlendInfo->E1TopSourceIdx);
						AddToUnion(BlendInfo->E2BotPointIdx, BlendInfo->E2BotSourceIdx);
						AddToUnion(BlendInfo->E2TopPointIdx, BlendInfo->E2TopSourceIdx);
					}
					else
					{
						// No BlendInfo found for this intersection point.
						// Walk prev/next in the path to find non-intersection neighbors and interpolate.
						auto GetNeighborTransform = [&](int32 Dir) -> TPair<FTransform, int32>
						{
							for (int32 Step = 1; Step < NumPoints; Step++)
							{
								const int32 Ni = (i + Dir * Step + NumPoints) % NumPoints;
								const PCGExClipper2Lib::Point64& NPt = Path[Ni];
								uint32 NPtIdx, NSrcIdx;
								PCGEx::H64(static_cast<uint64>(NPt.z), NPtIdx, NSrcIdx);

								if (NPtIdx != PCGExClipper2::INTERSECTION_MARKER)
								{
									const int32 NArrayIdx = static_cast<int32>(NSrcIdx);
									if (NArrayIdx >= 0 && NArrayIdx < AllOpData->Facades.Num())
									{
										const TSharedPtr<PCGExData::FFacade>& NSrcFacade = AllOpData->Facades[NArrayIdx];
										const int32 NNumPts = NSrcFacade->Source->GetNum(PCGExData::EIOSide::In);
										if (static_cast<int32>(NPtIdx) < NNumPts)
										{
											TConstPCGValueRange<FTransform> NSrcTransforms = NSrcFacade->Source->GetIn()->GetConstTransformValueRange();
											return TPair<FTransform, int32>(NSrcTransforms[NPtIdx], NArrayIdx);
										}
									}
									break;
								}
							}
							return TPair<FTransform, int32>(FTransform::Identity, INDEX_NONE);
						};

						auto [PrevT, PrevSrc] = GetNeighborTransform(-1);
						auto [NextT, NextSrc] = GetNeighborTransform(+1);

						if (PrevSrc != INDEX_NONE && NextSrc != INDEX_NONE)
						{
							OutTransform.Blend(PrevT, NextT, 0.5);
						}
						else if (PrevSrc != INDEX_NONE)
						{
							OutTransform = PrevT;
						}
						else if (NextSrc != INDEX_NONE)
						{
							OutTransform = NextT;
						}

						if (TransformMode == PCGExClipper2::ETransformRestoration::Unproject)
						{
							const FPCGExGeo2DProjectionDetails* Projection = GetProjection(
								PrevSrc != INDEX_NONE ? static_cast<uint32>(PrevSrc) :
								NextSrc != INDEX_NONE ? static_cast<uint32>(NextSrc) : 0);

							FVector UnprojectedPos(
								static_cast<double>(Pt.x) * InvScale,
								static_cast<double>(Pt.y) * InvScale,
								0.0);

							if (Projection) { Projection->UnprojectInPlace(UnprojectedPos); }
							OutTransform.SetLocation(UnprojectedPos);
						}

						// Add both neighbors to union for metadata blending
						TSharedPtr<PCGExData::IUnionData> Union = UnionMetadata->NewEntryAt_Unsafe(i);

						auto AddNeighborToUnion = [&](int32 Dir)
						{
							for (int32 Step = 1; Step < NumPoints; Step++)
							{
								const int32 Ni = (i + Dir * Step + NumPoints) % NumPoints;
								const PCGExClipper2Lib::Point64& NPt = Path[Ni];
								uint32 NPtIdx, NSrcIdx;
								PCGEx::H64(static_cast<uint64>(NPt.z), NPtIdx, NSrcIdx);

								if (NPtIdx != PCGExClipper2::INTERSECTION_MARKER)
								{
									const int32 NArrayIdx = static_cast<int32>(NSrcIdx);
									if (NArrayIdx >= 0 && NArrayIdx < AllOpData->Facades.Num())
									{
										const TSharedPtr<PCGExData::FFacade>& NSrcFacade = AllOpData->Facades[NArrayIdx];
										const int32 NNumPts = NSrcFacade->Source->GetNum(PCGExData::EIOSide::In);
										if (static_cast<int32>(NPtIdx) < NNumPts)
										{
											Union->Add_Unsafe(static_cast<int32>(NPtIdx), NSrcFacade->Idx);
										}
									}
									break;
								}
							}
						};

						AddNeighborToUnion(-1);
						AddNeighborToUnion(+1);
					}
				}
				else
				{
					// Regular point
					const int32 SourceArrayIdx = static_cast<int32>(SourceIdx);
					FPlatformAtomics::InterlockedExchange(&VisitedSources[SourceIdx], 1);

					if (TransformMode == PCGExClipper2::ETransformRestoration::Unproject)
					{
						// Unproject mode: use Clipper2 X/Y + stored projected Z
						const double ProjectedZ = GetProjectedZ(SourceIdx, OriginalPointIdx);
						const FPCGExGeo2DProjectionDetails* Projection = GetProjection(SourceIdx);

						FVector UnprojectedPos(
							static_cast<double>(Pt.x) * InvScale,
							static_cast<double>(Pt.y) * InvScale,
							ProjectedZ
						);

						if (Projection) { Projection->UnprojectInPlace(UnprojectedPos); }

						// Get rotation/scale from source
						if (SourceArrayIdx >= 0 && SourceArrayIdx < AllOpData->Facades.Num())
						{
							const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[SourceArrayIdx];
							const int32 SrcNumPoints = SrcFacade->Source->GetNum(PCGExData::EIOSide::In);

							if (static_cast<int32>(OriginalPointIdx) < SrcNumPoints)
							{
								TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
								OutTransform = SrcTransforms[OriginalPointIdx];
							}
						}

						// Override position with unprojected position
						OutTransform.SetLocation(UnprojectedPos);
					}
					else
					{
						// FromSource mode: restore original transform
						if (SourceArrayIdx >= 0 && SourceArrayIdx < AllOpData->Facades.Num())
						{
							const TSharedPtr<PCGExData::FFacade>& SrcFacade = AllOpData->Facades[SourceArrayIdx];
							const int32 SrcNumPoints = SrcFacade->Source->GetNum(PCGExData::EIOSide::In);

							if (static_cast<int32>(OriginalPointIdx) < SrcNumPoints)
							{
								TConstPCGValueRange<FTransform> SrcTransforms = SrcFacade->Source->GetIn()->GetConstTransformValueRange();
								OutTransform = SrcTransforms[OriginalPointIdx];
							}
						}
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
			}, NumPoints < 128);

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
			if (JointWriter) { SourceEndpointClass.SetNumZeroed(NumPoints); }

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
							if (static_cast<int32>(FlagPtIdx) == 0) { SourceEndpointClass[i] = 1; }
							else if (static_cast<int32>(FlagPtIdx) == SrcNumPts - 1) { SourceEndpointClass[i] = 2; }
						}
					}
				}

				// Pass 2: detect joint arc boundaries -- only flag the first/last point of each arc run
				if (JointWriter)
				{
					for (int32 i = 0; i < NumPoints; i++)
					{
						const int8 Current = SourceEndpointClass[i];
						if (Current == 0) { continue; }

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
		if (bIsHole && Settings->bTagHoles) { NewPointIO->Tags->AddRaw(Settings->HoleTag); }

		PCGExPaths::Helpers::SetClosedLoop(NewPointIO, bClosedPaths);

		if (!bClosedPaths && Settings->OpenPathsOutput == EPCGExClipper2OpenPathOutput::OutputPin) { NewPointIO->OutputPin = FName("Open Paths"); }

		TSharedPtr<PCGExData::FTags> GatheredTags = GatherTags(VisitedSources);
		NewPointIO->Tags->Append(GatheredTags.ToSharedRef());

		CarryOverDetails.Prune(NewPointIO->Tags.Get());

		OutPaths.Add(NewPointIO);
		LocalPathIndex++;
	}
}

void FPCGExClipper2ProcessorContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	// Base implementation does nothing - derived classes override this
}


bool FPCGExClipper2ProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

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

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetOrCreateHandle();

		for (int32 i = 0; i < Context->ProcessingGroups.Num(); i++)
		{
			WorkTasks->AddSimpleCallback([Settings, WeakHandle, Index = i]
			{
				FPCGContext::FSharedContext<FPCGExClipper2ProcessorContext> SharedContext(WeakHandle);
				if (!SharedContext.Get()) { return; }

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
	if (!Collection) { return 0; }

	const int32 NumInputs = Collection->Num();
	if (NumInputs == 0) { return 0; }

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
				if (!bIsClosed && !bSupportOpenPaths) { return; }

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
				if (!LocalProjection.Init(Facade)) { return; }

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

					Result.Path64.emplace_back(
						static_cast<int64_t>(ProjectedLocation.X * Scale),
						static_cast<int64_t>(ProjectedLocation.Y * Scale),
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
		if (!Result.bValid) { continue; }

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
		if (Idx < AllFacades.Num()) { MainFacades.Add(AllFacades[Idx]); }
	}

	// Determine main data partitions
	TArray<TArray<int32>> MainPartitions;

	bool bDoMainMatching = false;
	if (Settings->MainDataMatching.IsEnabled() && Settings->MainDataMatching.Mode != EPCGExMapMatchMode::Disabled)
	{
		auto Matcher = MakeShared<PCGExMatching::FDataMatcher>();
		Matcher->SetDetails(&Settings->MainDataMatching);

		bDoMainMatching = Matcher->Init(Context, MainFacades, true);

		if (bDoMainMatching)
		{
			PCGExMatching::Helpers::GetMatchingSourcePartitions(Matcher, MainFacades, MainPartitions, true);
		}
	}

	if (!bDoMainMatching)
	{
		// No matching - each main input is its own group
		switch (Settings->MainInputGroupingPolicy)
		{
		case EPCGExGroupingPolicy::Split:
			MainPartitions.Reserve(MainIndices.Num());
			for (const int32 Index : MainIndices) { MainPartitions.Add({Index}); }
			break;
		case EPCGExGroupingPolicy::Consolidate:
			{
				TArray<int32>& Consolidated = MainPartitions.Emplace_GetRef();
				Consolidated.Append(MainIndices);
			}
			break;
		}
	}
	else
	{
		// Convert facade indices to AllOpData indices
		for (TArray<int32>& Partition : MainPartitions)
		{
			for (int32& Idx : Partition)
			{
				if (Idx < MainFacades.Num())
				{
					// Now that Facade->Idx == ArrayIndex, we can use it directly
					const TSharedPtr<PCGExData::FFacade>& Facade = MainFacades[Idx];
					Idx = Facade->Idx;
				}
			}
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
			if (Idx < AllFacades.Num()) { OperandFacades.Add(AllFacades[Idx]); }
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

	// Build processing groups
	Context->ProcessingGroups.Reserve(MainPartitions.Num());

	for (int32 i = 0; i < MainPartitions.Num(); i++)
	{
		TSharedPtr<PCGExClipper2::FProcessingGroup> Group = MakeShared<PCGExClipper2::FProcessingGroup>();
		Group->SubjectIndices = MoveTemp(MainPartitions[i]);

		if (i < OperandPartitions.Num()) { Group->OperandIndices = MoveTemp(OperandPartitions[i]); }

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
