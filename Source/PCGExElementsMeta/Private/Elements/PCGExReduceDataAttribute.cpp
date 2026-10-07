// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExReduceDataAttribute.h"

#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExDataHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExReduceDataAttributeElement"
#define PCGEX_NAMESPACE ReduceDataAttribute

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExReduceDataAttributeSettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Infos;

	const TSet<EPCGExReduceDataDomainMethod> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExReduceDataDomainMethod>(ValuesToSkip, FTEXT("PCGEx | Reduce Data : {0}"));
}
#endif

void UPCGExReduceDataAttributeSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExReduceDataDomainMethod>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Method = static_cast<EPCGExReduceDataDomainMethod>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

TArray<FPCGPinProperties> UPCGExReduceDataAttributeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY(GetMainInputPin(), "Inputs", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExReduceDataAttributeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(GetMainOutputPin(), "Reduced attribute.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ReduceDataAttribute)

#if WITH_EDITOR
FString UPCGExReduceDataAttributeSettings::GetDisplayName() const
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGExReduceDataDomainMethod>())
	{
		FString DisplayName = EnumPtr->GetNameStringByValue(static_cast<int64>(Method)) + TEXT(" @Data.") + Attributes.Source.ToString();
		return DisplayName;
	}
	return GetDefaultNodeTitle().ToString();
}
#endif

bool FPCGExReduceDataAttributeElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ReduceDataAttribute)

	// A property selector leaves the name unset, which the validation below refuses.
	FPCGAttributeIdentifier ReadIdentifier;
	PCGExData::Helpers::TryGetDataValueIdentifier(Settings->Attributes.GetSourceSelector(), ReadIdentifier);
	PCGEX_VALIDATE_NAME(ReadIdentifier.Name)

	Context->WriteIdentifier = Settings->Attributes.GetTargetSelector().GetAttributeName();
	PCGEX_VALIDATE_NAME(Context->WriteIdentifier.Name)
	Context->WriteIdentifier.MetadataDomain = PCGMetadataDomainID::Elements;

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());

	TMap<int16, int32> TypeCounter;

	int32 MaxCount = 0;
	Context->Attributes.Reserve(Inputs.Num());
	TypeCounter.Reserve(Inputs.Num());

	for (const FPCGTaggedData& TaggedData : Inputs)
	{
		if (!TaggedData.Data || !TaggedData.Data->Metadata)
		{
			continue;
		}
		const FPCGMetadataAttributeBase* Attribute = PCGExMetaHelpers::TryGetConstAttribute(TaggedData.Data, ReadIdentifier);

		if (!Attribute)
		{
			PCGEX_LOG_WARN_ATTR_C(Context, Source, ReadIdentifier.Name)
			continue;
		}

		int32& Count = TypeCounter.FindOrAdd(Attribute->GetTypeId(), 0);
		Count++;

		if (MaxCount < Count)
		{
			MaxCount = Count;
			Context->OutputType = static_cast<EPCGMetadataTypes>(Attribute->GetTypeId());
		}

		Context->Attributes.Add(Attribute);
	}

	if (Context->Attributes.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing any valid input."))
		return false;
	}

	if (Settings->Method == EPCGExReduceDataDomainMethod::Join)
	{
		Context->OutputType = EPCGMetadataTypes::String;
	}
	else if (!Settings->bCustomOutputType
		&& (Settings->Method == EPCGExReduceDataDomainMethod::Hash || Settings->Method == EPCGExReduceDataDomainMethod::UnsignedHash))
	{
		Context->OutputType = EPCGMetadataTypes::Integer64;
	}

	return true;
}

bool FPCGExReduceDataAttributeElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExReduceDataAttributeElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ReduceDataAttribute)
	PCGEX_EXECUTION_CHECK

	UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();

	PCGEX_ON_INITIAL_EXECUTION
	{
		UPCGMetadata* OutMetadata = ParamData->Metadata;

		if (Settings->Method == EPCGExReduceDataDomainMethod::Join)
		{
			TArray<FString> StringsToJoin;
			AggregateValues(StringsToJoin, Context->Attributes);

			const FString OutValue = FString::Join(StringsToJoin, *Settings->JoinDelimiter);
			FPCGMetadataAttribute<FString>* OutAtt = OutMetadata->FindOrCreateAttribute(Context->WriteIdentifier, OutValue);
			OutAtt->SetValue(OutMetadata->AddEntry(), OutValue);
		}
		else if (Settings->Method == EPCGExReduceDataDomainMethod::Hash
			|| Settings->Method == EPCGExReduceDataDomainMethod::UnsignedHash)
		{
			TArray<int32> Hashes;
			AggregateValues(Hashes, Context->Attributes);

			if (Settings->Method == EPCGExReduceDataDomainMethod::UnsignedHash)
			{
				Hashes.Sort();
			}

			int64 AggregatedHash = CityHash64(reinterpret_cast<const char*>(Hashes.GetData()), Hashes.Num() * sizeof(int32));

			PCGExMetaHelpers::ExecuteWithRightType(Context->OutputType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				const T OutValue = PCGExTypeOps::Convert<int64, T>(AggregatedHash);
				FPCGMetadataAttribute<T>* OutAtt = OutMetadata->FindOrCreateAttribute(Context->WriteIdentifier, OutValue);
				OutAtt->SetValue(OutMetadata->AddEntry(), OutValue);
			});
		}
		else
		{
			PCGExMetaHelpers::ExecuteWithRightType(Context->OutputType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				T ReducedValue = T{};

				ForEachValue<T>(Context->Attributes, [&](T Value, int32 i)
				{
					if (i == 0)
					{
						ReducedValue = Value;
					}
					else
					{
						switch (Settings->Method)
						{
						case EPCGExReduceDataDomainMethod::Min:
							ReducedValue = PCGExTypeOps::FTypeOps<T>::Min(ReducedValue, Value);
							break;
						case EPCGExReduceDataDomainMethod::Max:
							ReducedValue = PCGExTypeOps::FTypeOps<T>::Max(ReducedValue, Value);
							break;
						case EPCGExReduceDataDomainMethod::Sum:
						case EPCGExReduceDataDomainMethod::Average:
							ReducedValue = PCGExTypeOps::FTypeOps<T>::Add(ReducedValue, Value);
							break;
						default: case EPCGExReduceDataDomainMethod::Join:
							break;
						}
					}
				});

				if (Settings->Method == EPCGExReduceDataDomainMethod::Average)
				{
					ReducedValue = PCGExTypeOps::FTypeOps<T>::Div(ReducedValue, Context->Attributes.Num());
				}

				FPCGMetadataAttribute<T>* OutAtt = OutMetadata->FindOrCreateAttribute(Context->WriteIdentifier, ReducedValue);
				OutAtt->SetValue(OutMetadata->AddEntry(), ReducedValue);
			});
		}
	}

	Context->StageOutput(ParamData, Settings->GetMainOutputPin(), PCGExData::EStaging::MutableAndManaged);
	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
