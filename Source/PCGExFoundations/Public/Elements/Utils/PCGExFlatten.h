// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGPin.h"
#include "Core/PCGExSettings.h"

#include "PCGExFlatten.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "Misc", meta=(PCGExNodeLibraryDoc="utilities/flatten"))
class UPCGExFlattenSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override
	{
		return FName(TEXT("PCGEx | Flatten"));
	}

	virtual FText GetDefaultNodeTitle() const override
	{
		return FTEXT("Flatten");
	}

	virtual FText GetNodeTooltipText() const override
	{
		return FTEXT("Outputs an owned, flattened copy of each input. Decouples the data from the graph that produced it, so it can be safely read from another component (e.g. via Wait For PCG Data / Get PCG Component Data) even while the producing graph is still running.");
	};

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Misc);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual bool ShouldDrawNodeCompact() const override
	{
		return true;
	}
#endif

protected:
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGExFlattenElement final : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return false;
	}

	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
