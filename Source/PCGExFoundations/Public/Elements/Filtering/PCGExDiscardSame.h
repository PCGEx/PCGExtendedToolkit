// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExAttributeHasher.h"

#include "PCGExDiscardSame.generated.h"

namespace PCGEx
{
	class FAttributeHasher;
}

UENUM()
enum class EPCGExDiscardSameMode : uint8
{
	FIFO = 0 UMETA(DisplayName = "FIFO", ToolTip="First in, first out"),
	LIFO = 1 UMETA(DisplayName = "LIFO", ToolTip="Last in, first out"),
	All  = 2 UMETA(DisplayName = "All", ToolTip="Discard all collections that have found duplicates (does not keep any)"),
};

UENUM()
enum class EPCGExDiscardAttributeHashMode : uint8
{
	None   = 0 UMETA(DisplayName = "None", ToolTip="Do not use attributes to check sameness"),
	Single = 1 UMETA(DisplayName = "Single", ToolTip="Use a single, overridable attribute."),
	List   = 2 UMETA(DisplayName = "List", ToolTip="Use a list of attributes. Arrays are not overridable"),
};

namespace PCGExDiscardSame
{
	class FProcessor;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/discarding/discard-same"))
class UPCGExDiscardSameSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(DiscardSame, "Discard Same", "Discard entire datasets based on a selection of parameters");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterHub);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Which duplicate to keep when matches are found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDiscardSameMode Mode = EPCGExDiscardSameMode::FIFO;

	/** How to combine multiple test conditions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFilterGroupMode TestMode = EPCGExFilterGroupMode::AND;


	/** Test collection bounds for equality. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bTestBounds = false;

	/** Test collection bounds equality, within tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bTestBounds", ClampMin = 0.000001))
	double TestBoundsTolerance = 0.1;


	/** Test collection point count for equality. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bTestPointCount = true;

	/** Test collection point count equality, within tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bTestPointCount", ClampMin = 0))
	int32 TestPointCountTolerance = 0;


	/** Test point positions for equality. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bTestPositions = true;

	/** Test point positions equality, within tolerance. It computes space occupation, and does not account for point count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bTestPositions", ClampMin = 0.000001))
	double TestPositionTolerance = 0.1;

#pragma region DEPRECATED

	/**  */
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bTestAttributeHash_DEPRECATED = false;

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExDiscardAttributeHashMode TestAttributesHash = EPCGExDiscardAttributeHashMode::None;

	/** Build a hash from a single attribute and test it against the others. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TestAttributesHash == EPCGExDiscardAttributeHashMode::List", EditConditionHides))
	TArray<FPCGExAttributeHashConfig> AttributeHashConfigs;

	/** Also include the single overridable attribute config below. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Append Overridable", EditCondition="TestAttributesHash == EPCGExDiscardAttributeHashMode::List", EditConditionHides))
	bool bIncludeSingleAttribute = false;

	/** Build a hash from a single attribute and test it against the others. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TestAttributesHash == EPCGExDiscardAttributeHashMode::Single || (TestAttributesHash == EPCGExDiscardAttributeHashMode::List && bIncludeSingleAttribute)", EditConditionHides))
	FPCGExAttributeHashConfig AttributeHashConfig;

private:
	friend class FPCGExDiscardSameElement;
};

struct FPCGExDiscardSameContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExDiscardSameElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExDiscardSameElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(DiscardSame)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExDiscardSame
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExDiscardSameContext, UPCGExDiscardSameSettings>
	{
		friend struct FPCGExDiscardSameContext;

		TArray<TSharedPtr<PCGEx::FAttributeHasher>> Hashers;

	public:
		double HashPointsCount = 0;
		uint32 HashBounds = 0;
		uint32 HashPositions = 0;

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		bool CompareHashers(const TArray<TSharedPtr<PCGEx::FAttributeHasher>>& InHashers);

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
