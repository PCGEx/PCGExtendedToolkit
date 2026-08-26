// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExCompareShorthandsDetails.h"
#include "UObject/Object.h"

#include "PCGExDataBoundsFilter.generated.h"

UENUM(BlueprintType)
enum class EPCGExDataBoundsAspect : uint8
{
	Extents     = 0 UMETA(DisplayName = "Extents", ToolTip="Bound's Extents", ActionIcon="Bounds_Extents"),
	Min         = 1 UMETA(DisplayName = "Min", ToolTip="Bound's Min", ActionIcon="Bounds_Min"),
	Max         = 2 UMETA(DisplayName = "Max", ToolTip="Bound's Max", ActionIcon="Bounds_Max"),
	Size        = 3 UMETA(DisplayName = "Size", ToolTip="Bound's Size", ActionIcon="Bounds_Size"),
	Volume      = 4 UMETA(DisplayName = "Volume", ToolTip="Bound's Volume", ActionIcon="Bounds_Volume"),
	AspectRatio = 5 UMETA(DisplayName = "Ratio", ToolTip="Bound's Size Ratio", ActionIcon="Bounds_Ratio"),
	SortedRatio = 6 UMETA(DisplayName = "Ratio (Sorted)", ToolTip="Bound's Size Ratio (Max/Min axis)", ActionIcon="Bounds_Ratio"),
};

UENUM(BlueprintType)
enum class EPCGExDataBoundsComponent : uint8
{
	Length        = 0 UMETA(DisplayName = "Length"),
	LengthSquared = 1 UMETA(DisplayName = "Length Squared"),
	X             = 2 UMETA(DisplayName = "X"),
	Y             = 3 UMETA(DisplayName = "Y"),
	Z             = 4 UMETA(DisplayName = "Z")
};

UENUM(BlueprintType)
enum class EPCGExDataBoundsRatio : uint8
{
	XY = 0 UMETA(DisplayName = "XY", ToolTip="Size.X / Size.Y"),
	XZ = 1 UMETA(DisplayName = "XZ", ToolTip="Size.X / Size.Z"),
	YZ = 2 UMETA(DisplayName = "YZ", ToolTip="Size.Y / Size.Z"),
	YX = 3 UMETA(DisplayName = "YX", ToolTip="Size.Y / Size.X"),
	ZX = 4 UMETA(DisplayName = "ZX", ToolTip="Size.Z / Size.X"),
	ZY = 5 UMETA(DisplayName = "ZY", ToolTip="Size.Z / Size.Y")
};

USTRUCT(BlueprintType)
struct FPCGExDataBoundsFilterConfig
{
	GENERATED_BODY()

	FPCGExDataBoundsFilterConfig()
	{
	}

	/** Operand A */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExDataBoundsAspect OperandA = EPCGExDataBoundsAspect::Volume;

	/** Sub Operand */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Sub Operand", EditCondition="OperandA == EPCGExDataBoundsAspect::Extents || OperandA == EPCGExDataBoundsAspect::Min || OperandA == EPCGExDataBoundsAspect::Max || OperandA == EPCGExDataBoundsAspect::Size", EditConditionHides))
	EPCGExDataBoundsComponent SubOperand = EPCGExDataBoundsComponent::Length;

	/** Ratio */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Ratio", EditCondition="OperandA == EPCGExDataBoundsAspect::AspectRatio", EditConditionHides))
	EPCGExDataBoundsRatio Ratio = EPCGExDataBoundsRatio::XY;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExCompareSelectorDouble OperandB;

	/** Invert the result of this filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bInvert = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExDataBoundsFilterFactory : public UPCGExFilterCollectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExDataBoundsFilterConfig Config;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FDataBoundsFilter final : public ICollectionFilter
	{
	public:
		explicit FDataBoundsFilter(const TObjectPtr<const UPCGExDataBoundsFilterFactory>& InDefinition)
			: ICollectionFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExDataBoundsFilterFactory> TypedFilterFactory;

		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FDataBoundsFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/data-filters/data-filter-bounds"))
class UPCGExDataBoundsFilterProviderSettings : public UPCGExFilterCollectionProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(DataBoundsFilterFactory, "Data Filter : Bounds", "Test an aspect of the collection' bounds", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExDataBoundsFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
