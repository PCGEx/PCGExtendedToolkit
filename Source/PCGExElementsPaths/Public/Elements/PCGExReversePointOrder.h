// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPointsProcessor.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/PCGExWinding.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExReversePointOrder.generated.h"

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExData
{
	struct FAttributeIdentity;
}

namespace PCGExData
{
	class IBuffer;
}

UENUM()
enum class EPCGExPointReverseMethod : uint8
{
	None         = 0 UMETA(DisplayName = "Unconditional", ToolTip="Always reverse."),
	SortingRules = 1 UMETA(DisplayName = "Sorting Rules", ToolTip="Compare the first and last point with the rules on the Sorting Rules pin, and reverse when the first already sorts ahead of the last."),
	Winding      = 2 UMETA(DisplayName = "Winding", ToolTip="Project the points to 2D and reverse only when the resulting winding differs from the one selected below."),
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSPATHS_API FPCGExSwapAttributePairDetails
{
	GENERATED_BODY()

	FPCGExSwapAttributePairDetails()
	{
	}

	/** First attribute in the swap pair. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName FirstAttributeName = NAME_None;
	PCGExData::FAttributeIdentity* FirstIdentity = nullptr;
	TSharedPtr<PCGExData::IBuffer> FirstWriter;

	/** Second attribute in the swap pair. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName SecondAttributeName = NAME_None;
	PCGExData::FAttributeIdentity* SecondIdentity = nullptr;
	TSharedPtr<PCGExData::IBuffer> SecondWriter;

	/** Multiply swapped numeric values by -1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMultiplyByMinusOne = false;

	bool Validate(const FPCGContext* InContext) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/sorting/reverse-order"))
class UPCGExReversePointOrderSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ReversePointOrder, "Reverse Order", "Simply reverse the order of points or change winding of paths.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Generic;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Method used to determine when to reverse point order. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPointReverseMethod Method = EPCGExPointReverseMethod::None;

	/** Sort direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Method == EPCGExPointReverseMethod::SortingRules", EditConditionHides))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	/** Winding */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Method == EPCGExPointReverseMethod::Winding", EditConditionHides))
	EPCGExWinding Winding = EPCGExWinding::CounterClockwise;

	/** Projection settings. Winding is computed on a 2D plane. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Method == EPCGExPointReverseMethod::Winding", EditConditionHides))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails();

	/** Attribute pairs to swap values between when reversing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	TArray<FPCGExSwapAttributePairDetails> SwapAttributesValues;

	/** Tag data that was reversed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfReversed = true;

	/** Tag to apply when points were reversed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfReversed"))
	FString IsReversedTag = TEXT("Reversed");

	/** Tag data that was not reversed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfNotReversed = false;

	/** Tag to apply when points were not reversed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfNotReversed"))
	FString IsNotReversedTag = TEXT("NotReversed");
};

struct FPCGExReversePointOrderContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExReversePointOrderElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExReversePointOrderElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ReversePointOrder)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExReversePointOrder
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExReversePointOrderContext, UPCGExReversePointOrderSettings>
	{
		TArray<FPCGExSwapAttributePairDetails> SwapPairs;
		TSharedPtr<PCGExSorting::FSorter> Sorter;

		FPCGExGeo2DProjectionDetails ProjectionDetails;

		bool bReversed = true;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};
}
