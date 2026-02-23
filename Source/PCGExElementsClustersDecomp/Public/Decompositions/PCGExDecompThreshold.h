// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompThreshold.generated.h"

UENUM()
enum class EPCGExDecompBinningMode : uint8
{
	Uniform  = 0 UMETA(DisplayName = "Uniform", ToolTip="Equal-width bins across the value range."),
	Quantile = 1 UMETA(DisplayName = "Quantile", ToolTip="Equal-count bins (each bin has roughly the same number of nodes)."),
};

/**
 * Threshold decomposition operation.
 * Reads a numeric attribute and assigns CellIDs by value ranges.
 */
class FPCGExDecompThreshold : public FPCGExDecompositionOperation
{
public:
	FName AttributeName = NAME_None;
	int32 NumBins = 4;
	EPCGExDecompBinningMode BinningMode = EPCGExDecompBinningMode::Uniform;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;
};

/**
 * Factory for Threshold decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Threshold"))
class UPCGExDecompThreshold : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	/** The numeric attribute to read values from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector AttributeSelector;

	/** Number of bins to create. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="2"))
	int32 NumBins = 4;

	/** Binning strategy. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDecompBinningMode BinningMode = EPCGExDecompBinningMode::Uniform;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) override;
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompThreshold, {
		Operation->AttributeName = AttributeSelector.GetName();
		Operation->NumBins = NumBins;
		Operation->BinningMode = BinningMode;
	})
};
