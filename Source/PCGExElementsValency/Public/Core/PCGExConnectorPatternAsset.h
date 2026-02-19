// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCGExValencyConnectorPattern.h"
#include "PCGExValencyConnectorSet.h"
#include "EdGraph/EdGraph.h"

#include "PCGExConnectorPatternAsset.generated.h"

/**
 * Authored connector type pair (user-facing, name-based).
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorTypePairAuthored
{
	GENERATED_BODY()

	/** Connector type name on the source (current) node */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName SourceType;

	/** Connector type name on the target (adjacent) node */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName TargetType;
};

/**
 * Authored adjacency definition (user-facing, name-based).
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternAdjacencyAuthored
{
	GENERATED_BODY()

	/** Index of the target entry in the pattern (0-based) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta=(ClampMin = 0))
	int32 TargetEntryIndex = 0;

	/** Connector type pairs that satisfy this adjacency (OR semantics) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FPCGExConnectorTypePairAuthored> TypePairs;
};

/**
 * Authored pattern entry (user-facing, name-based).
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternEntryAuthored
{
	GENERATED_BODY()

	/** Module names that can match this entry. Empty = any module (wildcard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FName> ModuleNames;

	/** If true, this entry is consumed by the pattern; if false, constraint-only */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bIsActive = true;

	/** Connector type names that must have NO edges (boundary constraint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Constraints")
	TArray<FName> BoundaryConnectorTypes;

	/** Connector type names that must have at least one edge (wildcard constraint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Constraints")
	TArray<FName> WildcardConnectorTypes;

	/** Adjacencies to other entries in this pattern */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FPCGExConnectorPatternAdjacencyAuthored> Adjacencies;
};

/**
 * Authored pattern (user-facing, name-based).
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternAuthored
{
	GENERATED_BODY()

	/** Pattern name for identification and filtering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName PatternName;

	/** Pattern entries (index 0 = root). Each entry represents a position in the pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FPCGExConnectorPatternEntryAuthored> Entries;

	/** Weight for probabilistic selection among competing patterns */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta=(ClampMin = "0.001"))
	float Weight = 1.0f;

	/** If true, matched points are claimed exclusively */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bExclusive = true;

	/** Minimum times this pattern must be matched (0 = no minimum) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta=(ClampMin = 0))
	int32 MinMatches = 0;

	/** Maximum times this pattern can be matched (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta=(ClampMin = -1))
	int32 MaxMatches = -1;

	/** Output strategy for matched points */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EPCGExPatternOutputStrategy OutputStrategy = EPCGExPatternOutputStrategy::Annotate;

	/** Transform computation mode for Collapse strategy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EPCGExPatternTransformMode TransformMode = EPCGExPatternTransformMode::Centroid;
};

/**
 * Data asset defining connector-type-aware patterns for matching.
 * Authored with name-based references, compiled to index-based at edit time.
 *
 * Patterns define subgraph topologies using connector type pairs:
 * - Each entry matches a module by name
 * - Adjacencies define connections via specific connector type pairs
 * - Boundary/wildcard constraints check connector type presence
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Valency | Connector Pattern")
class PCGEXELEMENTSVALENCY_API UPCGExConnectorPatternAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Connector set defining available connector types. Used to resolve names to indices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TObjectPtr<UPCGExValencyConnectorSet> ConnectorSet;

	/** Authored patterns (name-based, user-facing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta=(TitleProperty = "{PatternName}"))
	TArray<FPCGExConnectorPatternAuthored> Patterns;

	/**
	 * Compile authored patterns to runtime format.
	 * Resolves all type names to indices via ConnectorSet.
	 * @param OutErrors Optional array to receive compilation error messages
	 * @return True if compilation succeeded
	 */
	bool Compile(TArray<FText>* OutErrors = nullptr);

	/** Get the compiled patterns (const reference) */
	const FPCGExConnectorPatternSetCompiled& GetCompiledPatterns() const { return CompiledPatterns; }

	/** Check if patterns have been compiled */
	bool IsCompiled() const { return CompiledPatterns.HasPatterns(); }

#if WITH_EDITORONLY_DATA
	/** Graph used by the visual pattern editor. Persisted with the asset. */
	UPROPERTY()
	TObjectPtr<UEdGraph> PatternGraph;
#endif

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostLoad() override;

protected:
	/** Serialized compiled data (rebuilt on edit, loaded on PostLoad) */
	UPROPERTY()
	FPCGExConnectorPatternSetCompiled CompiledPatterns;
};
