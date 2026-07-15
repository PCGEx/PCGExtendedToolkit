// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBoxFittingRelax.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Details/PCGExSettingsMacros.h"

#include "PCGExRadiusFittingRelax.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Radius Fitting", PCGExNodeLibraryDoc="clusters/relax-cluster/radius-fitting"))
class UPCGExRadiusFittingRelax : public UPCGExFittingRelaxBase
{
	GENERATED_BODY()

public:
	UPCGExRadiusFittingRelax(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		RadiusAttribute_DEPRECATED.Update(TEXT("$Extents.Length"));
	}

	virtual void RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

	/** Radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Radius"))
	FPCGExInputShorthandSelectorDouble RadiusValue = FPCGExInputShorthandSelectorDouble(FString(TEXT("$Extents.Length")), 100, true);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType RadiusInput_DEPRECATED = EPCGExInputValueType::Attribute;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector RadiusAttribute_DEPRECATED;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Radius_DEPRECATED = 100;

#pragma endregion

#if WITH_EDITOR
	virtual void ApplyShorthandDeprecation() override;
#endif

	virtual bool PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster) override;
	virtual void Step2(const PCGExClusters::FNode& Node) override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<double>> RadiusBuffer;
};
