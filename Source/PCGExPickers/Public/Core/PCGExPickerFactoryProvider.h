// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExPickersCommon.h"
#include "Factories/PCGExFactoryData.h"
#include "Math/PCGExMath.h"

#include "PCGExPickerFactoryProvider.generated.h"

class UPCGExPickerOperation;

#define PCGEX_PICKER_BOILERPLATE(_PICKER, _NEW_FACTORY, _NEW_OPERATION) \
UPCGExFactoryData* UPCGExPicker##_PICKER##Settings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{ \
UPCGExPicker##_PICKER##Factory* NewFactory = InContext->ManagedObjects->New<UPCGExPicker##_PICKER##Factory>(); \
NewFactory->Config = Config; \
NewFactory->Config.Sanitize(); \
Super::CreateFactory(InContext, NewFactory); /* Super factory to grab custom override settings before body */ \
_NEW_FACTORY \
return NewFactory; }

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Picker"))
struct FPCGExDataTypeInfoPicker : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXPICKERS_API)
};


USTRUCT(BlueprintType)
struct PCGEXPICKERS_API FPCGExPickerConfigBase
{
	GENERATED_BODY()

	FPCGExPickerConfigBase() = default;
	virtual ~FPCGExPickerConfigBase() = default;

	/** Whether to treat values as discrete indices or relative ones */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bTreatAsNormalized = false;

	/** How to truncate relative picks */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bTreatAsNormalized", EditConditionHides))
	EPCGExTruncateMode TruncateMode = EPCGExTruncateMode::Round;

	/** How to sanitize index pick when they're out-of-bounds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety Safety = EPCGExIndexSafety::Ignore;


	virtual void Sanitize()
	{
	}

	virtual void Init()
	{
	}
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXPICKERS_API UPCGExPickerFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

	friend class UPCGExPickerFactoryProviderSettings;

	// TODO : To favor re-usability Picker factories hold more complex logic than regular factories
	// They are also samplers
	// We leverage internal point data and pack all needed attributes & computed points inside

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoPicker)

	UPROPERTY()
	TArray<int32> DiscretePicks;

	UPROPERTY()
	TArray<double> RelativePicks;

	virtual void AddPicks(int32 InNum, TSet<int32>& OutPicks) const;

	FPCGExPickerConfigBase BaseConfig;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

protected:
	virtual bool RequiresInputs() const;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return false;
	}

	virtual PCGExFactories::EPreparationResult InitInternalData(FPCGExContext* InContext);
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class PCGEXPICKERS_API UPCGExPickerFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoPicker)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Picker, "Picker Definition", "Creates a single Picker definition.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Misc);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExPickers::Labels::OutputPickerLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};

namespace PCGExPickers
{
	PCGEXPICKERS_API
	bool GetPicks(const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& Factories, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<int32>& OutPicks);
}
