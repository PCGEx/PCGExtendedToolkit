// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExShape.h"
#include "Core/PCGExShapeBuilderFactoryProvider.h"
#include "Core/PCGExShapeBuilderOperation.h"
#include "Details/PCGExClampDetails.h"
#include "Math/PCGExMath.h"

#include "PCGExShapeGrid.generated.h"

USTRUCT(BlueprintType)
struct FPCGExShapeGridConfig : public FPCGExShapeConfigBase
{
	GENERATED_BODY()

	FPCGExShapeGridConfig()
		: FPCGExShapeConfigBase(true)
	{
	}

	/** Adjust extents so they fill the selected axis. Only applies when BoundsSource is Fit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta=(PCG_NotOverridable, EditCondition="BoundsSource == EPCGExShapeBoundsSource::Fit", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 AdjustFit = 7;

	/** How */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_NotOverridable, DisplayName="X - Round", EditCondition="ResolutionMode == EPCGExResolutionMode::Distance", EditConditionHides))
	EPCGExTruncateMode TruncateX = EPCGExTruncateMode::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_Overridable, DisplayName="X - Clamp Count"))
	FPCGExClampDetails AxisClampDetailsX;

	/** Rounding mode for Y-axis cell count when using distance-based resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_NotOverridable, DisplayName="Y - Round", EditCondition="ResolutionMode == EPCGExResolutionMode::Distance", EditConditionHides))
	EPCGExTruncateMode TruncateY = EPCGExTruncateMode::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_Overridable, DisplayName="Y - Clamp Count"))
	FPCGExClampDetails AxisClampDetailsY;

	/** Rounding mode for Z-axis cell count when using distance-based resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_NotOverridable, DisplayName="Z - Round", EditCondition="ResolutionMode == EPCGExResolutionMode::Distance", EditConditionHides))
	EPCGExTruncateMode TruncateZ = EPCGExTruncateMode::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Resolution", meta = (PCG_Overridable, DisplayName="Z - Clamp Count"))
	FPCGExClampDetails AxisClampDetailsZ;
};

namespace PCGExShapes
{
	class FGrid : public FShape
	{
	public:
		FIntVector3 Count = FIntVector3(5);
		FVector Extents = FVector(10.0);
		FVector Offset = FVector(0);

		explicit FGrid(const PCGExData::FConstPoint& InPointRef)
			: FShape(InPointRef)
		{
		}
	};
}

/**
 * 
 */
class FPCGExShapeGridBuilder : public FPCGExShapeBuilderOperation
{
public:
	FPCGExShapeGridConfig Config;

	virtual bool PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade) override;
	virtual void PrepareShape(const PCGExData::FConstPoint& Seed) override;
	virtual void BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, bool bOwnsData = false) override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExShapeGridFactory : public UPCGExShapeBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExShapeGridConfig Config;

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Builder|Params", meta=(PCGExNodeLibraryDoc="shapes/shape-3d-grid"))
class UPCGExCreateShapeGridSettings : public UPCGExShapeBuilderFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ShapeBuilderGrid, "Shape : 3D Grid", "Create points in a 3D grid shape.", FName("Grid"))

#endif
	//~End UPCGSettings

	/** Shape properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExShapeGridConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
