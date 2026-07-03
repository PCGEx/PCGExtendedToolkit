// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClusterDiffuse.h"

#include "PCGExVersion.h"
#include "Blenders/PCGExUnionOpsManager.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Types/PCGExAttributeIdentity.h"

#define LOCTEXT_NAMESPACE "PCGExClusterDiffuse"
#define PCGEX_NAMESPACE ClusterDiffuse

#pragma region UPCGExClusterDiffuseSettings

PCGExData::EIOInit UPCGExClusterDiffuseSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExClusterDiffuseSettings::GetEdgeOutputInitMode() const
{
	// Edges are not modified by this node -- pass them through unchanged.
	return PCGExData::EIOInit::Forward;
}

TArray<FPCGPinProperties> UPCGExClusterDiffuseSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points.", Required)
	PCGEX_PIN_FACTORIES(PCGExFloodFill::SourceFillControlsLabel, "Fill controls, used to constraint & limit diffusion", Normal, FPCGExDataTypeInfoFillControl::AsId())
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal);
	PCGEX_PIN_FACTORIES(PCGExClusterDiffuse::SourceSeedBlendingLabel, "Blend operations that blend seed attributes (read from the seeds point cloud) onto reached vtx.", Normal, FPCGExDataTypeInfoBlendOp::AsId())

	return PinProperties;
}

#if WITH_EDITOR
void UPCGExClusterDiffuseSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 2)
	{
		// FillRate migrated from the Input/Attribute/Constant triple to FPCGExInputShorthandSelectorInteger32Abs.
		Diffusion.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

#pragma endregion

#pragma region FPCGExClusterDiffuseElement

PCGEX_INITIALIZE_ELEMENT(ClusterDiffuse)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ClusterDiffuse)

bool FPCGExClusterDiffuseElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDiffuse)

	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);

	// Seed Blend Ops (Layer 2) are optional -- they blend attributes from the seeds point cloud.
	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExClusterDiffuse::SourceSeedBlendingLabel, Context->SeedBlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);

	// Fill controls are optional
	PCGExFactories::GetInputFactories<UPCGExFillControlsFactoryData>(Context, PCGExFloodFill::SourceFillControlsLabel, Context->FillControlFactories, {FPCGExDataTypeInfoFillControl::AsId()}, false);

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	// Warm the per-seed factor once here (single-threaded, full read) so the parallel blend pass only reads it.
	Context->SeedFactorValue = Settings->SeedFactor.GetValueSetting();
	if (!Context->SeedFactorValue->Init(Context->SeedsDataFacade, false))
	{
		// Invalid factor attribute -- ignore the factor rather than fail the node.
		Context->SeedFactorValue = nullptr;
	}

	if (!Context->SeedBlendingFactories.IsEmpty())
	{
		// Warm the seeds-cloud attributes and resolve the blend configs once here (single-threaded, full reads)
		// so per-processor blending never enumerates metadata or cold-reads the shared seeds facade concurrently.
		TArray<PCGExData::FAttributeIdentity> SeedIdentities;
		PCGExBlending::GetFilteredIdentities(Context->SeedsDataFacade->GetIn()->Metadata, SeedIdentities);
		Context->SeedsDataFacade->CreateReadables(SeedIdentities, false);

		Context->SeedBlendOpsSchema = MakeShared<PCGExBlending::FBlendOpsSchema>();
		if (!Context->SeedBlendOpsSchema->Init(Context, Context->SeedBlendingFactories, {Context->SeedsDataFacade.ToSharedRef()}))
		{
			return false;
		}
	}

	// Build the Falloff shaping curve once. Linear (default) = identity; applied to the [0,1] intensity in Falloff space only.
	Context->FalloffLUT = Settings->FalloffCurveLookup.MakeLookup(
		Settings->bUseLocalFalloffCurve, Settings->LocalFalloffCurve, Settings->FalloffCurve,
		[](FRichCurve& CurveData)
		{
			CurveData.AddKey(0, 0);
			CurveData.AddKey(1, 1);
		});

	return true;
}

bool FPCGExClusterDiffuseElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExClusterDiffuseElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ClusterDiffuse)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

#pragma endregion

namespace PCGExClusterDiffuse
{
#pragma region FProcessor

	FProcessor::~FProcessor()
	{
	}

	void FProcessor::CompleteWork()
	{
		if (Diffusions.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No valid diffusions."));
			bIsProcessorValid = false;
			return;
		}

		// Nothing to blend without any blend operations -- the vtx pass through unchanged.
		if (Context->BlendingFactories.IsEmpty() && Context->SeedBlendingFactories.IsEmpty())
		{
			return;
		}

		// Per-vtx "does the original value participate" toggle (constant or attribute); full read (grouping below hits arbitrary vtx).
		const TSharedPtr<PCGExDetails::TSettingValue<bool>> ParticipatesValue = Settings->VtxParticipates.GetValueSetting();
		const bool bHasParticipates = ParticipatesValue && ParticipatesValue->Init(VtxDataFacade, false);

		// Per reached node, collect every seed that touched it, in CSR layout (count -> prefix-sum -> fill:
		// one flat allocation instead of one heap array per reached vtx). Built single-threaded (growth has
		// finished); no dedup needed except the self-participation case handled below.
		// MaxContributors sizes the per-thread scratch buffers in the blend pass.
		int32 MaxContributors = 0;
		{
			TArray<int32> Counts;
			Counts.Init(0, NumNodes);
			for (const TSharedPtr<PCGExFloodFill::FDiffusion>& Diffusion : Diffusions)
			{
				for (const PCGExFloodFill::FCandidate& Candidate : Diffusion->Captured)
				{
					Counts[Candidate.Node->Index]++;
				}
			}

			// Prefix sum, with one slack slot per reached node for the potential participation self-entry.
			ContribOffsets.SetNumUninitialized(NumNodes);
			int32 RunningTotal = 0;
			for (int32 i = 0; i < NumNodes; i++)
			{
				ContribOffsets[i] = RunningTotal;
				const int32 Count = Counts[i];
				RunningTotal += Count + ((bHasParticipates && Count > 0) ? 1 : 0);
				MaxContributors = FMath::Max(MaxContributors, Count);
			}
			MaxContributors += bHasParticipates ? 1 : 0;

			Contributions.SetNumUninitialized(RunningTotal);
			ContribEnds = ContribOffsets; // Write cursors
		}

		for (const TSharedPtr<PCGExFloodFill::FDiffusion>& Diffusion : Diffusions)
		{
			const int32 SeedVtx = Diffusion->SeedNode->PointIndex;
			const int32 SeedPoint = Diffusion->SeedIndex;
			// Per-diffusion falloff normalizers (0 at the seed, 1 at the diffusion's furthest reach).
			const int32 DiffMaxDepth = Diffusion->GetMaxDepth();
			const double DiffMaxDist = Diffusion->GetMaxDistance();
			const double InvMaxDepth = DiffMaxDepth > 0 ? 1.0 / static_cast<double>(DiffMaxDepth) : 0.0;
			const double InvMaxDist = DiffMaxDist > 0 ? 1.0 / DiffMaxDist : 0.0;
			for (const PCGExFloodFill::FCandidate& Candidate : Diffusion->Captured)
			{
				const double NormDepth = static_cast<double>(Candidate.Depth) * InvMaxDepth;
				const double NormDist = Candidate.PathDistance * InvMaxDist;
				Contributions[ContribEnds[Candidate.Node->Index]++] = FContribution{SeedVtx, SeedPoint, Candidate.Depth, NormDepth, NormDist, Candidate.PathDistance};
			}
		}

		// Add each reached vtx's own value as a contributor (depth 0), skipping vtx that are their own seed (avoid double-counting).
		if (bHasParticipates)
		{
			for (int32 NodeIndex = 0; NodeIndex < NumNodes; NodeIndex++)
			{
				const int32 Start = ContribOffsets[NodeIndex];
				const int32 End = ContribEnds[NodeIndex];
				if (Start == End)
				{
					continue;
				}

				const int32 VtxIndex = Cluster->GetNodePointIndex(NodeIndex);
				if (!ParticipatesValue->Read(VtxIndex))
				{
					continue;
				}

				bool bSelfPresent = false;
				for (int32 i = Start; i < End; i++)
				{
					if (Contributions[i].SeedVtxIndex == VtxIndex)
					{
						bSelfPresent = true;
						break;
					}
				}
				if (!bSelfPresent)
				{
					Contributions[ContribEnds[NodeIndex]++] = FContribution{VtxIndex, -1, 0, 0.0, 0.0, 0.0};
				}
			}
		}

		// Blenders are built once at batch scope (FBatch::Process) and shared via PrepareSingle, so this parallel
		// pass only writes into their pre-allocated buffers -- never allocating on the shared vtx facade. Either layer may be null.

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, BlendDiffusions)

		BlendDiffusions->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE, WeightMode = Settings->Weighting, WeightSpace = Settings->WeightSpace, MaxContributors](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			TArray<double> Weights;
			TArray<int32> Order;
			TArray<PCGExData::FWeightedPoint> VtxWeightedPoints;
			TArray<PCGExData::FWeightedPoint> SeedWeightedPoints;
			TArray<PCGEx::FOpStats> VtxTrackers;
			TArray<PCGEx::FOpStats> SeedTrackers;

			// Reserve once (buffers are reused via Reset across the chunk) so no vtx pays reallocation growth.
			Weights.Reserve(MaxContributors);
			Order.Reserve(MaxContributors);
			if (This->VtxBlender)
			{
				This->VtxBlender->InitTrackers(VtxTrackers);
				VtxWeightedPoints.Reserve(MaxContributors);
			}
			if (This->SeedBlender)
			{
				This->SeedBlender->InitTrackers(SeedTrackers);
				SeedWeightedPoints.Reserve(MaxContributors);
			}

			const TSharedPtr<PCGExDetails::TSettingValue<double>>& SeedFactor = This->Context->SeedFactorValue;

			// Scope loop runs over cluster NODE indices; the blend target is the node's vtx point index.
			PCGEX_SCOPE_LOOP(NodeIndex)
			{
				const int32 Count = This->ContribEnds[NodeIndex] - This->ContribOffsets[NodeIndex];
				if (Count == 0)
				{
					continue;
				}

				const TArrayView<const FContribution> Contributors(This->Contributions.GetData() + This->ContribOffsets[NodeIndex], Count);
				const int32 Index = This->Cluster->GetNodePointIndex(NodeIndex);

				Weights.Reset(Count);

				if (WeightSpace == EPCGExClusterDiffuseWeightSpace::Falloff)
				{
					// Absolute intensity: 1 at the seed, fading to 0 at that diffusion's furthest reach, scaled by the
					// per-seed factor. No normalization -- the multi-blend composites intensities and caps past 1. Any
					// unfilled remainder goes to the participation self-entry (fades to original), else the vtx fades to 0.
					double SumSeed = 0;
					int32 SelfIndex = -1;
					const PCGExFloatLUT& FalloffLUT = This->Context->FalloffLUT;
					for (int32 i = 0; i < Count; i++)
					{
						const FContribution& C = Contributors[i];
						if (C.SeedPointIndex < 0)
						{
							SelfIndex = i;
							Weights.Add(0.0);
							continue;
						}

						double Intensity;
						switch (WeightMode)
						{
						case EPCGExClusterDiffuseWeightMode::Distance:
							Intensity = 1.0 - C.NormDist;
							break;
						case EPCGExClusterDiffuseWeightMode::Depth:
							Intensity = 1.0 - C.NormDepth;
							break;
						case EPCGExClusterDiffuseWeightMode::CountAndDepth:
							Intensity = 0.5 + 0.5 * (1.0 - C.NormDepth);
							break;
						default:
							Intensity = 1.0;
							break; // Count: flat full
						}
						Intensity = FMath::Clamp(Intensity, 0.0, 1.0);
						if (FalloffLUT)
						{
							Intensity = FalloffLUT->Eval(Intensity);
						} // shape the falloff (linear = identity)
						if (SeedFactor)
						{
							Intensity *= FMath::Max(0.0, SeedFactor->Read(C.SeedPointIndex));
						}

						Weights.Add(Intensity);
						SumSeed += Intensity;
					}

					// Background fill: the vtx's own value takes whatever intensity the seeds leave.
					if (SelfIndex >= 0)
					{
						Weights[SelfIndex] = FMath::Max(0.0, 1.0 - SumSeed);
					}

					// No normalization, no reorder -- absolute weights fed as-is (fine for accumulate-from-zero blends).
				}
				else
				{
					// Relative: seeds compete territorially -- the strongest SEED reaches full intensity, so a lone seed
					// fully claims its vtx. The self-entry is NOT weighted by its own zero distance/depth (that pinned it to
					// the max and let it swallow the seeds); it joins as a peer to the strongest seed, with Seed Factor tilting
					// the balance (>1 favours seeds). With participation off, these steps reduce to the legacy raw -> factor -> normalize.
					if (Count == 1)
					{
						// A lone seed (participation never yields a count of one) is always weight 1: a blend of one is a copy.
						Weights.Add(1.0);
					}
					else
					{
						// 1. Raw seed weights (territorial competition). The self-entry gets 0 here; step 4 promotes it to a peer.
						switch (WeightMode)
						{
						case EPCGExClusterDiffuseWeightMode::Distance:
							// Inverse path length: the seed with the shorter path weighs more; where two diffusions meet both
							// are far, so weights converge (smooth blend). Path length (not straight-line) matches the traversal metric.
							for (const FContribution& C : Contributors)
							{
								Weights.Add(C.SeedPointIndex < 0 ? 0.0 : 1.0 / (C.PathDistance + 1.0));
							}
							break;
						case EPCGExClusterDiffuseWeightMode::Depth:
							for (const FContribution& C : Contributors)
							{
								Weights.Add(C.SeedPointIndex < 0 ? 0.0 : 1.0 / (static_cast<double>(C.Depth) + 1.0));
							}
							break;
						case EPCGExClusterDiffuseWeightMode::CountAndDepth:
						{
							double SumInvDepth = 0;
							int32 NumSeeds = 0;
							for (const FContribution& C : Contributors)
							{
								if (C.SeedPointIndex < 0)
								{
									continue;
								}
								SumInvDepth += 1.0 / (static_cast<double>(C.Depth) + 1.0);
								++NumSeeds;
							}
							const double EqualShare = NumSeeds > 0 ? 0.5 / static_cast<double>(NumSeeds) : 0.0;
							const double DepthScale = SumInvDepth > 0 ? 0.5 / SumInvDepth : 0.0;
							for (const FContribution& C : Contributors)
							{
								Weights.Add(C.SeedPointIndex < 0 ? 0.0 : EqualShare + (1.0 / (static_cast<double>(C.Depth) + 1.0)) * DepthScale);
							}
						}
						break;
						default: // Count
							for (int32 i = 0; i < Count; i++)
							{
								Weights.Add(Contributors[i].SeedPointIndex < 0 ? 0.0 : 1.0);
							}
							break;
						}

						// 2. Normalize so the strongest SEED reaches full intensity (territorial among seeds; self-entry excluded).
						double MaxSeed = 0;
						for (int32 i = 0; i < Count; i++)
						{
							if (Contributors[i].SeedPointIndex >= 0)
							{
								MaxSeed = FMath::Max(MaxSeed, Weights[i]);
							}
						}
						if (MaxSeed > 0)
						{
							const double InvMaxSeed = 1.0 / MaxSeed;
							for (int32 i = 0; i < Count; i++)
							{
								if (Contributors[i].SeedPointIndex >= 0)
								{
									Weights[i] *= InvMaxSeed;
								}
							}
						}
					}

					// 3. Seed Factor tilts seeds against the (fixed) original, and biases seed-vs-seed for per-seed factors.
					if (SeedFactor)
					{
						for (int32 i = 0; i < Count; i++)
						{
							const int32 SeedPt = Contributors[i].SeedPointIndex;
							if (SeedPt >= 0)
							{
								Weights[i] *= FMath::Max(0.0, SeedFactor->Read(SeedPt));
							}
						}
					}

					// 4. The original participates as a peer to the strongest seed (equal at Seed Factor 1).
					for (int32 i = 0; i < Count; i++)
					{
						if (Contributors[i].SeedPointIndex < 0)
						{
							Weights[i] = 1.0;
						}
					}

					// 5. Renormalize to max = 1 (preserves the seed/original ratio; orders dominant-first, as the multi-blend
					// copies the first as its base and only re-normalizes once the summed weight passes 1).
					double MaxWeight = 0;
					for (const double W : Weights)
					{
						MaxWeight = FMath::Max(MaxWeight, W);
					}
					if (MaxWeight > 0)
					{
						const double InvMax = 1.0 / MaxWeight;
						for (double& W : Weights)
						{
							W *= InvMax;
						}
					}
					// Dominant-first ordering (Relative only; Falloff walks contributors in natural order).
					Order.Reset(Count);
					for (int32 i = 0; i < Count; i++)
					{
						Order.Add(i);
					}
					Order.Sort([&Weights](const int32 A, const int32 B)
					{
						return Weights[A] > Weights[B];
					});
				}

				// Skip if no seed meaningfully reached this vtx (all weights ~0 -- e.g. Seed Factor 0, or grazed at a
				// diffusion edge): leave the original rather than let the reset-mode multi-blend wipe it to 0. Real weights
				// still apply the Falloff gradient, and the participation self-entry (weight ~1) stays above this threshold.
				double TotalWeight = 0;
				for (const double W : Weights)
				{
					TotalWeight += W;
				}
				if (TotalWeight < UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}

				// Emit both layers in one walk (Relative: dominant-first via Order; Falloff: natural order). Layer 1 = seed vtx
				// value (source 0); Layer 2 = seed cloud value (source 0), routing the self-entry to the vtx background (source 1).
				const bool bReorder = WeightSpace == EPCGExClusterDiffuseWeightSpace::Relative;
				if (This->VtxBlender)
				{
					VtxWeightedPoints.Reset(Count);
				}
				if (This->SeedBlender)
				{
					SeedWeightedPoints.Reset(Count);
				}

				for (int32 k = 0; k < Count; k++)
				{
					const int32 O = bReorder ? Order[k] : k;
					const FContribution& C = Contributors[O];
					const double W = Weights[O];

					if (This->VtxBlender)
					{
						VtxWeightedPoints.Emplace(C.SeedVtxIndex, W, 0);
					}
					if (This->SeedBlender)
					{
						if (C.SeedPointIndex < 0)
						{
							SeedWeightedPoints.Emplace(C.SeedVtxIndex, W, 1);
						}
						else
						{
							SeedWeightedPoints.Emplace(C.SeedPointIndex, W, 0);
						}
					}
				}

				if (This->VtxBlender)
				{
					This->VtxBlender->Blend(Index, VtxWeightedPoints, VtxTrackers);
				}
				if (This->SeedBlender && !SeedWeightedPoints.IsEmpty())
				{
					This->SeedBlender->Blend(Index, SeedWeightedPoints, SeedTrackers);
				}
			}
		};

		BlendDiffusions->StartSubLoops(NumNodes, PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);
	}

	void FProcessor::Cleanup()
	{
		// Base resets the growth state (diffusions + fill controls handler).
		TDiffusionGrowthProcessor<FPCGExClusterDiffuseContext, UPCGExClusterDiffuseSettings>::Cleanup();
		VtxBlender.Reset();
		SeedBlender.Reset();
		ContribOffsets.Empty();
		ContribEnds.Empty();
		Contributions.Empty();
	}

#pragma endregion

#pragma region FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffuse)

		PCGExBlending::RegisterBuffersDependencies(Context, FacadePreloader, Context->BlendingFactories);

		// Vtx-participation toggle may read from a vtx attribute -- preload it.
		Settings->VtxParticipates.RegisterBufferDependencies(Context, FacadePreloader);

		for (const TObjectPtr<const UPCGExFillControlsFactoryData>& Factory : Context->FillControlFactories)
		{
			Factory->RegisterBuffersDependencies(Context, FacadePreloader);
		}
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffuse)

		FillRate = Settings->Diffusion.FillRate.GetValueSetting();
		bIsBatchValid = FillRate->Init(Settings->Diffusion.FillRateSource == EPCGExFloodFillSettingSource::Seed ? Context->SeedsDataFacade : VtxDataFacade);

		if (!bIsBatchValid)
		{
			return;
		}

		// Build the blenders once here (single-threaded): creating them in the parallel per-processor CompleteWork would
		// allocate output buffers on the shared, batch-owned vtx facade from many threads. Layer 1 = vtx->vtx; Layer 2 = seeds (+ vtx background) -> vtx.
		if (!Context->BlendingFactories.IsEmpty())
		{
			TArray<TSharedRef<PCGExData::FFacade>> VtxSources;
			VtxSources.Add(VtxDataFacade);

			VtxBlender = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, nullptr);
			if (!VtxBlender->Init(Context, VtxDataFacade, VtxSources))
			{
				bIsBatchValid = false;
				return;
			}
		}

		if (!Context->SeedBlendingFactories.IsEmpty())
		{
			// Source 1 (vtx background) tolerates attributes the vtx lacks: seed-only attributes have no
			// original to fade to and fall toward 0, while attributes the vtx holds fade to their original.
			TArray<TSharedRef<PCGExData::FFacade>> SeedSources;
			SeedSources.Add(Context->SeedsDataFacade.ToSharedRef());
			SeedSources.Add(VtxDataFacade);

			SeedBlender = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->SeedBlendingFactories, nullptr);
			// Source 0 = seeds (primary, schema-resolved); source 1 = vtx (background: factories + tolerate-missing).
			static constexpr bool BackgroundFlags[] = {false, true};
			if (!SeedBlender->Init(Context, VtxDataFacade, SeedSources, Context->SeedBlendOpsSchema, BackgroundFlags))
			{
				bIsBatchValid = false;
				return;
			}
		}

		TBatch<FProcessor>::Process();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}

		PCGEX_TYPED_PROCESSOR

		TypedProcessor->FillRate = FillRate;
		TypedProcessor->VtxBlender = VtxBlender;
		TypedProcessor->SeedBlender = SeedBlender;

		return true;
	}

	void FBatch::Write()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ClusterDiffuse)

		TBatch<FProcessor>::Write();

		// Batch-scoped blender teardown (mirrors the batch-scoped creation), after every processor's blend
		// pass has completed and before the vtx facade is flushed.
		if (VtxBlender)
		{
			VtxBlender->Cleanup(Context);
		}
		if (SeedBlender)
		{
			SeedBlender->Cleanup(Context);
		}

		VtxDataFacade->WriteFastest(TaskManager);
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
