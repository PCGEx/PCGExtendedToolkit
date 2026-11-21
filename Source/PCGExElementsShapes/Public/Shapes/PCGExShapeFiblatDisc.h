// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Shapes/PCGExShapeFiblat.h"

#include "PCGExShapeFiblatDisc.generated.h"

/**
 *
 */
class FPCGExShapeFiblatDiscBuilder : public FPCGExShapeFiblatBuilderBase
{
public:
	virtual void PrepareShape(const PCGExData::FConstPoint& Seed) override;
	virtual void BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, bool bOwnsData = false) override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExShapeFiblatDiscFactory : public UPCGExShapeBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExShapeFiblatConfig Config;

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Builder|Params", meta=(PCGExNodeLibraryDoc="shapes/shape-f-disc"))
class UPCGExCreateShapeFiblatDiscSettings : public UPCGExShapeBuilderFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ShapeBuilderFiblatDisc, "Shape : φ Disc", "Create a Fibonacci Lattice disc.", FName(TEXT("φ Disc")))
#endif
	//~End UPCGSettings

	/** Shape properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExShapeFiblatConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
