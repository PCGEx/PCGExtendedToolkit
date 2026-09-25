// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/PCGExMathContrast.h"

#include "PCGExWeightedLookup.generated.h"

namespace PCGExData
{
	class IBuffer;
	class FDataForwardHandler;

	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExWeightedLookupWeightsSource : uint8
{
	Self     = 0 UMETA(DisplayName = "Self", ToolTip="Weights are read from the processed points; the Map key column names the point attribute holding each row's weight."),
	External = 1 UMETA(DisplayName = "External", ToolTip="Weights are read from the Weights input; each point selects a Weights row by matching its key attribute, and the Map key column names the Weights attribute holding each row's weight."),
};

UENUM()
enum class EPCGExWeightedLookupPickMode : uint8
{
	Highest        = 0 UMETA(DisplayName = "Highest", ToolTip="Deterministic: the row with the highest weight wins. Ties resolve to the first row."),
	WeightedRandom = 1 UMETA(DisplayName = "Weighted Random", ToolTip="Seeded random pick where each row's probability is proportional to its weight."),
};

UENUM()
enum class EPCGExWeightedLookupFallback : uint8
{
	None          = 0 UMETA(DisplayName = "None", ToolTip="No row is picked: forwarded attributes are left untouched on that point, the row index is -1."),
	UniformRandom = 1 UMETA(DisplayName = "Uniform Random", ToolTip="Pick a row at random, ignoring weights."),
	FixedRow      = 2 UMETA(DisplayName = "Fixed Row", ToolTip="Pick a specific row index."),
};

namespace PCGExWeightedLookup::Labels
{
	const FName SourceMapLabel = TEXT("Map");
	const FName SourceWeightsLabel = TEXT("Weights");
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "weighted lookup pick match landscape layer weight table row", PCGExNodeLibraryDoc="metadata/keys/weighted-lookup"))
class UPCGExWeightedLookupSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExWeightedLookupSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(WeightedLookup, "Weighted Lookup", "Pick one row of a Map attribute set per point, by highest or weighted-random weight, and forward that row's columns to the point. Each row's weight is read from the attribute its key column names.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	virtual bool UseSeed() const override
	{
		return true;
	}

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

	/** Map column holding, per row, the name of the attribute that carries that row's weight. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Map", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector MapKey;

	/** Which Map columns are forwarded onto the picked point. The key column and output names never are. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Map", meta=(PCG_Overridable))
	FPCGExNameFiltersDetails Columns;

	/** Where each row's weight is read from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weights", meta=(PCG_Overridable))
	EPCGExWeightedLookupWeightsSource WeightsSource = EPCGExWeightedLookupWeightsSource::Self;

	/** Point attribute matched (by value hash) against the Weights key to select the point's Weights row. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weights", meta=(PCG_Overridable, EditCondition="WeightsSource == EPCGExWeightedLookupWeightsSource::External", EditConditionHides))
	FPCGAttributePropertyInputSelector PointKey;

	/** Weights attribute matched against each point's key. On duplicate keys, the first row wins. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weights", meta=(PCG_Overridable, EditCondition="WeightsSource == EPCGExWeightedLookupWeightsSource::External", EditConditionHides))
	FPCGAttributePropertyInputSelector WeightsKey;

	/** Suppress the warning emitted when a row's weight attribute is missing. Missing weights read as 0. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weights", meta=(PCG_Overridable))
	bool bQuietMissingWeightAttribute = false;

	/** How the picked row is chosen from the per-point weights. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable))
	EPCGExWeightedLookupPickMode PickMode = EPCGExWeightedLookupPickMode::WeightedRandom;

	/** Which components contribute to seed generation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGExCore.EPCGExSeedComponents"))
	uint8 SeedComponents = static_cast<uint8>(EPCGExSeedComponents::Local | EPCGExSeedComponents::Settings);

	/** Local seed offset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable))
	int32 LocalSeed = 0;

	/** Exponent on max-normalized weights before the roll. 1 = no change, >1 sharpens, <1 flattens. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable, ClampMin=0.001, EditCondition="PickMode == EPCGExWeightedLookupPickMode::WeightedRandom", EditConditionHides))
	double Exponent = 1.0;

	/** Contrast on max-normalized weights before the roll, around 0.5. 1 = no change, >1 = more, <1 = less. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable, ClampMin=0.001, EditCondition="PickMode == EPCGExWeightedLookupPickMode::WeightedRandom", EditConditionHides))
	double Contrast = 1.0;

	/** Contrast curve type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable, DisplayName=" └─ Curve", EditCondition="PickMode == EPCGExWeightedLookupPickMode::WeightedRandom && Contrast != 1.0", EditConditionHides))
	EPCGExContrastCurve ContrastCurve = EPCGExContrastCurve::Power;

	/** What happens when a point has no usable weight (all weights are 0, or its key matches no Weights row). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable))
	EPCGExWeightedLookupFallback Fallback = EPCGExWeightedLookupFallback::None;

	/** Row picked by the Fixed Row fallback. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Pick", meta=(PCG_Overridable, ClampMin=0, EditCondition="Fallback == EPCGExWeightedLookupFallback::FixedRow", EditConditionHides))
	int32 FallbackRowIndex = 0;

	/** Write the picked row's key (the weight attribute name) to the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputKey = false;

	/** Name of the attribute the picked key is written to. NAME_None when no row was picked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bOutputKey"))
	FName KeyAttributeName = "LookupKey";

	/** Write the picked row index to the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputRowIndex = false;

	/** Name of the attribute the picked row index is written to. -1 when no row was picked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bOutputRowIndex"))
	FName RowIndexAttributeName = "LookupRowIndex";

	/** Write the picked row's raw weight (before exponent and contrast) to the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputWeight = false;

	/** Name of the attribute the picked weight is written to. 0 when no row was picked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bOutputWeight"))
	FName WeightAttributeName = "LookupWeight";
};

struct FPCGExWeightedLookupContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExWeightedLookupElement;

	TSharedPtr<PCGExData::FFacade> MapFacade;
	int32 NumRows = 0;

	/** Per Map row, the name of the attribute holding that row's weight. */
	TArray<FName> MapKeys;

	/** Column filter as forward details; IgnoredColumns holds the key column and the enabled output names. */
	FPCGExForwardDetails ForwardDetails = FPCGExForwardDetails(true);
	TSet<FName> IgnoredColumns;

	// External weights only. Readers are row-aligned with MapKeys (null when the attribute is missing),
	// built once here and read concurrently by every processor.
	TSharedPtr<PCGExData::FFacade> WeightsFacade;
	TArray<TSharedPtr<PCGExData::TBuffer<double>>> ExternalWeightReaders;
	TMap<PCGExValueHash, int32> WeightsRowByKey;
	EPCGMetadataTypes WeightsKeyType = EPCGMetadataTypes::Unknown;

	/** Missing weight attributes already reported: Self mode warns once per name, not once per input. */
	FCriticalSection MissingWeightsLock;
	TSet<FName> ReportedMissingWeights;

	/** Clamped Fixed Row fallback index. */
	int32 FallbackRow = -1;

	bool bApplyExponent = false;
	bool bApplyContrast = false;

	/** Sanitized shaping knobs, kept strictly positive so a zero weight can never shape into a pickable one. */
	double Exponent = 1.0;
	double Contrast = 1.0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExWeightedLookupElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WeightedLookup)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWeightedLookup
{
	/** One weight reader per Map key on the facade; null for missing attributes, reported once per name unless quiet. */
	void BuildWeightReaders(FPCGExWeightedLookupContext* InContext, const UPCGExWeightedLookupSettings* InSettings, const TSharedPtr<PCGExData::FFacade>& InFacade, const bool bScoped, TArray<TSharedPtr<PCGExData::TBuffer<double>>>& OutReaders);

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExWeightedLookupContext, UPCGExWeightedLookupSettings>
	{
		int32 NumRows = 0;
		const UPCGComponent* Component = nullptr;

		/** Self weights only; External reads Context->ExternalWeightReaders. */
		TArray<TSharedPtr<PCGExData::TBuffer<double>>> SelfWeightReaders;
		const TArray<TSharedPtr<PCGExData::TBuffer<double>>>* WeightReaders = nullptr;

		/** Rows whose weight reader exists; the hot loop only reads these, missing rows stay at weight 0. */
		TArray<int32> ActiveRows;

		/** External weights only. */
		TSharedPtr<PCGExData::IBuffer> PointKeyReader;

		TSharedPtr<PCGExData::FDataForwardHandler> Forward;

		TSharedPtr<PCGExData::TBuffer<FName>> KeyWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> RowIndexWriter;
		TSharedPtr<PCGExData::TBuffer<double>> WeightWriter;

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

	protected:
		// One instantiation per (weights source, shaping) combination so the default path carries no shaping work.
		template <bool bExternal, bool bExponent, bool bContrast>
		void ProcessScope(const PCGExMT::FScope& Scope);

		int32 SeedFor(const int32 PointSeed) const;
		void Commit(const int32 Index, const int32 Row, const double Weight);
		void CommitFallback(const int32 Index, const int32 PointSeed);
	};
}
