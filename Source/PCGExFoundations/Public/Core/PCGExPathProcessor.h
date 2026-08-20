// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPointsProcessor.h"

#include "PCGExPathProcessor.generated.h"

#define PCGEX_SKIP_INVALID_PATH_ENTRY if (Entry->GetNum() < 2){ if (!Settings->bOmitInvalidPathsOutputs) { Entry->InitializeOutput(PCGExData::EIOInit::Forward); } bHasInvalidInputs = true; return false; }

#define PCGEX_OUTPUT_VALID_PATHS(_COLLECTION) if (Settings->bOmitInvalidPathsOutputs) { Context->_COLLECTION->StageOutputs(2, MAX_int32); }else{ Context->_COLLECTION->StageOutputs(); }

class UPCGExPointFilterFactoryData;

class UPCGExNodeStateFactory;

namespace PCGExClusters
{
	class FNodeStateHandler;
}

/**
 * 
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/common-settings/path-processor-settings"))
class PCGEXFOUNDATIONS_API UPCGExPathProcessorSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExPathProcessorSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Path);
	}
#endif
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
	virtual FName GetMainInputPin() const override;
	virtual FName GetMainOutputPin() const override;

	virtual FString GetPointFilterTooltip() const override
	{
		return TEXT("Path points processing filters");
	}

	//~End UPCGExPointsProcessorSettings

	UPROPERTY()
	bool bSupportClosedLoops = true;

	/** If enabled, collections that have less than 2 points won't be processed and be omitted from the output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bOmitInvalidPathsOutputs = true;
};

struct PCGEXFOUNDATIONS_API FPCGExPathProcessorContext : FPCGExPointsProcessorContext
{
	friend class FPCGExPathProcessorElement;
	TSharedPtr<PCGExData::FPointIOCollection> MainPaths;
};

class PCGEXFOUNDATIONS_API FPCGExPathProcessorElement : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathProcessor)
	virtual bool Boot(FPCGExContext* InContext) const override;
};
