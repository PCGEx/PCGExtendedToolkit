// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Math/PCGExMath.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExPathShift.generated.h"

namespace PCGExData
{
	class IBuffer;
}

UENUM()
enum class EPCGExShiftType : uint8
{
	Index                 = 0 UMETA(DisplayName = "Index", ToolTip="..."),
	Metadata              = 1 UMETA(DisplayName = "Metadata", ToolTip="..."),
	Properties            = 2 UMETA(DisplayName = "Properties", ToolTip="..."),
	MetadataAndProperties = 3 UMETA(DisplayName = "Metadata and Properties", ToolTip="..."),
	CherryPick            = 4 UMETA(DisplayName = "CherryPick", ToolTip="...")
};

UENUM()
enum class EPCGExShiftPathMode : uint8
{
	Discrete = 0 UMETA(DisplayName = "Discrete", ToolTip="Shift point is selected using a discrete value"),
	Relative = 1 UMETA(DisplayName = "Relative", ToolTip="Shift point is selected using a value relative to the input size"),
	Filter   = 2 UMETA(DisplayName = "Filter", ToolTip="Shift point using the first point that passes the provided filters"),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-shift"))
class UPCGExShiftPathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExShiftPathSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UObject interface
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathShift, "Path : Shift", "Shift path points");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(InputMode == EPCGExShiftPathMode::Filter ? PCGExPaths::Labels::SourceShiftFilters : NAME_None, "Filters used to find the shift starting point.", PCGExFactories::PointFilters(), InputMode == EPCGExShiftPathMode::Filter)
	//~End UPCGExPointsProcessorSettings

	/** What data is shifted (index, metadata, properties, etc.). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExShiftType ShiftType = EPCGExShiftType::MetadataAndProperties;

	/** How the shift amount is determined. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExShiftPathMode InputMode = EPCGExShiftPathMode::Relative;

	/** Relative shift amount as a fraction of path length (0-1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InputMode == EPCGExShiftPathMode::Relative", EditConditionHides))
	double RelativeConstant = 0.5;

	/** How to truncate the computed index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InputMode == EPCGExShiftPathMode::Relative", EditConditionHides))
	EPCGExTruncateMode Truncate = EPCGExTruncateMode::Round;

	/** Number of positions to shift by. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InputMode == EPCGExShiftPathMode::Discrete", EditConditionHides))
	int32 DiscreteConstant = 0;

	/** How to handle out-of-range indices after shift. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InputMode != EPCGExShiftPathMode::Filter", EditConditionHides))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Tile;

	/** Reverse shift order */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bReverseShift = false;

	/** Point properties to be shifted */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="ShiftType == EPCGExShiftType::CherryPick", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExPointNativeProperties"))
	uint8 CherryPickedProperties = 0;

	/** Attributes to be shifted */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="ShiftType == EPCGExShiftType::CherryPick", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExPointNativeProperties"))
	TArray<FName> CherryPickedAttributes;

	/** Suppress warning when path data has already been shifted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietDoubleShiftWarning = false;
};

struct FPCGExShiftPathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExShiftPathElement;
	FPCGExBlendingDetails BlendingSettings;
	EPCGPointNativeProperties ShiftedProperties;
	TArray<FPCGAttributeIdentifier> ShiftedAttributes;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExShiftPathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ShiftPath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExShiftPath
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExShiftPathContext, UPCGExShiftPathSettings>
	{
		int32 MaxIndex = 0;
		int32 PivotIndex = -1;

		TArray<int32> Indices;

		TArray<TSharedPtr<PCGExData::IBuffer>> Buffers;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};
}
