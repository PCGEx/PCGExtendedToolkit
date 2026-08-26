// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPickerConstantRange.h"
#include "Core/PCGExPickerFactoryProvider.h"

#include "PCGExPickerAttributeSetRanges.generated.h"

USTRUCT(BlueprintType)
struct FPCGExPickerAttributeSetRangesConfig : public FPCGExPickerConfigBase
{
	GENERATED_BODY()

	FPCGExPickerAttributeSetRangesConfig()
		: FPCGExPickerConfigBase()
	{
	}

	/** List of attributes to read ranges of indices from FVector2. Use negative values to select from the end. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TArray<FPCGAttributePropertyInputSelector> Attributes;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="utilities/pickers/picker-ranges-from-set"))
class UPCGExPickerAttributeSetRangesFactory : public UPCGExPickerFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExPickerAttributeSetRangesConfig Config;
	TArray<FPCGExPickerConstantRangeConfig> Ranges;

	PCGEXPICKERS_API
	/** bQuiet suppresses the missing-data log so callers can honor their own fallback policy. */
	static bool GetUniqueRanges(FPCGExContext* InContext, FName InPinLabel, const FPCGExPickerAttributeSetRangesConfig& InConfig, TArray<FPCGExPickerConstantRangeConfig>& OutRanges, const bool bQuiet = false);

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual void AddPicks(int32 InNum, TSet<int32>& OutPicks) const override;

protected:
	virtual PCGExFactories::EPreparationResult InitInternalData(FPCGExContext* InContext) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Pickers|Params")
class UPCGExPickerAttributeSetRangesSettings : public UPCGExPickerFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PickerConstantSet, "Picker : Ranges from Set", "A Picker that accept lists of ranges in the form of FVector2, read from one of more attribute. Note that if no attribute is set in the details, it will use the first available one.")

#endif
	//~End UPCGSettings

	/** Picker properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPickerAttributeSetRangesConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
