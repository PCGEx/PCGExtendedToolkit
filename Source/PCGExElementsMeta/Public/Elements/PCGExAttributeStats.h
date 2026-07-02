// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Utils/PCGExCompare.h"


#include "PCGParamData.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Types/PCGExTypeOps.h"
#include "Types/PCGExTypeTraits.h"

#include "PCGExAttributeStats.generated.h"

UENUM()
enum class EPCGExStatsOutputToPoints : uint8
{
	None   = 0 UMETA(DisplayName = "No output", ToolTip="None"),
	Prefix = 1 UMETA(DisplayName = "Prefix", ToolTip="Uses specified name as a prefix to the attribute' name"),
	Suffix = 2 UMETA(DisplayName = "Suffix", ToolTip="Uss specified name as a suffix to the attribute' name"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/analyze/attribute-stats"))
class UPCGExAttributeStatsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
#if WITH_EDITOR

public:
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AttributeStats, "Attribute Stats", "Output attribute statistics.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Misc);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** Attributes to get. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails Filters = FPCGExNameFiltersDetails(true);

	/** Output a separate data collection for each unique value with occurrence counts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOutputPerUniqueValuesStats = false;

	/** How stats are written to point attributes (None, Prefix, or Suffix to attribute name). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExStatsOutputToPoints OutputToPoints = EPCGExStatsOutputToPoints::None;

	/** Output to tags */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExStatsOutputToPoints OutputToTags = EPCGExStatsOutputToPoints::None;

	/** Write a unique identifier for the source data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputIdentifier = true;

	/** Attribute name for the unique identifier. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Identifier", EditCondition="bOutputIdentifier"))
	FName IdentifierAttributeName = FName(TEXT("Identifier"));

	/** Write the attribute's default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputDefaultValue = true;

	/** Attribute name for the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Default", EditCondition="bOutputDefaultValue"))
	FName DefaultValueAttributeName = FName(TEXT("Default"));

	/** Write the minimum value found across all points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputMinValue = true;

	/** Attribute name for the minimum value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Min", EditCondition="bOutputMinValue"))
	FName MinValueAttributeName = FName(TEXT("Min"));

	/** Write the maximum value found across all points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputMaxValue = true;

	/** Attribute name for the maximum value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Max", EditCondition="bOutputMaxValue"))
	FName MaxValueAttributeName = FName(TEXT("Max"));

	/** Write the minimum value excluding points with the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSetMinValue = true;

	/** Attribute name for the non-default minimum value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Non-Default Min", EditCondition="bOutputSetMinValue"))
	FName SetMinValueAttributeName = FName(TEXT("SetMin"));

	/** Write the maximum value excluding points with the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSetMaxValue = true;

	/** Attribute name for the non-default maximum value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Non-Default Max", EditCondition="bOutputSetMaxValue"))
	FName SetMaxValueAttributeName = FName(TEXT("SetMax"));

	/** Write the average value across all points. For non-numeric types, outputs the most frequent value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputAverageValue = true;

	/** Attribute name for the average value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Average", EditCondition="bOutputAverageValue"))
	FName AverageValueAttributeName = FName(TEXT("Average"));

	/** Write the count of values that appear exactly once. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputUniqueValuesNum = true;

	/** Attribute name for the unique values count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Unique Values Num", EditCondition="bOutputUniqueValuesNum"))
	FName UniqueValuesNumAttributeName = FName(TEXT("UniqueValues"));

	/** Write the count of non-default values that appear exactly once. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputUniqueSetValuesNum = true;

	/** Attribute name for the unique non-default values count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Unique Set Values Num", EditCondition="bOutputUniqueSetValuesNum"))
	FName UniqueSetValuesNumAttributeName = FName(TEXT("UniqueSetValues"));

	/** Write the total number of distinct values found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputDifferentValuesNum = true;

	/** Attribute name for the different values count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Different Values Num", EditCondition="bOutputDifferentValuesNum"))
	FName DifferentValuesNumAttributeName = FName(TEXT("DifferentValues"));

	/** Write the total number of distinct non-default values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputDifferentSetValuesNum = true;

	/** Attribute name for the different non-default values count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Different Set Values Num", EditCondition="bOutputDifferentSetValuesNum"))
	FName DifferentSetValuesNumAttributeName = FName(TEXT("DifferentSetValues"));

	/** Write the count of points with the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputDefaultValuesNum = true;

	/** Attribute name for the default values count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Default Values Num", EditCondition="bOutputDefaultValuesNum"))
	FName DefaultValuesNumAttributeName = FName(TEXT("DefaultValues"));

	/** Write whether all points have the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputHasOnlyDefaultValues = true;

	/** Attribute name for the all-default-values flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Has only Default Values", EditCondition="bOutputHasOnlyDefaultValues"))
	FName HasOnlyDefaultValuesAttributeName = FName(TEXT("HasOnlyDefaultValues"));

	/** Write whether no points have the default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputHasOnlySetValues = true;

	/** Attribute name for the no-default-values flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Has only Set Values", EditCondition="bOutputHasOnlySetValues"))
	FName HasOnlySetValuesAttributeName = FName(TEXT("HasOnlySetValues"));

	/** Write whether every point has a unique value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputHasOnlyUniqueValues = true;

	/** Attribute name for the all-unique-values flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Has only Unique Values", EditCondition="bOutputHasOnlyUniqueValues"))
	FName HasOnlyUniqueValuesAttributeName = FName(TEXT("HasOnlyUniqueValues"));

	/** Write the total number of sampled points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSamples = true;

	/** Attribute name for the sample count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Samples", EditCondition="bOutputSamples"))
	FName SamplesAttributeName = FName(TEXT("Samples"));

	/** Write whether the attribute was valid for stats computation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOutputIsValid = true;

	/** Attribute name for the validity flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName = "Is Valid", EditCondition="bOutputIsValid"))
	FName IsValidAttributeName = FName(TEXT("IsValid"));


	/** Attribute name for the unique value column in per-value stats output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs (Unique Values)", meta = (PCG_Overridable, DisplayName = "Value Column", EditCondition="bOutputPerUniqueValuesStats"))
	FName UniqueValueAttributeName = FName(TEXT("Value"));

	/** Exclude the default value from unique values output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs (Unique Values)", meta = (PCG_Overridable, EditCondition="bOutputPerUniqueValuesStats"))
	bool bOmitDefaultValue = false;

	/** Attribute name for the occurrence count in per-value stats output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs (Unique Values)", meta = (PCG_Overridable, DisplayName = "Value Count", EditCondition="bOutputPerUniqueValuesStats"))
	FName ValueCountAttributeName = FName(TEXT("Count"));

	/** Suppress warnings when attribute type doesn't support stats computation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bQuietTypeMismatchWarning = false;

	/** Prevent feedback loops when stats output is used as input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bFeedbackLoopFailsafe = true;

private:
	friend class FPCGExAttributeStatsElement;
};

struct FPCGExAttributeStatsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExAttributeStatsElement;
	TSharedPtr<PCGExData::FAttributesInfos> AttributesInfos;

	TArray<UPCGParamData*> OutputParams;
	TMap<FName, UPCGParamData*> OutputParamsMap;
	TArray<int64> Rows;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExAttributeStatsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributeStats)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExAttributeStats
{
	const FName OutputAttributeStats = FName("Stats");
	const FName OutputAttributeUniqueValues = FName("UniqueValues");

	class IAttributeStats : public TSharedFromThis<IAttributeStats>
	{
	public:
		const PCGExData::FAttributeIdentity Identity;
		const int64 Key;

		explicit IAttributeStats(const PCGExData::FAttributeIdentity& InIdentity, const int64 InKey)
			: Identity(InIdentity)
			  , Key(InKey)
		{
		}

		virtual ~IAttributeStats() = default;

		virtual void Process(const TSharedRef<PCGExData::FFacade> InDataFacade, FPCGExAttributeStatsContext* Context, const UPCGExAttributeStatsSettings* Settings, const TArray<int8>& Filter)
		{
		}
	};

	template <typename T>
	class TAttributeStats : public IAttributeStats
	{
		using Traits = PCGExTypes::TTraits<T>;

	public:
		T DefaultValue = T{};
		T MinValue = T{};
		T MaxValue = T{};
		T SetMinValue = T{};
		T SetMaxValue = T{};
		T AverageValue = T{};
		T AverageSetValue = T{};
		T MaxUniqueValue = T{};
		T MinUniqueValue = T{};
		int32 UniqueValuesNum = 0;
		int32 UniqueSetValuesNum = 0;
		int32 DifferentValuesNum = 0;
		int32 DifferentSetValuesNum = 0;
		int32 DefaultValuesNum = 0;

		explicit TAttributeStats(const PCGExData::FAttributeIdentity& InIdentity, const int64 InKey)
			: IAttributeStats(InIdentity, InKey)
		{
		}

		virtual void Process(const TSharedRef<PCGExData::FFacade> InDataFacade, FPCGExAttributeStatsContext* Context, const UPCGExAttributeStatsSettings* Settings, const TArray<int8>& Filter) override
		{
			UPCGParamData* ParamData = Context->OutputParamsMap[Identity.Name];

			FString StrName = Identity.Name.ToString();
			UPCGMetadata* PointsMetadata = nullptr;

			const PCGExTypeOps::ITypeOpsBase* TypeOps = PCGExTypeOps::FTypeOpsRegistry::Get<T>();

			if (Settings->OutputToPoints != EPCGExStatsOutputToPoints::None)
			{
				PointsMetadata = InDataFacade->GetOut()->Metadata;
			}

#define PCGEX_OUTPUT_STAT(_NAME, _TYPE, _VALUE) \
	if(Settings->bOutput##_NAME){ ParamData->Metadata->GetMutableTypedAttribute<_TYPE>(Settings->_NAME##AttributeName)->SetValue(Key, _VALUE); \
	if(Settings->OutputToTags != EPCGExStatsOutputToPoints::None){ InDataFacade->Source->Tags->Set<_TYPE>(Settings->OutputToTags == EPCGExStatsOutputToPoints::Prefix ? (Settings->_NAME##AttributeName.ToString() + StrName) : (StrName + Settings->_NAME##AttributeName.ToString()), _VALUE); } \
	if (PointsMetadata){\
		FPCGAttributeIdentifier PrintName(Settings->OutputToPoints == EPCGExStatsOutputToPoints::Prefix ? FName(Settings->_NAME##AttributeName.ToString() + StrName) : FName(StrName + Settings->_NAME##AttributeName.ToString()), PCGMetadataDomainID::Data);\
		if (PointsMetadata->GetConstTypedAttribute<_TYPE>(PrintName)) { PointsMetadata->DeleteAttribute(PrintName); }\
		PointsMetadata->FindOrCreateAttribute<_TYPE>(PrintName, _VALUE);} }

			TSharedPtr<PCGExData::TBuffer<T>> Buffer = InDataFacade->GetReadable<T>(Identity.GetIdentifier());
			SetMinValue = MinValue = Traits::Max();
			SetMaxValue = MaxValue = Traits::Min();

#define PCGEX_NO_AVERAGE if constexpr (std::is_same_v<T, FString> || std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)

			if (!Buffer)
			{
				// Invalid attribute, type mismatch!
				PCGEX_OUTPUT_STAT(IsValid, bool, false)
				return;
			}

			const FString Identifier = FString::Printf(TEXT("PCGEx/Identifier:%u"), InDataFacade->Source->GetIn()->GetUniqueID());
			PCGEX_OUTPUT_STAT(Identifier, FString, Identifier)

			if constexpr (!PCGEx::IsValidForTMap<T>::value)
			{
				// Unsupported types
				PCGEX_OUTPUT_STAT(IsValid, bool, false)
			}
			else
			{
				UPCGParamData* UniqueValuesParamData = nullptr;
				if (Settings->bOutputPerUniqueValuesStats)
				{
					UniqueValuesParamData = Context->ManagedObjects->New<UPCGParamData>();

					Context->StageOutput(
						UniqueValuesParamData, OutputAttributeUniqueValues, PCGExData::EStaging::None,
						{Identifier, Identity.Name.ToString()});

					InDataFacade->Source->Tags->AddRaw(Identifier);
				}

				const int32 NumPoints = InDataFacade->GetNum();
				TMap<T, int32> ValuesCount;
				TMap<T, int32> SetValuesCount;
				ValuesCount.Reserve(NumPoints);
				SetValuesCount.Reserve(NumPoints);

				DefaultValue = Buffer->InAttribute->template GetValueFromItemKey<T>(PCGDefaultValueKey);
				int32 NumValues = 0;

				for (int i = 0; i < NumPoints; i++)
				{
					if (!Filter[i])
					{
						continue;
					}
					NumValues++;

					const T& Value = Buffer->Read(i);

					TypeOps->BlendMin(&Value, &MinValue, &MinValue);
					TypeOps->BlendMax(&Value, &MaxValue, &MaxValue);

					PCGEX_NO_AVERAGE
					{
					}
					else
					{
						TypeOps->BlendAdd(&Value, &AverageValue, &AverageValue);
					}

					int32& Count = ValuesCount.FindOrAdd(Value, 0);
					++Count;

					if (PCGExCompare::StrictlyEqual(Value, DefaultValue))
					{
						DefaultValuesNum++;
					}
					else
					{
						int32& SetCount = SetValuesCount.FindOrAdd(Value, 0);
						++SetCount;

						TypeOps->BlendMin(&Value, &SetMinValue, &SetMinValue);
						TypeOps->BlendMax(&Value, &SetMaxValue, &SetMaxValue);
					}
				}

				PCGEX_NO_AVERAGE
				{
					// Pick the most present value.
					int32 Max = -1;
					for (const TPair<T, int32>& Pair : ValuesCount)
					{
						if (Pair.Value > Max)
						{
							Max = Pair.Value;
							AverageValue = Pair.Key;
						}
					}
				}

				if (UniqueValuesParamData)
				{
					UPCGMetadata* UVM = UniqueValuesParamData->Metadata;
					FPCGMetadataAttributeBase* UValues = UVM->FindOrCreateAttribute<T>(Settings->UniqueValueAttributeName, MinValue);
					FPCGMetadataAttribute<int32>* UCount = UVM->FindOrCreateAttribute<int32>(Settings->ValueCountAttributeName, 0);

					if (Settings->bOmitDefaultValue)
					{
						for (const TPair<T, int32>& Pair : SetValuesCount)
						{
							int64 UVKey = UVM->AddEntry();
							UValues->SetValue(UVKey, Pair.Key);
							UCount->SetValue(UVKey, Pair.Value);
						}
					}
					else
					{
						for (const TPair<T, int32>& Pair : ValuesCount)
						{
							int64 UVKey = UVM->AddEntry();
							UValues->SetValue(UVKey, Pair.Key);
							UCount->SetValue(UVKey, Pair.Value);
						}
					}
				}

				if (Settings->bOutputUniqueValuesNum)
				{
					UniqueValuesNum = 0;
					for (const TPair<T, int32>& Pair : ValuesCount)
					{
						if (Pair.Value == 1)
						{
							UniqueValuesNum++;
						}
					}
				}

				if (Settings->bOutputUniqueSetValuesNum)
				{
					UniqueSetValuesNum = 0;
					for (const TPair<T, int32>& Pair : SetValuesCount)
					{
						if (Pair.Value == 1)
						{
							UniqueSetValuesNum++;
						}
					}
				}

				DifferentValuesNum = ValuesCount.Num();
				DifferentSetValuesNum = SetValuesCount.Num();

				ValuesCount.Empty();
				SetValuesCount.Empty();

				////// OUTPUT		

				TypeOps->BlendDiv(&AverageValue, static_cast<int32>(NumValues), &AverageValue);

				PCGEX_OUTPUT_STAT(DefaultValue, T, DefaultValue)
				PCGEX_OUTPUT_STAT(MinValue, T, MinValue)
				PCGEX_OUTPUT_STAT(MaxValue, T, MaxValue)
				PCGEX_OUTPUT_STAT(SetMinValue, T, SetMinValue)
				PCGEX_OUTPUT_STAT(SetMaxValue, T, SetMaxValue)
				PCGEX_OUTPUT_STAT(AverageValue, T, AverageValue)
				PCGEX_OUTPUT_STAT(UniqueValuesNum, int32, UniqueValuesNum)
				PCGEX_OUTPUT_STAT(UniqueSetValuesNum, int32, UniqueSetValuesNum)
				PCGEX_OUTPUT_STAT(DifferentValuesNum, int32, DifferentValuesNum)
				PCGEX_OUTPUT_STAT(DifferentSetValuesNum, int32, DifferentSetValuesNum)
				PCGEX_OUTPUT_STAT(DefaultValuesNum, int32, DefaultValuesNum)
				PCGEX_OUTPUT_STAT(HasOnlyDefaultValues, bool, NumValues == DefaultValuesNum)
				PCGEX_OUTPUT_STAT(HasOnlySetValues, bool, DefaultValuesNum == 0)
				PCGEX_OUTPUT_STAT(HasOnlyUniqueValues, bool, NumValues == UniqueSetValuesNum)
				PCGEX_OUTPUT_STAT(Samples, int32, NumValues)
				PCGEX_OUTPUT_STAT(IsValid, bool, true)

#undef PCGEX_OUTPUT_STAT
#undef PCGEX_NO_AVERAGE
			}
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAttributeStatsContext, UPCGExAttributeStatsSettings>
	{
		TArray<TSharedPtr<IAttributeStats>> Stats;
		TMap<FName, int32> PerAttributeStatMap;
		TArray<UPCGParamData*> PerAttributeStats;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
