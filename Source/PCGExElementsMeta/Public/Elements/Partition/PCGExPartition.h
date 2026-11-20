// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Details/PCGExAttributesDetails.h"
#include "Math/PCGExMath.h"

#include "PCGExPartition.generated.h"

UENUM()
enum class EPCGExPartitionFilterMode : uint8
{
	Floor  = 0 UMETA(DisplayName = "Floor", Tooltip = "Floor the value (default behavior)"),
	Ceil   = 1 UMETA(DisplayName = "Ceil", Tooltip = "Ceiling the value"),
	Round  = 2 UMETA(DisplayName = "Round", Tooltip = "Round to nearest"),
	Modulo = 3 UMETA(DisplayName = "Modulo", Tooltip = "Use modulo operation with specified value"),
};

USTRUCT(BlueprintType)
struct FPCGExPartitonRuleConfig : public FPCGExInputConfig
{
	GENERATED_BODY()

	FPCGExPartitonRuleConfig()
	{
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const override
	{
		if (bEnabled)
		{
			return FPCGExInputConfig::GetDisplayName();
		}
		return "(Disabled) " + FPCGExInputConfig::GetDisplayName();
	}
#endif

	/** Enable or disable this partition. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayPriority=-1))
	bool bEnabled = true;

	/** Filter Size. Higher values means fewer, larger groups. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0.001))
	double FilterSize = 1.0;

	/** Upscale multiplier, applied before filtering. Handy to deal with floating point values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0.001))
	double Upscale = 1.0;

	/** Offset input value. Applied after upscaling the raw value.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Offset = 0.0;

	/** Filter mode determines how values are converted to partition keys. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPartitionFilterMode FilterMode = EPCGExPartitionFilterMode::Floor;

	/** Value used for modulo operation when FilterMode is set to Modulo. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="FilterMode == EPCGExPartitionFilterMode::Modulo", EditConditionHides, ClampMin=1))
	int32 ModuloValue = 10;

	/** Whether to clamp the partition key to a specific range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bClampKey = false;

	/** Minimum value for partition key clamping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bClampKey"))
	int64 KeyClampMin = 0;

	/** Maximum value for partition key clamping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bClampKey"))
	int64 KeyClampMax = 100;

	/** Whether to invert (negate) the partition key. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvertKey = false;

	/** Whether to use absolute value of the partition key. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAbsoluteKey = false;

	/** Whether to write the partition Key to an attribute. Useful for debugging.
	 * Note: They key is not the index, but instead the filtered value used to distribute into partitions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteKey = false;

	/** Name of the int64 attribute to write the partition Key to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteKey"))
	FName KeyAttributeName = "@Data.PartitionKey";

	/** Output the partition index instead of the value used for partitioning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteKey"))
	bool bUsePartitionIndexAsKey = false;

	/** Whether to write the partition Key to a tag. Will write tags as 'Prefix::Key' */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTag = false;

	/** Name of the tag prefix used for this partition. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteTag"))
	FName TagPrefixName = "Partition";

	/** Output the partition index to the tag instead of the value used for partitioning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteTag"))
	bool bTagUsePartitionIndexAsKey = false;
};

namespace PCGExPartition
{
	struct FRule final
	{
		explicit FRule(FPCGExPartitonRuleConfig& InRule)
			: RuleConfig(&InRule)
			  , FilterSize(InRule.FilterSize)
			  , Upscale(InRule.Upscale)
			  , Offset(InRule.Offset)
			  , FilterMode(InRule.FilterMode)
			  , ModuloValue(InRule.ModuloValue)
			  , bClampKey(InRule.bClampKey)
			  , KeyClampMin(InRule.KeyClampMin)
			  , KeyClampMax(InRule.KeyClampMax)
			  , bInvertKey(InRule.bInvertKey)
			  , bAbsoluteKey(InRule.bAbsoluteKey)
		{
		}

		~FRule()
		{
			RuleConfig = nullptr;
		}

		FPCGExPartitonRuleConfig* RuleConfig;
		TArray<int64> FilteredValues;
		TMap<int64, int32> KeyToPartitionIndex;

		double FilterSize = 1.0;
		double Upscale = 1.0;
		double Offset = 0.0;
		EPCGExPartitionFilterMode FilterMode = EPCGExPartitionFilterMode::Floor;
		int32 ModuloValue = 10;
		bool bClampKey = false;
		int64 KeyClampMin = 0;
		int64 KeyClampMax = 100;
		bool bInvertKey = false;
		bool bAbsoluteKey = false;

		TSharedPtr<PCGExData::TBuffer<double>> DataCache;

		FORCEINLINE int64 Filter(const int32 Index) const
		{
			const double Value = DataCache->Read(Index) * Upscale + Offset;
			int64 Key;

			switch (FilterMode)
			{
			default:
			case EPCGExPartitionFilterMode::Floor:
			{
				const double Filtered = (Value - FMath::Fmod(Value, FilterSize)) / FilterSize + PCGExMath::SignPlus(Value);
				Key = static_cast<int64>(Filtered);
			}
				break;
			case EPCGExPartitionFilterMode::Ceil:
				Key = static_cast<int64>(FMath::CeilToDouble(Value / FilterSize));
				break;
			case EPCGExPartitionFilterMode::Round:
				Key = static_cast<int64>(FMath::RoundToDouble(Value / FilterSize));
				break;
			case EPCGExPartitionFilterMode::Modulo:
				Key = static_cast<int64>(Value) % ModuloValue;
				break;
			}

			if (bAbsoluteKey)
			{
				Key = FMath::Abs(Key);
			}
			if (bInvertKey)
			{
				Key = -Key;
			}
			if (bClampKey)
			{
				Key = FMath::Clamp(Key, KeyClampMin, KeyClampMax);
			}

			return Key;
		}
	};
};
