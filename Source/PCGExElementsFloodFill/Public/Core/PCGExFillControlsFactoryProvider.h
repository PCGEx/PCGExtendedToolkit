// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFloodFill.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExFillControlsFactoryProvider.generated.h"

#define PCGEX_FORWARD_FILLCONTROL_FACTORY \
	NewFactory->Config = Config; \
	NewFactory->Config.Init(); \
	NewFactory->ConfigBase = NewFactory->Config;

#define PCGEX_FORWARD_FILLCONTROL_OPERATION \
	NewOperation->Factory = this;

class FPCGExFillControlOperation;

USTRUCT(BlueprintType)
struct PCGEXELEMENTSFLOODFILL_API FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigBase()
	{
	}

	~FPCGExFillControlConfigBase()
	{
	}

	UPROPERTY()
	bool bSupportSource = true;

	UPROPERTY()
	bool bSupportSteps = true;

	/** Where to fetch the attribute from. Note that this may not be supported by all controls..*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="bSupportSource", EditConditionHides, HideEditConditionToggle))
	EPCGExFloodFillSettingSource Source = EPCGExFloodFillSettingSource::Seed;

	/** At which diffusion step should this control be applied. Note that this may not be supported by all controls. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExElementsFloodFill.EPCGExFloodFillControlStepsFlags", EditCondition="bSupportSteps", EditConditionHides, HideEditConditionToggle))
	uint8 Steps = static_cast<uint8>(EPCGExFloodFillControlStepsFlags::Candidate);

	void Init();
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Fill Control"))
struct FPCGExDataTypeInfoFillControl : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXELEMENTSFLOODFILL_API)
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSFLOODFILL_API UPCGExFillControlsFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoFillControl)

	FPCGExFillControlConfigBase ConfigBase;


	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fill-control"))
class PCGEXELEMENTSFLOODFILL_API UPCGExFillControlsFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoFillControl)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AbstractFillControls, "Fill Controls Definition", "Creates a single Fill Control node, to be used with flood fill nodes.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(FillControl);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override
	{
		return PCGExFloodFill::OutputFillControlsLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
