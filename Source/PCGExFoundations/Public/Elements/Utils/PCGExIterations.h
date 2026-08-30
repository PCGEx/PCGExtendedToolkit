// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGSettings.h"
#include "Core/PCGExPointsProcessor.h"

#include "PCGExIterations.generated.h"

UENUM()
enum class EPCGExIterationDataType : uint8
{
	Any     = 0 UMETA(DisplayName = "Any", Tooltip="Output dummy iteration data of type Attribute set, using an untyped pin."),
	Params  = 1 UMETA(DisplayName = "Attribute Set", Tooltip="Output dummy iteration data of type Attribute set."),
	Points  = 2 UMETA(DisplayName = "Points", Tooltip="Output dummy iteration data of type Points."),
	Spline  = 3 UMETA(DisplayName = "Spline", Tooltip="Output dummy iteration data of type Spline."),
	Texture = 4 UMETA(DisplayName = "Texture", Tooltip="Output dummy iteration data of type Texture."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="utilities/control-flow/iterations"))
class UPCGExIterationsSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExIterationsElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Iterations, "Iterations", "Generates a chosen number of lightweight dummy data entries for a loop node to iterate over. A single dummy object is created once and duplicated in the node output, so each entry is one iteration.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	/** Type of data to generate. This is useful if you build subgraphs that are meant to be used as both loops and regular subgraphs, so you can have properly typed pins. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	EPCGExIterationDataType Type = EPCGExIterationDataType::Params;

	/** Number of dataset to generate */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	int32 Iterations = 0;

	/** Output per-iteration params with useful values. Less optimized than the non-value version */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Type = EPCGExIterationDataType::Params"))
	bool bOutputUtils = false;
};

class FPCGExIterationsElement final : public IPCGExElement
{
public:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT

protected:
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}
};
