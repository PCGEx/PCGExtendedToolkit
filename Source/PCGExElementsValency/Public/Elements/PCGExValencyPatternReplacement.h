// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExValencyProcessor.h"
#include "Core/PCGExValencyPattern.h"
#include "Core/PCGExPatternMatcherOperation.h"

#include "PCGExValencyPatternReplacement.generated.h"

/**
 * Valency Pattern Replacement - Detects and transforms patterns in solved clusters.
 * Reads solved module indices and matches against compiled patterns from BondingRules.
 * Uses pluggable matchers to define matching behavior.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Valency", meta=(Keywords = "wfc wave function collapse valency pattern replacement", PCGExNodeLibraryDoc="valency/valency-patterns"))
class PCGEXELEMENTSVALENCY_API UPCGExValencyPatternReplacementSettings : public UPCGExValencyProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ValencyPatternReplacement, "Valency : Patterns", "Detects patterns in solved clusters and transforms matched points.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	// BondingRules and OrbitalSet are resolved from the Valency Map, not direct references
	virtual bool WantsOrbitalSet() const override { return false; }
	virtual bool WantsBondingRules() const override { return false; }
	virtual bool WantsValencyMap() const override { return true; }

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	/** If enabled, output matched points to a secondary pin (for Remove/Fork strategies) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bOutputMatchedPoints = true;

	/** Attribute name for the pattern name annotation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName PatternNameAttributeName = FName("PatternName");

	/** Attribute name for the pattern match index (which occurrence of the pattern this point belongs to) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName PatternMatchIndexAttributeName = FName("PatternMatchIndex");

	/** Suppress warnings about no patterns in bonding rules */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietNoPatterns = false;

	/** Suppress warnings about no matcher connected */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietNoMatcher = false;

	/** Pattern matcher to use for matching patterns. If none provided, uses default subgraph matcher. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = "Settings|Matching", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExPatternMatcherFactory> Matcher;
};

struct PCGEXELEMENTSVALENCY_API FPCGExValencyPatternReplacementContext final : FPCGExValencyProcessorContext
{
	friend class FPCGExValencyPatternReplacementElement;

	/** Compiled patterns (reference from BondingRules) */
	const FPCGExValencyPatternSetCompiled* CompiledPatterns = nullptr;

	/** Registered matcher factory (from Settings) */
	TObjectPtr<const UPCGExPatternMatcherFactory> MatcherFactory;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class PCGEXELEMENTSVALENCY_API FPCGExValencyPatternReplacementElement final : public FPCGExValencyProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ValencyPatternReplacement)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExValencyPatternReplacement
{
	class FBatch;

	class FProcessor final : public PCGExValencyMT::TProcessor<FPCGExValencyPatternReplacementContext, UPCGExValencyPatternReplacementSettings>
	{
		friend class FBatch;

	protected:
		/** ValencyEntry reader/writer (for pattern matching and flag propagation) */
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryReader;
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryWriter;

		/** Matcher operation (created from factory) */
		TSharedPtr<FPCGExPatternMatcherOperation> MatcherOperation;

		/** Matcher-specific allocations */
		TSharedPtr<PCGExPatternMatcher::FMatcherAllocations> MatcherAllocations;

		/** Claimed node indices (for exclusive patterns, shared across matchers) */
		TSet<int32> ClaimedNodes;

		/** Node indices to remove from main output (Remove/Fork/Collapse strategies) */
		TSet<int32> NodesToRemove;

		/** Collapse replacement transforms (NodeIdx -> ReplacementTransform) */
		TMap<int32, FTransform> CollapseReplacements;

		/** Swap target module indices (NodeIdx -> TargetModuleIdx) */
		TMap<int32, int32> SwapTargets;

		/** Annotated node indices (all nodes that matched a pattern) */
		TSet<int32> AnnotatedNodes;

		/** Pattern name writer (for annotation) */
		TSharedPtr<PCGExData::TBuffer<FName>> PatternNameWriter;

		/** Pattern match index writer */
		TSharedPtr<PCGExData::TBuffer<int32>> PatternMatchIndexWriter;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override = default;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void Write() override;

	protected:
		/** Run pattern matching (called from Process) */
		void RunMatching();

		/** Apply matches to output (topology-altering - to be moved to separate nodes) */
		void ApplyMatches();

		/** Compute replacement transform for Collapse mode */
		FTransform ComputeReplacementTransform(const FPCGExValencyPatternMatch& Match, const FPCGExValencyPatternSettingsCompiled& PatternSettings);
	};

	class FBatch final : public PCGExValencyMT::TBatch<FProcessor>
	{
		/** ValencyEntry reader/writer (owned here, shared with processors) */
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryReader;
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryWriter;

		/** Pattern name writer (owned here, shared with processors) */
		TSharedPtr<PCGExData::TBuffer<FName>> PatternNameWriter;

		/** Pattern match index writer (owned here, shared with processors) */
		TSharedPtr<PCGExData::TBuffer<int32>> PatternMatchIndexWriter;

		/** Matcher-specific allocations (created from factory, shared with processors) */
		TSharedPtr<PCGExPatternMatcher::FMatcherAllocations> MatcherAllocations;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);
		virtual ~FBatch() override;

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void CompleteWork() override;
	};
}
