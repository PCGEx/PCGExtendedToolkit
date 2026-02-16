// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExValencySolverOperation.h"
#include "Growth/PCGExConnectorConstraintResolver.h"
#include "PCGExValencyGenerativeCommon.h"

#include "PCGExValencyGrowthOperation.generated.h"

/**
 * Base class for Valency growth operations.
 * Subclass this and override SelectNextConnector() to create custom growth strategies.
 *
 * Growth operations expand structures from seed modules by:
 * 1. Selecting an open connector from the frontier
 * 2. Finding compatible modules for that connector
 * 3. Computing attachment transforms and checking bounds
 * 4. Placing modules or marking connectors as failed
 */
class PCGEXELEMENTSVALENCY_API FPCGExValencyGrowthOperation : public FPCGExOperation
{
public:
	FPCGExValencyGrowthOperation() = default;
	virtual ~FPCGExValencyGrowthOperation() override = default;

	/**
	 * Initialize the growth operation with rules and tracking state.
	 */
	virtual void Initialize(
		const FPCGExValencyBondingRulesCompiled* InCompiledRules,
		const UPCGExValencyConnectorSet* InConnectorSet,
		FPCGExBoundsTracker& InBoundsTracker,
		FPCGExGrowthBudget& InBudget,
		int32 InSeed);

	/**
	 * Run the full growth from seed modules.
	 * @param OutPlaced Output array of all placed modules (including seeds)
	 */
	virtual void Grow(TArray<FPCGExPlacedModule>& OutPlaced);

	/**
	 * Compute world bounds for a module at a given transform.
	 * Public so the element can compute seed bounds before growth starts.
	 */
	FBox ComputeWorldBounds(int32 ModuleIndex, const FTransform& WorldTransform) const;

	/** Cached local bounds per module (indexed by module index). Set by element before Grow(). */
	TArray<FBox> ModuleLocalBounds;

protected:
	/** Subclass interface: pick next connector from frontier. Return INDEX_NONE if empty. */
	virtual int32 SelectNextConnector(TArray<FPCGExOpenConnector>& Frontier) PCGEX_NOT_IMPLEMENTED_RET(SelectNextConnector, INDEX_NONE);

	// ========== Shared Utilities ==========

	/**
	 * Find all modules whose connectors are compatible with the given connector type and polarity.
	 * @param ConnectorType The connector type to find compatible modules for
	 * @param SourcePolarity Polarity of the source connector (checked against candidate polarities)
	 * @param OutModuleIndices Module indices that have a compatible connector
	 * @param OutConnectorIndices Corresponding connector indices on those modules
	 */
	void FindCompatibleModules(
		FName ConnectorType,
		EPCGExConnectorPolarity SourcePolarity,
		TArray<int32>& OutModuleIndices,
		TArray<int32>& OutConnectorIndices) const;

	/**
	 * Compute world transform for placing a child module attached at a connector.
	 * Starts from parent module rotation and applies a minimal correction to align the
	 * child's attachment connector opposite to the parent connector direction.
	 * Scale propagated from parent module.
	 */
	FTransform ComputeAttachmentTransform(
		const FPCGExOpenConnector& ParentConnector,
		const FTransform& ParentModuleWorld,
		int32 ChildModuleIndex,
		int32 ChildConnectorIndex) const;

	/**
	 * Try to place a module at a connector. Evaluates constraints to generate
	 * candidate transforms and tries each until one fits or all are exhausted.
	 */
	bool TryPlaceModule(
		const FPCGExOpenConnector& Connector,
		int32 ModuleIndex,
		int32 ChildConnectorIndex,
		TArray<FPCGExPlacedModule>& OutPlaced,
		TArray<FPCGExOpenConnector>& OutFrontier);

	/**
	 * Try to place a module at a specific pre-computed world transform.
	 * Checks bounds overlap and creates the placement entry.
	 */
	bool TryPlaceModuleAt(
		const FPCGExOpenConnector& Connector,
		int32 ModuleIndex,
		int32 ChildConnectorIndex,
		const FTransform& WorldTransform,
		TArray<FPCGExPlacedModule>& OutPlaced,
		TArray<FPCGExOpenConnector>& OutFrontier);

	/**
	 * Add a placed module's remaining connectors to the frontier.
	 */
	void ExpandFrontier(
		const FPCGExPlacedModule& Placed,
		int32 PlacedIndex,
		int32 UsedConnectorIndex,
		TArray<FPCGExOpenConnector>& OutFrontier);

	/**
	 * Weighted random module selection from candidates.
	 * @return Index into CandidateModules array (not module index)
	 */
	int32 SelectWeightedRandom(const TArray<int32>& CandidateModules);

	/**
	 * Pre-load all constraint preset assets and build the resolver's flatten cache.
	 * Called during Initialize() to avoid per-connector work during growth.
	 */
	void BuildConstraintCache();

	// ========== State ==========

	const FPCGExValencyBondingRulesCompiled* CompiledRules = nullptr;
	const UPCGExValencyConnectorSet* ConnectorSet = nullptr;
	FPCGExBoundsTracker* BoundsTracker = nullptr;
	FPCGExGrowthBudget* Budget = nullptr;
	FRandomStream RandomStream;
	PCGExValency::FDistributionTracker DistributionTracker;
	FPCGExConstraintResolver ConstraintResolver;
};

/**
 * Base factory for creating Valency growth operations.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType)
class PCGEXELEMENTSVALENCY_API UPCGExValencyGrowthFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExValencyGrowthOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation, nullptr);
};
