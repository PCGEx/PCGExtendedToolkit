// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Utils/PCGPointOctree.h"


#include "PCGExCollocationCount.generated.h"

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/analyze/collocation-count"))
class UPCGExCollocationCountSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(CollocationCount, "Collocation Count", "Write the number of time a point shares its location with another.", CollicationNumAttributeName);

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** The name of the attribute to write collocation to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName CollicationNumAttributeName = "NumCollocations";

	/** Write the linear index of occurrence for collocated points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLinearOccurences = false;

	/** The name of the attribute to write linear occurences to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteLinearOccurences"))
	FName LinearOccurencesAttributeName = "NumLinearOccurences";

	/** Distance tolerance for considering two points as collocated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.01))
	double Tolerance = DBL_COLLOCATION_TOLERANCE;
};

struct FPCGExCollocationCountContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExCollocationCountElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExCollocationCountElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CollocationCount)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExCollocationCount
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExCollocationCountContext, UPCGExCollocationCountSettings>
	{
		double ToleranceConstant = DBL_COLLOCATION_TOLERANCE;
		TSharedPtr<PCGExData::TBuffer<int32>> CollocationWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> LinearOccurencesWriter;

		const PCGPointOctree::FPointOctree* Octree = nullptr;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};
}
