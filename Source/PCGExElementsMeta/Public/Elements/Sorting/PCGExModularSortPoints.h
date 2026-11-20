// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSortPoints.h"
#include "Core/PCGExPointsProcessor.h"

#include "PCGExModularSortPoints.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/sorting/sort-points"))
class UPCGExModularSortPointsSettings : public UPCGExSortPointsBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ModularSortPoints, "Sort Points", "Sort the source points according to specific rules.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const override;
};
