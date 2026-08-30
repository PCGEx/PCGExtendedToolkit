// Copyright 2026 Edward Boucher and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExConstantsDefinitions.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "Core/PCGExContext.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Elements/Debug/PCGExDrawAttributes.h"
#include "PCGExConstants.generated.h"


UCLASS(BlueprintType, ClassGroup=(Procedural), meta=(PCGExNodeLibraryDoc="metadata/keys/constant"))
class UPCGExConstantsSettings : public UPCGExSettings
{
public:
	GENERATED_BODY()

#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(Constant, "Constant", "Constants.", GetEnumName());

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}

	FName GetEnumName() const;

	virtual bool OnlyExposePreconfiguredSettings() const override
	{
		return true;
	};

	virtual bool CanUserEditTitle() const override
	{
		return false;
	}

	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

	/** Used by the preconfigured settings to load the right constants. */
	UPROPERTY(BlueprintReadWrite, Category=Settings)
	EPCGExConstantListID ConstantList;

	/** Export the negative of the constant instead of the constant itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool NegateOutput = false;

	/** Output 1/x instead of x (e.g. 2 becomes 1/2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool OutputReciprocal = false;

	/** Apply a custom multiplier to the output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	double CustomMultiplier = 1.0;

	/** Cast to a specific type. Double is used by default, ignored for vectors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	EPCGExNumericOutput NumericOutputType;

	UFUNCTION(BlueprintCallable, Category=Settings)
	static EPCGExConstantType GetOutputType(EPCGExConstantListID ListID);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, NoClear, EditFixedSize, meta=(ReadOnlyKeys, ForceInlineRow))
	TMap<FName, FName> AttributeNameMap = {};

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override
	{
		return {};
	}

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	static PCGExConstants::TDescriptorList<double> GetNumericConstantList(EPCGExConstantListID ConstantList)
	{
		if (ConstantList < EPCGExConstantListID::ADDITIONAL_NUMERICS)
		{
			return PCGExConstants::Numbers.ExportedConstants[static_cast<uint8>(ConstantList)];
		}
		constexpr uint8 AdditionalValuesStart = static_cast<uint8>(EPCGExConstantListID::ADDITIONAL_NUMERICS) + 1;
		return PCGExConstants::AdditionalNumbers.ExportedConstants[static_cast<uint8>(ConstantList) - AdditionalValuesStart];
	}

	static PCGExConstants::TDescriptorList<FVector> GetVectorConstantList(EPCGExConstantListID ConstantList)
	{
		if (ConstantList == EPCGExConstantListID::Vectors)
		{
			return PCGExConstants::Vectors.ExportedConstants[0];
		}

		constexpr uint8 AdditionalVectorsStart = static_cast<uint8>(EPCGExConstantListID::ADDITIONAL_VECTORS) + 1;

		return PCGExConstants::AdditionalVectors.ExportedConstants[static_cast<uint8>(ConstantList) - AdditionalVectorsStart];
	}

	static TArray<PCGExConstants::TDescriptor<bool>> GetBooleanConstantList(EPCGExConstantListID ConstantList)
	{
		if (ConstantList == EPCGExConstantListID::TrueBool)
		{
			return {PCGExConstants::Booleans[0]};
		}

		if (ConstantList == EPCGExConstantListID::FalseBool)
		{
			return {PCGExConstants::Booleans[1]};
		}

		return PCGExConstants::Booleans;
	}

	template <typename T>
	T ApplyNumericValueSettings(const T& InValue) const
	{
		T Value = InValue;
		if (NegateOutput)
		{
			Value *= -1;
		}

		if constexpr (std::is_floating_point_v<T>)
		{
			if (OutputReciprocal)
			{
				if (!FMath::IsNearlyZero(Value))
				{
					Value = 1 / Value;
				}
				else
				{
					Value = 0;
				}
			}
		}

		Value *= CustomMultiplier;
		return Value;
	}
};

class FPCGExConstantsElement : public IPCGExElement
{
protected:
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;


	// InPinLabel stays the canonical constant name -- pins are declared from it and renames must not detach them.
	template <typename T>
	void StageConstant(FPCGExContext* InContext, const FName InPinLabel, const FName InName, const T& InValue, const UPCGExConstantsSettings* Settings) const
	{
		if (!PCGExMetaHelpers::IsWritableAttributeName(InName))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("\"{0}\" is not a valid attribute name."), FText::FromName(InName)));
			return;
		}

		UPCGParamData* OutputData = InContext->ManagedObjects->New<UPCGParamData>();
		check(OutputData && OutputData->Metadata);

		FPCGMetadataAttributeBase* Attrib = OutputData->Metadata->CreateAttribute<T>(InName, InValue, true, false);
		Attrib->SetValue(OutputData->Metadata->AddEntry(), InValue);

		InContext->StageOutput(OutputData, InPinLabel, PCGExData::EStaging::Managed);
	}

public:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT
};
