// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExShapesCommon.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExShapeBuilderFactoryProvider.generated.h"

#define PCGEX_SHAPE_BUILDER_BOILERPLATE(_SHAPE) \
TSharedPtr<FPCGExShapeBuilderOperation> UPCGExShape##_SHAPE##Factory::CreateOperation(FPCGExContext* InContext) const{ \
	PCGEX_FACTORY_NEW_OPERATION(Shape##_SHAPE##Builder)\
	NewOperation->Config = Config; \
	NewOperation->Config.Init(); \
	NewOperation->BaseConfig = NewOperation->Config; \
	NewOperation->Transform = NewOperation->Config.LocalTransform; \
	return NewOperation; } \
UPCGExFactoryData* UPCGExCreateShape##_SHAPE##Settings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{ \
	UPCGExShape##_SHAPE##Factory* NewFactory = InContext->ManagedObjects->New<UPCGExShape##_SHAPE##Factory>(); \
	NewFactory->Config = Config; \
	return Super::CreateFactory(InContext, NewFactory);}

struct FPCGExShapeConfigBase;
class FPCGExShapeBuilderOperation;

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Shape"))
struct FPCGExDataTypeInfoShape : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXELEMENTSSHAPES_API)
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXELEMENTSSHAPES_API UPCGExShapeBuilderFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoShape)

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="shapes/shape"))
class PCGEXELEMENTSSHAPES_API UPCGExShapeBuilderFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoShape)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;

	PCGEX_NODE_INFOS(ShapeBuilder, "ShapeBuilder Definition", "Creates a single shape builder node, to be used with a Shape processor node.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Shape);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override
	{
		return PCGExShapes::Labels::OutputShapeBuilderLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
