// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathInsert.h"

#include "Algo/RemoveIf.h"
#include "Async/TaskGraphInterfaces.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"
#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "PCGExPathInsertElement"
#define PCGEX_NAMESPACE PathInsert

#if WITH_EDITORONLY_DATA
void UPCGExPathInsertSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Blending) { Blending = NewObject<UPCGExSubPointsBlendInterpolate>(this, TEXT("Blending")); }
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExPathInsertSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, "The point data to insert into paths.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBlending::Labels::SourceOverridesBlendingOps)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPathInsertSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathInsert)
PCGEX_ELEMENT_BATCH_POINT_IMPL(PathInsert)

bool FPCGExPathInsertElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PathInsert)

	PCGEX_BIND_INSTANCED_FACTORY(Blending, UPCGExSubPointsBlendInstancedFactory, PCGExBlending::Labels::SourceOverridesBlendingOps)

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->NumMaxTargets = Context->TargetsHandler->Init(Context, PCGExCommon::Labels::SourceTargetsLabel);

	if (!Context->NumMaxTargets)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No valid targets to insert."));
		return false;
	}

	// Initialize claim map for exclusive targets mode
	if (Settings->bExclusiveTargets)
	{
		Context->TargetClaimMap = MakeShared<PCGExPathInsert::FTargetClaimMap>();
		Context->TargetClaimMap->Reserve(Context->NumMaxTargets);
	}

	return true;
}

bool FPCGExPathInsertElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathInsertElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathInsert)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetOrCreateHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

			Context->TargetsHandler->SetMatchingDetails(Context, &Settings->DataMatching);

			// Build shared target map when data matching is disabled (common case optimization)
			if (!Settings->DataMatching.IsEnabled())
			{
				Context->TargetsHandler->BuildFlatTargetMap(Context->SharedTargetPrefixSums, nullptr);
				Context->SharedTotalTargets = Context->SharedTargetPrefixSums.Last();
				Context->bUseSharedTargetMap = true;
			}

			PCGEX_ON_INVALILD_INPUTS_C(SharedContext.Get(), FTEXT("Some inputs have less than 2 points and won't be processed."))

			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					if (Entry->GetNum() < 2)
					{
						Entry->InitializeOutput(PCGExData::EIOInit::Forward);
						if (Settings->bTagIfNoInserts) { Entry->Tags->AddRaw(Settings->NoInsertsTag); }
						bHasInvalidInputs = true;
						return false;
					}
					return true;
				}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				Context->CancelExecution(TEXT("Could not find any paths to process."));
			}
		};

		Context->TargetsHandler->StartLoading(Context->GetTaskManager());
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

#pragma region FProcessor

namespace PCGExPathInsert
{
	void FProcessor::GatherCandidates()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathInsert::GatherCandidates);

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;
		TConstPCGValueRange<FTransform> PathTransforms = PointIO->GetIn()->GetConstTransformValueRange();

		// Cache first/last edge info for extension checks
		const bool bCanExtend = !bClosedLoop && !Settings->bEdgeInteriorOnly && Settings->bAllowPathExtension;
		FVector FirstEdgeStart = FVector::ZeroVector;
		FVector FirstEdgeDir = FVector::ZeroVector;
		FVector LastEdgeEnd = FVector::ZeroVector;
		FVector LastEdgeDir = FVector::ZeroVector;

		if (bCanExtend && Path->NumEdges > 0)
		{
			const PCGExPaths::FPathEdge& FirstEdge = Path->Edges[0];
			FirstEdgeStart = PathTransforms[FirstEdge.Start].GetLocation();
			const FVector FirstEdgeEndPos = PathTransforms[FirstEdge.End].GetLocation();
			FirstEdgeDir = (FirstEdgeEndPos - FirstEdgeStart).GetSafeNormal();

			const PCGExPaths::FPathEdge& LastEdge = Path->Edges[Path->LastEdge];
			const FVector LastEdgeStart = PathTransforms[LastEdge.Start].GetLocation();
			LastEdgeEnd = PathTransforms[LastEdge.End].GetLocation();
			LastEdgeDir = (LastEdgeEnd - LastEdgeStart).GetSafeNormal();
		}

		// Use shared target map when available (data matching disabled), otherwise build local
		const TArray<int32>* TargetPrefixSumsPtr;
		TArray<int32> LocalTargetPrefixSums;
		int32 TotalTargets;

		if (Context->bUseSharedTargetMap)
		{
			TargetPrefixSumsPtr = &Context->SharedTargetPrefixSums;
			TotalTargets = Context->SharedTotalTargets;
		}
		else
		{
			Context->TargetsHandler->BuildFlatTargetMap(LocalTargetPrefixSums, &IgnoreList);
			TargetPrefixSumsPtr = &LocalTargetPrefixSums;
			TotalTargets = LocalTargetPrefixSums.Last();
		}

		const TArray<int32>& TargetPrefixSums = *TargetPrefixSumsPtr;

		if (TotalTargets == 0) { return; }

		// Read range value once if constant (optimization for most common case)
		const double MaxRange = RangeGetter ? RangeGetter->Read(0) : MAX_dbl;
		const float MaxRangeF = static_cast<float>(MaxRange);

		// Chunked parallelism - zero contention, each chunk has its own array
		const int32 NumChunks = FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads());
		const int32 ChunkSize = (TotalTargets + NumChunks - 1) / NumChunks;

		TArray<TArray<FCompactCandidate>> ChunkResults;
		ChunkResults.SetNum(NumChunks);

		// Parallel iteration over chunks
		ParallelFor(NumChunks, [&](int32 ChunkIndex)
		{
			const int32 Start = ChunkIndex * ChunkSize;
			const int32 End = FMath::Min(Start + ChunkSize, TotalTargets);
			
			TArray<FCompactCandidate>& LocalCandidates = ChunkResults[ChunkIndex];

			for (int32 i = Start; i < End; i++)
			{
				const PCGExData::FConstPoint TargetPoint = Context->TargetsHandler->GetPointByFlatIndex(i, TargetPrefixSums, nullptr);
				const FVector TargetLocation = TargetPoint.GetLocation();

				double BestDistSq = MAX_dbl;
				int32 BestEdgeIndex = -1;
				double BestAlpha = 0;

				// Find closest point on any edge
				for (int32 EdgeIdx = 0; EdgeIdx < Path->NumEdges; EdgeIdx++)
				{
					const PCGExPaths::FPathEdge& Edge = Path->Edges[EdgeIdx];

					const FVector EdgeStart = PathTransforms[Edge.Start].GetLocation();
					const FVector EdgeEnd = PathTransforms[Edge.End].GetLocation();

					// Find closest point on segment
					const FVector ClosestPoint = FMath::ClosestPointOnSegment(TargetLocation, EdgeStart, EdgeEnd);
					const double DistSq = FVector::DistSquared(TargetLocation, ClosestPoint);

					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestEdgeIndex = EdgeIdx;

						// Compute alpha (0-1 along edge)
						const double EdgeLength = FVector::Dist(EdgeStart, EdgeEnd);
						BestAlpha = (EdgeLength > KINDA_SMALL_NUMBER) ? FVector::Dist(EdgeStart, ClosestPoint) / EdgeLength : 0;
					}
				}

				if (BestEdgeIndex < 0) { continue; }

				const float BestDist = static_cast<float>(FMath::Sqrt(BestDistSq));

				// Check range filter if enabled
				if (RangeGetter && BestDist > MaxRangeF) { continue; }

				// Check for path extension (open paths only)
				if (bCanExtend)
				{
					// Check if target is beyond path start
					if (BestEdgeIndex == 0 && BestAlpha < KINDA_SMALL_NUMBER)
					{
						const double ProjectionDist = FVector::DotProduct(TargetLocation - FirstEdgeStart, FirstEdgeDir);
						if (ProjectionDist < 0)
						{
							FCompactCandidate Compact;
							Compact.TargetFlatIndex = i;
							Compact.EdgeIndex = -1; // Pre-path marker
							Compact.Alpha = static_cast<float>(ProjectionDist);
							Compact.Distance = BestDist;
							LocalCandidates.Add(Compact);
							continue;
						}
					}

					// Check if target is beyond path end
					if (BestEdgeIndex == Path->LastEdge && BestAlpha > (1.0 - KINDA_SMALL_NUMBER))
					{
						const double ProjectionDist = FVector::DotProduct(TargetLocation - LastEdgeEnd, LastEdgeDir);
						if (ProjectionDist > 0)
						{
							FCompactCandidate Compact;
							Compact.TargetFlatIndex = i;
							Compact.EdgeIndex = Path->NumEdges; // Post-path marker
							Compact.Alpha = static_cast<float>(ProjectionDist);
							Compact.Distance = BestDist;
							LocalCandidates.Add(Compact);
							continue;
						}
					}
				}

				// Skip endpoint candidates if edge interior only is enabled
				if (Settings->bEdgeInteriorOnly)
				{
					const bool bAtStart = BestAlpha < KINDA_SMALL_NUMBER;
					const bool bAtEnd = BestAlpha > (1.0 - KINDA_SMALL_NUMBER);
					if (bAtStart || bAtEnd) { continue; }
				}

				// Add as regular edge candidate
				FCompactCandidate Compact;
				Compact.TargetFlatIndex = i;
				Compact.EdgeIndex = BestEdgeIndex;
				Compact.Alpha = static_cast<float>(BestAlpha);
				Compact.Distance = BestDist;
				LocalCandidates.Add(Compact);
			}
		});

		// Merge chunks and partition into destination arrays
		for (const TArray<FCompactCandidate>& Chunk : ChunkResults)
		{
			for (const FCompactCandidate& Compact : Chunk)
			{
				// Reconstruct full candidate
				const PCGExData::FConstPoint TargetPoint = Context->TargetsHandler->GetPointByFlatIndex(Compact.TargetFlatIndex, TargetPrefixSums, nullptr);

				FInsertCandidate Candidate;
				Candidate.TargetIOIndex = TargetPoint.IO;
				Candidate.TargetPointIndex = TargetPoint.Index;
				Candidate.EdgeIndex = Compact.EdgeIndex;
				Candidate.Alpha = Compact.Alpha;
				Candidate.Distance = Compact.Distance;
				Candidate.OriginalLocation = TargetPoint.GetLocation();

				// Compute PathLocation based on edge type
				if (Compact.EdgeIndex < 0)
				{
					// Pre-path: Alpha is projection distance (negative)
					Candidate.PathLocation = FirstEdgeStart + FirstEdgeDir * Compact.Alpha;
					PrePathInserts.Add(Candidate);
				}
				else if (Compact.EdgeIndex >= Path->NumEdges)
				{
					// Post-path: Alpha is projection distance (positive)
					Candidate.PathLocation = LastEdgeEnd + LastEdgeDir * Compact.Alpha;
					PostPathInserts.Add(Candidate);
				}
				else
				{
					// Regular edge: Alpha is 0-1 along edge
					const PCGExPaths::FPathEdge& Edge = Path->Edges[Compact.EdgeIndex];
					const FVector Start = PathTransforms[Edge.Start].GetLocation();
					const FVector End = PathTransforms[Edge.End].GetLocation();
					Candidate.PathLocation = FMath::Lerp(Start, End, Compact.Alpha);
					EdgeInserts[Compact.EdgeIndex].Add(Candidate);
				}
			}
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathInsert::Process);

		// Can't support scoped here
		//PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointIO->GetIn());
		LastIndex = PointDataFacade->GetNum() - 1;

		// Build path structure
		Path = MakeShared<PCGExPaths::FPath>(PointIO->GetIn(), 0);
		Path->IOIndex = PointIO->IOIndex;
		PathLength = Path->AddExtra<PCGExPaths::FPathEdgeLength>();
		Path->ComputeAllEdgeExtra();

		// Initialize edge inserts array
		EdgeInserts.SetNum(Path->NumEdges);
		StartIndices.SetNum(PointDataFacade->GetNum());

		// Initialize range getter if filtering by range
		if (Settings->bWithinRange)
		{
			RangeGetter = Settings->Range.GetValueSetting();
			if (!RangeGetter->Init(PointDataFacade)) { return false; }
		}

		// Initialize limit getter if limiting inserts per edge
		if (Settings->bLimitInsertsPerEdge)
		{
			LimitGetter = Settings->InsertLimit.GetValueSetting();
			if (!LimitGetter->Init(PointDataFacade)) { return false; }
		}

		// Create sub-blending operation
		SubBlending = Context->Blending->CreateOperation();
		SubBlending->bClosedLoop = bClosedLoop;

		// Populate ignore list based on matching rules
		IgnoreList.Add(PointDataFacade->GetIn());
		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true); !Context->TargetsHandler->PopulateIgnoreList(PointDataFacade->Source, MatchingScope, IgnoreList))
		{
			(void)Context->TargetsHandler->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		// Stage 1: Gather candidates from all targets (parallel)
		GatherCandidates();

		// Apply insert limits per edge if enabled
		if (LimitGetter)
		{
			const double LimitValue = LimitGetter->Read(0);

			for (int32 EdgeIdx = 0; EdgeIdx < Path->NumEdges; EdgeIdx++)
			{
				FEdgeInserts& EI = EdgeInserts[EdgeIdx];
				if (EI.IsEmpty()) { continue; }

				int32 MaxInserts;
				if (Settings->LimitMode == EPCGExInsertLimitMode::Discrete)
				{
					MaxInserts = FMath::Max(0, static_cast<int32>(LimitValue));
				}
				else // Distance/Spacing mode
				{
					const double EdgeLength = PathLength->Get(EdgeIdx);
					if (LimitValue > KINDA_SMALL_NUMBER)
					{
						const double FractionalMax = EdgeLength / LimitValue;
						MaxInserts = FMath::Max(0, static_cast<int32>(PCGExMath::TruncateDbl(FractionalMax, Settings->LimitTruncate)));
					}
					else
					{
						MaxInserts = MAX_int32; // No effective limit if spacing is 0
					}
				}

				if (EI.Num() > MaxInserts)
				{
					// Sort by distance (closest to path wins)
					EI.Inserts.Sort([](const FInsertCandidate& A, const FInsertCandidate& B) { return A.Distance < B.Distance; });
					// Truncate
					EI.Inserts.SetNum(MaxInserts);
					// Re-sort by alpha for output order
					EI.SortByAlpha();
				}
			}
		}
		else
		{
			// Just sort by alpha (no limits)
			for (FEdgeInserts& EI : EdgeInserts)
			{
				if (!EI.IsEmpty()) { EI.SortByAlpha(); }
			}
		}

		// Apply collocation filtering if enabled
		if (Settings->bPreventCollocation)
		{
			const double ToleranceSq = FMath::Square(Settings->CollocationTolerance);
			TConstPCGValueRange<FTransform> PathTransforms = PointIO->GetIn()->GetConstTransformValueRange();

			for (int32 EdgeIdx = 0; EdgeIdx < Path->NumEdges; EdgeIdx++)
			{
				FEdgeInserts& EI = EdgeInserts[EdgeIdx];
				if (EI.IsEmpty()) { continue; }

				const PCGExPaths::FPathEdge& Edge = Path->Edges[EdgeIdx];
				const FVector EdgeStart = PathTransforms[Edge.Start].GetLocation();
				const FVector EdgeEnd = PathTransforms[Edge.End].GetLocation();

				// Filter out inserts that are too close to vertices or each other
				TArray<FInsertCandidate> FilteredInserts;
				FilteredInserts.Reserve(EI.Num());

				for (const FInsertCandidate& Insert : EI.Inserts)
				{
					const FVector InsertPos = Settings->bSnapToPath ? Insert.PathLocation : Insert.OriginalLocation;

					// Check distance to edge start
					if (FVector::DistSquared(InsertPos, EdgeStart) < ToleranceSq) { continue; }

					// Check distance to edge end
					if (FVector::DistSquared(InsertPos, EdgeEnd) < ToleranceSq) { continue; }

					// Check distance to already accepted inserts
					bool bTooClose = false;
					for (const FInsertCandidate& Accepted : FilteredInserts)
					{
						const FVector AcceptedPos = Settings->bSnapToPath ? Accepted.PathLocation : Accepted.OriginalLocation;
						if (FVector::DistSquared(InsertPos, AcceptedPos) < ToleranceSq)
						{
							bTooClose = true;
							break;
						}
					}

					if (!bTooClose)
					{
						FilteredInserts.Add(Insert);
					}
				}

				EI.Inserts = MoveTemp(FilteredInserts);
			}
		}

		// Sort extension inserts by projection distance
		if (!PrePathInserts.IsEmpty())
		{
			// Sort ascending (most negative first = furthest from start comes first in path order)
			PrePathInserts.Sort([](const FInsertCandidate& A, const FInsertCandidate& B) { return A.Alpha < B.Alpha; });
		}
		if (!PostPathInserts.IsEmpty())
		{
			// Sort ascending (smallest positive first = closest to end comes first)
			PostPathInserts.Sort([](const FInsertCandidate& A, const FInsertCandidate& B) { return A.Alpha < B.Alpha; });
		}

		// Register candidates to claim map for exclusive targets mode
		if (Context->TargetClaimMap)
		{
			const int32 ProcessorIdx = BatchIndex;

			for (const FInsertCandidate& Insert : PrePathInserts)
			{
				Context->TargetClaimMap->RegisterCandidate(Insert.GetTargetHash(), ProcessorIdx, Insert.Distance);
			}

			for (const FInsertCandidate& Insert : PostPathInserts)
			{
				Context->TargetClaimMap->RegisterCandidate(Insert.GetTargetHash(), ProcessorIdx, Insert.Distance);
			}

			for (const FEdgeInserts& EI : EdgeInserts)
			{
				for (const FInsertCandidate& Insert : EI.Inserts)
				{
					Context->TargetClaimMap->RegisterCandidate(Insert.GetTargetHash(), ProcessorIdx, Insert.Distance);
				}
			}
		}

		// Count total inserts (will be recalculated in CompleteWork if exclusive mode filters some out)
		TotalInserts = PrePathInserts.Num() + PostPathInserts.Num();
		for (const FEdgeInserts& EI : EdgeInserts)
		{
			TotalInserts += EI.Num();
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		// Filter candidates for exclusive targets mode
		if (Context->TargetClaimMap)
		{
			const int32 ProcessorIdx = BatchIndex;

			// Filter pre-path inserts
			PrePathInserts.SetNum(Algo::StableRemoveIf(PrePathInserts, [&](const FInsertCandidate& Insert)
			{
				return !Context->TargetClaimMap->IsClaimedBy(Insert.GetTargetHash(), ProcessorIdx);
			}));

			// Filter post-path inserts
			PostPathInserts.SetNum(Algo::StableRemoveIf(PostPathInserts, [&](const FInsertCandidate& Insert)
			{
				return !Context->TargetClaimMap->IsClaimedBy(Insert.GetTargetHash(), ProcessorIdx);
			}));

			// Filter edge inserts
			for (FEdgeInserts& EI : EdgeInserts)
			{
				EI.Inserts.SetNum(Algo::StableRemoveIf(EI.Inserts, [&](const FInsertCandidate& Insert)
				{
					return !Context->TargetClaimMap->IsClaimedBy(Insert.GetTargetHash(), ProcessorIdx);
				}));
			}

			// Recalculate total inserts
			TotalInserts = PrePathInserts.Num() + PostPathInserts.Num();
			for (const FEdgeInserts& EI : EdgeInserts)
			{
				TotalInserts += EI.Num();
			}
		}

		// No inserts? Just forward the data
		if (TotalInserts == 0)
		{
			PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::Forward)
			if (Settings->bTagIfNoInserts) { PointIO->Tags->AddRaw(Settings->NoInsertsTag); }
			return;
		}

		// Calculate output size and start indices
		// Output order: [PrePathInserts] [Point0] [Edge0 Inserts] [Point1] ... [PointN] [PostPathInserts]
		const int32 NumOriginalPoints = PointDataFacade->GetNum();
		const int32 NumPreInserts = PrePathInserts.Num();
		const int32 NumPostInserts = PostPathInserts.Num();
		const int32 NumOutputPoints = NumOriginalPoints + TotalInserts;

		// Allocate output
		PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::New)

		const UPCGBasePointData* InPoints = PointIO->GetIn();
		UPCGBasePointData* OutPoints = PointIO->GetOut();
		UPCGMetadata* Metadata = OutPoints->Metadata;

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, NumOutputPoints, InPoints->GetAllocatedProperties());

		TConstPCGValueRange<int64> InMetadataEntries = InPoints->GetConstMetadataEntryValueRange();
		TPCGValueRange<int64> OutMetadataEntries = OutPoints->GetMetadataEntryValueRange();

		// Build index mapping for original points and initialize metadata
		TArray<int32> WriteIndices;
		WriteIndices.SetNum(NumOriginalPoints);

		int32 WriteIndex = 0;

		// Pre-path inserts (inherit from first point)
		for (int32 i = 0; i < NumPreInserts; i++)
		{
			OutMetadataEntries[WriteIndex] = PCGInvalidEntryKey;
			Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex], InMetadataEntries[0], InPoints->Metadata);
			WriteIndex++;
		}

		// Original points and edge inserts
		for (int32 i = 0; i < Path->NumEdges; i++)
		{
			WriteIndices[i] = WriteIndex;
			StartIndices[i] = WriteIndex;

			// Copy original point's metadata
			OutMetadataEntries[WriteIndex] = InMetadataEntries[i];
			Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex]);
			WriteIndex++;

			// Initialize metadata for inserted points (inherit from edge start)
			const int32 NumInserts = EdgeInserts[i].Num();
			for (int32 j = 0; j < NumInserts; j++)
			{
				OutMetadataEntries[WriteIndex] = PCGInvalidEntryKey;
				Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex], InMetadataEntries[i], InPoints->Metadata);
				WriteIndex++;
			}
		}

		// Handle last point for open paths
		if (!bClosedLoop)
		{
			WriteIndices[LastIndex] = WriteIndex;
			StartIndices[LastIndex] = WriteIndex;
			OutMetadataEntries[WriteIndex] = InMetadataEntries[LastIndex];
			Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex]);
			WriteIndex++;

			// Post-path inserts (inherit from last point)
			for (int32 i = 0; i < NumPostInserts; i++)
			{
				OutMetadataEntries[WriteIndex] = PCGInvalidEntryKey;
				Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex], InMetadataEntries[LastIndex], InPoints->Metadata);
				WriteIndex++;
			}
		}

		// Copy original points to new locations
		PointIO->InheritPoints(WriteIndices);

		// Create output writers and write default values for original points
		if (Settings->bFlagInsertedPoints)
		{
			FlagWriter = PointDataFacade->GetWritable<bool>(Settings->InsertedFlagName, false, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->InsertedFlagName);
		}

		if (Settings->bWriteAlpha)
		{
			AlphaWriter = PointDataFacade->GetWritable<double>(Settings->AlphaAttributeName, Settings->DefaultAlpha, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->AlphaAttributeName);
		}

		if (Settings->bWriteDistance)
		{
			DistanceWriter = PointDataFacade->GetWritable<double>(Settings->DistanceAttributeName, Settings->DefaultDistance, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->DistanceAttributeName);
		}

		if (Settings->bWriteTargetIndex)
		{
			TargetIndexWriter = PointDataFacade->GetWritable<int32>(Settings->TargetIndexAttributeName, Settings->DefaultTargetIndex, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->TargetIndexAttributeName);
		}

		if (Settings->bWriteDirection)
		{
			DirectionWriter = PointDataFacade->GetWritable<FVector>(Settings->DirectionAttributeName, Settings->DefaultDirection, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->DirectionAttributeName);
		}

		// Prepare blending
		if (!SubBlending->PrepareForData(Context, PointDataFacade, &ProtectedAttributes))
		{
			bIsProcessorValid = false;
			return;
		}

		// Initialize forward handlers for target attributes
		const int32 NumTargets = Context->TargetsHandler->Num();
		ForwardHandlers.Init(nullptr, NumTargets);
		Context->TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& InTarget, const int32 Index)
		{
			ForwardHandlers[Index] = Settings->TargetForwarding.TryGetHandler(InTarget, PointDataFacade, false);
		});

		// Tag output
		if (Settings->bTagIfHasInserts) { PointIO->Tags->AddRaw(Settings->HasInsertsTag); }

		// Process extension inserts (single-threaded, before parallel edge processing)
		TPCGValueRange<FTransform> OutTransforms = PointIO->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = PointIO->GetOut()->GetSeedValueRange(false);
		TConstPCGValueRange<FTransform> InTransforms = PointIO->GetIn()->GetConstTransformValueRange();

		// Pre-path extensions
		if (NumPreInserts > 0)
		{
			const FVector FirstPointPos = InTransforms[0].GetLocation();
			const int32 FirstPointOutIdx = StartIndices[0];

			PCGExPaths::FPathMetrics PreMetrics = PCGExPaths::FPathMetrics(
				Settings->bSnapToPath ? PrePathInserts[0].PathLocation : PrePathInserts[0].OriginalLocation);

			for (int32 i = 0; i < NumPreInserts; i++)
			{
				const FInsertCandidate& Insert = PrePathInserts[i];
				const FVector Position = Settings->bSnapToPath ? Insert.PathLocation : Insert.OriginalLocation;

				OutTransforms[i].SetLocation(Position);
				OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(Position);

				// Write output attributes for pre-path extension
				if (FlagWriter) { FlagWriter->SetValue(i, true); }
				if (AlphaWriter) { AlphaWriter->SetValue(i, Insert.Alpha); } // Negative = before start
				if (DistanceWriter) { DistanceWriter->SetValue(i, Insert.Distance); }
				if (TargetIndexWriter) { TargetIndexWriter->SetValue(i, Insert.TargetIOIndex); }
				if (DirectionWriter)
				{
					FVector Direction = (Insert.PathLocation - Insert.OriginalLocation).GetSafeNormal();
					if (Settings->bInvertDirection) { Direction = -Direction; }
					DirectionWriter->SetValue(i, Direction);
				}

				// Forward attributes from target
				if (const TSharedPtr<PCGExData::FDataForwardHandler>& Handler = ForwardHandlers[Insert.TargetIOIndex])
				{
					Handler->Forward(Insert.TargetPointIndex, i);
				}

				if (i > 0) { PreMetrics.Add(Position); }
			}

			PreMetrics.Add(FirstPointPos);

			if (NumPreInserts > 1)
			{
				PCGExData::FScope PreScope = PointDataFacade->GetOutScope(1, NumPreInserts - 1);
				SubBlending->ProcessSubPoints(
					PointDataFacade->GetOutPoint(0),
					PointDataFacade->GetOutPoint(FirstPointOutIdx),
					PreScope,
					PreMetrics);
			}
		}

		// Post-path extensions
		if (!bClosedLoop && NumPostInserts > 0)
		{
			const int32 LastPointOutIdx = StartIndices[LastIndex];
			const FVector LastPointPos = InTransforms[LastIndex].GetLocation();

			PCGExPaths::FPathMetrics PostMetrics = PCGExPaths::FPathMetrics(LastPointPos);

			for (int32 i = 0; i < NumPostInserts; i++)
			{
				const FInsertCandidate& Insert = PostPathInserts[i];
				const int32 InsertIndex = LastPointOutIdx + 1 + i;
				const FVector Position = Settings->bSnapToPath ? Insert.PathLocation : Insert.OriginalLocation;

				OutTransforms[InsertIndex].SetLocation(Position);
				OutSeeds[InsertIndex] = PCGExRandomHelpers::ComputeSpatialSeed(Position);

				// Write output attributes for post-path extension
				if (FlagWriter) { FlagWriter->SetValue(InsertIndex, true); }
				if (AlphaWriter) { AlphaWriter->SetValue(InsertIndex, 1.0 + Insert.Alpha); } // > 1 = after end
				if (DistanceWriter) { DistanceWriter->SetValue(InsertIndex, Insert.Distance); }
				if (TargetIndexWriter) { TargetIndexWriter->SetValue(InsertIndex, Insert.TargetIOIndex); }
				if (DirectionWriter)
				{
					FVector Direction = (Insert.PathLocation - Insert.OriginalLocation).GetSafeNormal();
					if (Settings->bInvertDirection) { Direction = -Direction; }
					DirectionWriter->SetValue(InsertIndex, Direction);
				}

				// Forward attributes from target
				if (const TSharedPtr<PCGExData::FDataForwardHandler>& Handler = ForwardHandlers[Insert.TargetIOIndex])
				{
					Handler->Forward(Insert.TargetPointIndex, InsertIndex);
				}

				PostMetrics.Add(Position);
			}

			if (NumPostInserts > 1)
			{
				PCGExData::FScope PostScope = PointDataFacade->GetOutScope(LastPointOutIdx + 1, NumPostInserts - 1);
				SubBlending->ProcessSubPoints(
					PointDataFacade->GetOutPoint(LastPointOutIdx),
					PointDataFacade->GetOutPoint(LastPointOutIdx + NumPostInserts),
					PostScope,
					PostMetrics);
			}
		}

		// Process edge inserts in parallel
		StartParallelLoopForRange(Path->NumEdges);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		TPCGValueRange<FTransform> OutTransforms = PointIO->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = PointIO->GetOut()->GetSeedValueRange(false);

		TConstPCGValueRange<FTransform> InTransforms = PointIO->GetIn()->GetConstTransformValueRange();

		// Process regular edge inserts
		PCGEX_SCOPE_LOOP(EdgeIndex)
		{
			const FEdgeInserts& EI = EdgeInserts[EdgeIndex];
			if (EI.IsEmpty()) { continue; }

			const PCGExPaths::FPathEdge& Edge = Path->Edges[EdgeIndex];
			const int32 OutStartIdx = StartIndices[EdgeIndex];

			const FVector EdgeStart = InTransforms[Edge.Start].GetLocation();
			const FVector EdgeEnd = InTransforms[Edge.End].GetLocation();

			// Determine the end index for blending
			int32 EndPointIndex;
			if (EdgeIndex == Path->LastEdge && !bClosedLoop)
			{
				EndPointIndex = StartIndices[LastIndex];
			}
			else
			{
				EndPointIndex = StartIndices[(EdgeIndex + 1) % Path->NumEdges];
			}

			// Build path metrics for blending
			PCGExPaths::FPathMetrics Metrics = PCGExPaths::FPathMetrics(EdgeStart);

			const int32 NumInserts = EI.Num();
			for (int32 i = 0; i < NumInserts; i++)
			{
				const FInsertCandidate& Insert = EI.Inserts[i];
				const int32 InsertIndex = OutStartIdx + 1 + i;

				// Set position based on snap setting
				const FVector Position = Settings->bSnapToPath ? Insert.PathLocation : Insert.OriginalLocation;

				OutTransforms[InsertIndex].SetLocation(Position);
				OutSeeds[InsertIndex] = PCGExRandomHelpers::ComputeSpatialSeed(Position);

				// Write output attributes
				if (FlagWriter) { FlagWriter->SetValue(InsertIndex, true); }
				if (AlphaWriter) { AlphaWriter->SetValue(InsertIndex, Insert.Alpha); }
				if (DistanceWriter) { DistanceWriter->SetValue(InsertIndex, Insert.Distance); }
				if (TargetIndexWriter) { TargetIndexWriter->SetValue(InsertIndex, Insert.TargetIOIndex); }
				if (DirectionWriter)
				{
					FVector Direction = (Insert.PathLocation - Insert.OriginalLocation).GetSafeNormal();
					if (Settings->bInvertDirection) { Direction = -Direction; }
					DirectionWriter->SetValue(InsertIndex, Direction);
				}

				// Forward attributes from target
				if (const TSharedPtr<PCGExData::FDataForwardHandler>& Handler = ForwardHandlers[Insert.TargetIOIndex])
				{
					Handler->Forward(Insert.TargetPointIndex, InsertIndex);
				}

				Metrics.Add(Position);
			}

			Metrics.Add(EdgeEnd);

			// Apply sub-blending for attributes
			PCGExData::FScope SubScope = PointDataFacade->GetOutScope(OutStartIdx + 1, NumInserts);
			SubBlending->ProcessSubPoints(
				PointDataFacade->GetOutPoint(OutStartIdx),
				PointDataFacade->GetOutPoint(EndPointIndex),
				SubScope,
				Metrics);
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
