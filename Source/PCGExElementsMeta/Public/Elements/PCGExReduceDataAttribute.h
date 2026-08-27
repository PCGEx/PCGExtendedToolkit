// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"


#include "Core/PCGExPointsProcessor.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExAttributesDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExTypes.h"

#include "PCGExReduceDataAttribute.generated.h"

class FPCGMetadataAttributeBase;

UENUM()
enum class EPCGExReduceDataDomainMethod : uint8
{
	Min          = 0 UMETA(DisplayName = "Min", ToolTip="Keep the smallest value across every input, component by component for vector types."),
	Max          = 1 UMETA(DisplayName = "Max", ToolTip="Keep the largest value across every input, component by component for vector types."),
	Sum          = 2 UMETA(DisplayName = "Sum", ToolTip="Add every input value together."),
	Average      = 3 UMETA(DisplayName = "Average", ToolTip="Add every input value together, then divide by the number of inputs."),
	Join         = 4 UMETA(DisplayName = "Join", ToolTip="Concatenate every input value as text, separated by Join Delimiter. The output is forced to String, so Custom Output Type has no effect."),
	Hash         = 5 UMETA(DisplayName = "Hash", ToolTip="Hashed in order of inputs"),
	UnsignedHash = 6 UMETA(DisplayName = "Hash (Sorted)", ToolTip="Sorted, then hashed in sorted order"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/analyze/reduce-data"))
class UPCGExReduceDataAttributeSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ReduceDataAttribute, "Reduce Data", "Reduce @Data domain attribute.", FName(GetDisplayName()));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}

	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

	virtual FName GetMainInputPin() const override
	{
		return PCGPinConstants::DefaultInputLabel;
	}

	virtual FName GetMainOutputPin() const override
	{
		return PCGPinConstants::DefaultOutputLabel;
	}

protected:
	virtual bool IsInputless() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAttributeSourceToTargetDetails Attributes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExReduceDataDomainMethod Method = EPCGExReduceDataDomainMethod::Min;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bCustomOutputType;

	/* Not supported for Join! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bCustomOutputType"))
	EPCGMetadataTypes OutputType = EPCGMetadataTypes::Integer32;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Method == EPCGExReduceDataDomainMethod::Join", EditConditionHides))
	FString JoinDelimiter = FString(", ");

#if WITH_EDITOR
	virtual FString GetDisplayName() const;
#endif
};

struct FPCGExReduceDataAttributeContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExReduceDataAttributeElement;
	FPCGAttributeIdentifier WriteIdentifier;
	TArray<const FPCGMetadataAttributeBase*> Attributes;
	EPCGMetadataTypes OutputType = EPCGMetadataTypes::Unknown;
};

class FPCGExReduceDataAttributeElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ReduceDataAttribute)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	template <typename T, typename Fn>
	void ForEachValue(const TArray<const FPCGMetadataAttributeBase*>& InAttributes, Fn&& Func) const
	{
		for (int i = 0; i < InAttributes.Num(); i++)
		{
			const FPCGMetadataAttributeBase* Att = InAttributes[i];
			PCGExMetaHelpers::ExecuteWithRightType(Att->GetTypeId(), [&](auto ValueType)
			{
				using T_ATTR = decltype(ValueType);
				const FPCGMetadataAttribute<T_ATTR>* TypedAtt = static_cast<const FPCGMetadataAttribute<T_ATTR>*>(Att);
				T_ATTR Value = PCGExData::Helpers::ReadDataValue(TypedAtt);
				Func(PCGExTypeOps::Convert<T_ATTR, T>(Value), i);
			});
		}
	}

	template <typename T>
	void AggregateValues(TArray<T>& OutList, const TArray<const FPCGMetadataAttributeBase*>& InAttributes) const
	{
		OutList.Reserve(InAttributes.Num());
		ForEachValue<T>(InAttributes, [&](T Value, int32)
		{
			OutList.Add(Value);
		});
	}
};
