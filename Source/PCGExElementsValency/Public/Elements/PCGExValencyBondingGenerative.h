// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyMap.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Growth/PCGExValencyGenerativeCommon.h"
#include "Growth/PCGExValencyGrowthOperation.h"

#include "PCGExValencyBondingGenerative.generated.h"

class UPCGExValencyGrowthFactory;

/**
 * Valency Bonding (Generative) - Grow structures from seed points using connector connections.
 * Seeds resolve to modules, modules expose connectors, connectors spawn new modules.
 * Outputs cluster data (vtx + edges) with orbital and connector attributes.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Valency", meta=(Keywords = "valency generative growth grow seed connector bonding", PCGExNodeLibraryDoc="valency/valency-generative"))
class PCGEXELEMENTSVALENCY_API UPCGExValencyBondingGenerativeSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject
	virtual void PostInitProperties() override;
	//~End UObject

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ValencyBondingGenerative, "Valency : Bonding (Generative)", "Grow structures from seed points using connector-based module connections. Outputs cluster data with orbital edges.");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(MiscAdd); }
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

protected:
	virtual FName GetMainOutputPin() const override { return PCGExClusters::Labels::OutputVerticesLabel; }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** The bonding rules data asset (required). Connector set is inferred from this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TSoftObjectPtr<UPCGExValencyBondingRules> BondingRules;

	/** Growth strategy algorithm */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExValencyGrowthFactory> GrowthStrategy;

	/** Growth budget controlling expansion limits */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExGrowthBudget Budget;

	/** Global bounds padding in world units (cm). Positive = gap between modules. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Bounds", meta=(PCG_Overridable))
	float BoundsInflation = 0.0f;

	/** Suffix for the ValencyEntry attribute name (e.g. "Main" -> "PCGEx/V/Entry/Main") */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName EntrySuffix = FName("Main");

	/** If enabled, applies module's local transform offset to output points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bApplyLocalTransforms = true;

	/** If enabled, output the resolved module name as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bOutputModuleName = false;

	/** Attribute name for the resolved module name */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="bOutputModuleName"))
	FName ModuleNameAttributeName = FName("ModuleName");

	/** If enabled, output tree depth as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bOutputDepth = true;

	/** Attribute name for growth depth */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="bOutputDepth"))
	FName DepthAttributeName = FName("Depth");

	/** If enabled, output seed index as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bOutputSeedIndex = false;

	/** Attribute name for seed index */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="bOutputSeedIndex"))
	FName SeedIndexAttributeName = FName("SeedIndex");

	/** If enabled, output seeds that couldn't be resolved to modules */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bOutputUnsolvableSeeds = false;

	/** Attribute on seed points for module name filtering (empty = no filtering) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seed Filtering", meta=(PCG_Overridable))
	FName SeedModuleNameAttribute;

	/** Attribute on seed points for tag-based filtering (empty = no filtering) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Seed Filtering", meta=(PCG_Overridable))
	FName SeedTagAttribute;

	/** Graph builder output configuration (edge position, solidification, cluster filtering) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Graph Output", meta=(PCG_Overridable))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Write orbital data to edges (mask on vtx, packed orbital indices on edges) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Edge Output", meta=(PCG_Overridable))
	bool bOutputOrbitalData = true;

	/** Write raw connector indices to edges (for connector-level pattern matching) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Edge Output", meta=(PCG_Overridable))
	bool bOutputConnectorData = true;
};

struct PCGEXELEMENTSVALENCY_API FPCGExValencyBondingGenerativeContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExValencyBondingGenerativeElement;

	virtual void RegisterAssetDependencies() override;

	/** Loaded bonding rules */
	TObjectPtr<UPCGExValencyBondingRules> BondingRules;

	/** Connector set (derived from BondingRules) */
	TObjectPtr<UPCGExValencyConnectorSet> ConnectorSet;

	/** Registered growth factory */
	UPCGExValencyGrowthFactory* GrowthFactory = nullptr;

	/** Valency packer for ValencyEntry hash writing */
	TSharedPtr<PCGExValency::FValencyPacker> ValencyPacker;

	/** Compiled bonding rules (cached after PostBoot) */
	const FPCGExValencyBondingRulesCompiled* CompiledRules = nullptr;

	/** Module local bounds (inflated) */
	TArray<FBox> ModuleLocalBounds;

	/** Name-to-module lookup for seed filtering */
	TMap<FName, TArray<int32>> NameToModules;

	/** Edge output collection for graph builder */
	TSharedPtr<PCGExData::FPointIOCollection> EdgesIO;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class PCGEXELEMENTSVALENCY_API FPCGExValencyBondingGenerativeElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ValencyBondingGenerative)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExValencyBondingGenerative
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExValencyBondingGenerativeContext, UPCGExValencyBondingGenerativeSettings>
	{
		/** Per-seed resolved module index (written in parallel during ProcessPoints) */
		TArray<int32> ResolvedModules;

		/** Name attribute reader for seed filtering */
		TSharedPtr<PCGExData::TBuffer<FName>> NameReader;

		/** Growth state (per input dataset) */
		TSharedPtr<FPCGExValencyGrowthOperation> GrowthOp;
		TArray<FPCGExPlacedModule> PlacedModules;

		/** Graph builder for cluster output */
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
		virtual void Output() override;
	};
}
