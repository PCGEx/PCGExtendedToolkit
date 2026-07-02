// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactoryProvider.h"

#include "Factories/PCGExFactoryData.h"

#include "PCGExProbeFactoryProvider.generated.h"

#define PCGEX_CREATE_PROBE_FACTORY(_NAME, _EXTRA_FACTORY, _EXTRA_OPERATION) \
UPCGExFactoryData* UPCGExProbe##_NAME##ProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{\
	UPCGExProbeFactory##_NAME* NewFactory = InContext->ManagedObjects->New<UPCGExProbeFactory##_NAME>();\
	NewFactory->Config = Config; _EXTRA_FACTORY \
	return Super::CreateFactory(InContext, NewFactory); } \
TSharedPtr<FPCGExProbeOperation> UPCGExProbeFactory##_NAME::CreateOperation(FPCGExContext* InContext) const{\
	PCGEX_FACTORY_NEW_OPERATION(Probe##_NAME)\
	NewOperation->Config = Config; NewOperation->BaseConfig = &NewOperation->Config; _EXTRA_OPERATION return NewOperation;}

class FPCGExProbeOperation;

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Probe"))
struct FPCGExDataTypeInfoProbe : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXELEMENTSPROBING_API)
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSPROBING_API UPCGExProbeFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoProbe)

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class PCGEXELEMENTSPROBING_API UPCGExProbeFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoProbe)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AbstractProbe, "Probe Definition", "Creates a single probe to look for a nearby connection.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Probe);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override;
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
