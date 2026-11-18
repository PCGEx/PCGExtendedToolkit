// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Math/PCGExMath.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExCopyAttributesByIndex.generated.h"

namespace PCGExData
{
	class FDataForwardHandler;
	class FMultiFacadePreloader;
	class FTags;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "copy attributes index restore fetch lookup gather", PCGExNodeLibraryDoc="metadata/modify/copy-attributes-by-index"))
class UPCGExCopyAttributesByIndexSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExCopyAttributesByIndexSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(CopyAttributesByIndex, "Copy Attributes by Index", "Copy attributes from Sources onto each point, reading on the point the index of the Sources point to copy from.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	virtual bool HasDynamicPins() const override
	{
		return true;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/** Attribute holding, per point, the index of the Sources point to copy from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector IndexAttribute;

	/** How an index outside the paired Sources data is resolved. Ignored points keep their own values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Ignore;

	/** Which Sources attributes are copied, @Data ones included. The index attribute never is. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNameFiltersDetails Attributes;

	/** Also copy the tags of the paired Sources data. A tag the data already carries is never replaced. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bCopyTags = true;

	/** Which Sources tags are copied. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bCopyTags"))
	FPCGExNameFiltersDetails Tags;
};

struct FPCGExCopyAttributesByIndexContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExCopyAttributesByIndexElement;

	/** Aligned with the Sources pin entries; null where an entry holds no points. */
	TArray<TSharedPtr<PCGExData::FFacade>> SourceFacades;

	/** Reads the forwarded Sources attributes, one task each, before any processor builds its handler. */
	TSharedPtr<PCGExData::FMultiFacadePreloader> SourcesPreloader;

	/** Attribute filter as forward details. */
	FPCGExForwardDetails ForwardDetails = FPCGExForwardDetails(true);
	FPCGExNameFiltersDetails TagFilters;

	/** Aligned with the main pin entries, Steal Data only: false where an in-place write would show through Sources. */
	TBitArray<> StealableInputs;

	/** Sources data paired with the input at that pin position: same position, or the only Sources data. */
	TSharedPtr<PCGExData::FFacade> GetSourceFacade(const int32 InputIndex) const;

	/** Whether the input at that pin position is written in place instead of duplicated. */
	bool CanStealInput(const int32 InputIndex) const;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExCopyAttributesByIndexElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CopyAttributesByIndex)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExCopyAttributesByIndex
{
	/** Adds to To the tags of From that pass the filter and whose key To does not carry yet. */
	void CopyMissingTags(const PCGExData::FTags& From, PCGExData::FTags& To, const FPCGExNameFiltersDetails& Filter);

	/** True when that safety maps every index to a valid row, so no point is ever left untouched. */
	bool ResolvesEveryIndex(const EPCGExIndexSafety Safety);

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExCopyAttributesByIndexContext, UPCGExCopyAttributesByIndexSettings>
	{
		int32 MaxSourceIndex = -1;

		/** The index reader's input array; null for a single-value (@Data) index, whose row is resolved once in Process. */
		const int32* IndexData = nullptr;

		/** Per point, the Sources row to copy at scope end; -1 when the index is ignored. */
		TArray<int32> SourceRows;
		TSharedPtr<PCGExData::FDataForwardHandler> Forward;

		template <EPCGExIndexSafety Safety>
		void ResolveRows(const PCGExMT::FScope& Scope);

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
