// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyWriter.h"
#include "PCGExCoreMacros.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"

void FPCGExPropertyOutputSettings::GetEffectiveConfigs(TArray<FPCGExPropertyOutputConfig>& OutConfigs) const
{
	OutConfigs.Reset();
	OutConfigs.Reserve(Configs.Num() + IncludedSchemas.Num());

	TSet<FName> SeenNames;
	SeenNames.Reserve(Configs.Num());

	// Disabled explicit entries still suppress same-named schema-derived entries below.
	for (const FPCGExPropertyOutputConfig& Config : Configs)
	{
		OutConfigs.Add(Config);
		if (!Config.PropertyName.IsNone())
		{
			SeenNames.Add(Config.PropertyName);
		}
	}

	// Resolve() handles recursion + cycle detection and skips entries with empty Name / invalid Property.
	TArray<FPCGExPropertyResolved> Resolved;
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& SchemaAsset : IncludedSchemas)
	{
		if (!SchemaAsset)
		{
			continue;
		}

		SchemaAsset->Collection.Resolve(Resolved);

		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (!Entry.Source)
			{
				continue;
			}

			const FName EntryName = Entry.Source->Name;
			if (EntryName.IsNone() || SeenNames.Contains(EntryName))
			{
				continue;
			}

			FPCGExPropertyOutputConfig& NewConfig = OutConfigs.AddDefaulted_GetRef();
			NewConfig.bEnabled = true;
			NewConfig.PropertyName = EntryName;

			SeenNames.Add(EntryName);
		}
	}
}

bool FPCGExPropertyOutputSettings::HasOutputs() const
{
	for (const FPCGExPropertyOutputConfig& Config : Configs)
	{
		if (Config.IsValid())
		{
			return true;
		}
	}

	TArray<FPCGExPropertyResolved> Resolved;
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& SchemaAsset : IncludedSchemas)
	{
		if (!SchemaAsset)
		{
			continue;
		}

		SchemaAsset->Collection.Resolve(Resolved);

		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (Entry.Source && !Entry.Source->Name.IsNone())
			{
				return true;
			}
		}
	}

	return false;
}

FName FPCGExPropertyOutputConfig::GetEffectiveOutputName() const
{
	// When OutputAttributeName is unset, the PropertyName is used directly - but PropertyName may
	// contain characters that aren't valid in an attribute name (e.g. "My.Prop"), so we sanitize
	// to produce a usable attribute name. When OutputAttributeName is explicitly set, keep the
	// strict check - the user is responsible for providing a valid name.
	if (OutputAttributeName.IsNone())
	{
		return PCGExMetaHelpers::SanitizeAttributeName(PropertyName);
	}

	if (!PCGExMetaHelpers::IsWritableAttributeName(OutputAttributeName))
	{
		return NAME_None;
	}
	return OutputAttributeName;
}

void FPCGExPropertySetWriter::CopySidecarClones(TArray<FInstancedStruct>& OutClones) const
{
	for (const FWriter& Writer : Writers)
	{
		const FPCGExProperty* Clone = Writer.WriterInstance.GetPtr<FPCGExProperty>();
		if (Clone && !Clone->GetOutputSidecarPin().IsNone())
		{
			OutClones.Add(Writer.WriterInstance);
		}
	}
}

bool FPCGExPropertySetWriter::Initialize(
	FPCGExContext* InContext,
	const IPCGExPropertyProvider* InProvider,
	const FPCGExPropertyOutputSettings& OutputSettings,
	UPCGMetadata* Metadata)
{
	TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
	OutputSettings.GetEffectiveConfigs(EffectiveConfigs);
	return Initialize(InContext, InProvider, EffectiveConfigs, Metadata);
}

bool FPCGExPropertySetWriter::Initialize(
	FPCGExContext* InContext,
	const IPCGExPropertyProvider* InProvider,
	TConstArrayView<FPCGExPropertyOutputConfig> EffectiveConfigs,
	UPCGMetadata* Metadata)
{
	Writers.Reset();
	Provider = InProvider;

	if (!Provider || !Metadata || EffectiveConfigs.IsEmpty())
	{
		return false;
	}

	Writers.Reserve(EffectiveConfigs.Num());

	for (const FPCGExPropertyOutputConfig& Config : EffectiveConfigs)
	{
		if (!Config.IsValid())
		{
			continue;
		}

		const FName OutputName = Config.GetEffectiveOutputName();
		if (OutputName.IsNone())
		{
			continue;
		}

		const FInstancedStruct* Prototype = Provider->FindPrototypeProperty(Config.PropertyName);
		if (!Prototype)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' not found in any schema source."), *Config.PropertyName.ToString())));
			continue;
		}

		const FPCGExProperty* PrototypeProp = Prototype->GetPtr<FPCGExProperty>();
		if (!PrototypeProp || !PrototypeProp->SupportsOutput())
		{
			continue;
		}

		FWriter& Writer = Writers.Emplace_GetRef();
		Writer.PropertyName = Config.PropertyName;
		Writer.WriterInstance = *Prototype;

		if (FPCGExProperty* MutableWriter = Writer.WriterInstance.GetMutablePtr<FPCGExProperty>())
		{
			Writer.Attribute = MutableWriter->CreateMetadataAttribute(Metadata, PrototypeProp->ResolveOutputAttributeName(OutputName));
		}

		if (!Writer.Attribute)
		{
			Writers.Pop(EAllowShrinking::No);
		}
	}

	return !Writers.IsEmpty();
}

void FPCGExPropertySetWriter::WriteAt(int32 WriterIdx, int64 Key, const FInstancedStruct& Source)
{
	FWriter& Writer = Writers[WriterIdx];

	// Script-struct equality is the cheapest way to confirm CopyValueFrom is safe -- different
	// concrete types can't transfer values through the type-erased FPCGExProperty interface.
	if (Source.GetScriptStruct() != Writer.WriterInstance.GetScriptStruct())
	{
		return;
	}

	const FPCGExProperty* SrcProp = Source.GetPtr<FPCGExProperty>();
	FPCGExProperty* WriterProp = Writer.WriterInstance.GetMutablePtr<FPCGExProperty>();
	if (!SrcProp || !WriterProp)
	{
		return;
	}

	WriterProp->CopyValueFrom(SrcProp);
	WriterProp->WriteMetadataValue(Writer.Attribute, Key);
}

int32 FPCGExPropertySetWriter::WriteEntry(int64 Key, int32 SourceIndex)
{
	if (!Provider)
	{
		return 0;
	}

	int32 NumWritten = 0;
	for (int32 w = 0; w < Writers.Num(); w++)
	{
		if (const FInstancedStruct* Source = Provider->GetPropertyAt(SourceIndex, Writers[w].PropertyName))
		{
			WriteAt(w, Key, *Source);
			++NumWritten;
		}
	}
	return NumWritten;
}

bool FPCGExPropertyWriter::Initialize(
	const IPCGExPropertyProvider* InProvider,
	const TSharedRef<PCGExData::FFacade>& OutputFacade,
	const FPCGExPropertyOutputSettings& OutputSettings)
{
	if (!InProvider)
	{
		return false;
	}

	TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
	OutputSettings.GetEffectiveConfigs(EffectiveConfigs);

	return Initialize(InProvider, OutputFacade, EffectiveConfigs);
}

bool FPCGExPropertyWriter::Initialize(
	const IPCGExPropertyProvider* InProvider,
	const TSharedRef<PCGExData::FFacade>& OutputFacade,
	TConstArrayView<FPCGExPropertyOutputConfig> EffectiveConfigs)
{
	if (!InProvider)
	{
		return false;
	}

	Provider = InProvider;

	for (const FPCGExPropertyOutputConfig& OutputConfig : EffectiveConfigs)
	{
		if (!OutputConfig.IsValid())
		{
			continue;
		}

		FName OutputName = OutputConfig.GetEffectiveOutputName();

		// Find prototype property from provider
		const FInstancedStruct* Prototype = Provider->FindPrototypeProperty(OutputConfig.PropertyName);
		if (!Prototype)
		{
			continue;
		}

		// Check if property supports output
		const FPCGExProperty* ProtoBase = Prototype->GetPtr<FPCGExProperty>();
		if (!ProtoBase || !ProtoBase->SupportsOutput())
		{
			continue;
		}

		// Clone as writer instance
		FInstancedStruct WriterInstance = *Prototype;

		// Initialize output buffers
		FPCGExProperty* Writer = WriterInstance.GetMutablePtr<FPCGExProperty>();
		if (!Writer || !Writer->InitializeOutput(OutputFacade, ProtoBase->ResolveOutputAttributeName(OutputName)))
		{
			continue;
		}

		WriterInstances.Add(OutputConfig.PropertyName, MoveTemp(WriterInstance));
	}

	return HasOutputs();
}

// WARNING: CopyValueFrom mutates the writer instance's Value field, so a single
// FPCGExPropertyWriter cannot be driven from multiple threads. Parallel writers must use
// FPCGExProperty::WriteOutputFrom directly (reads source, writes buffer, no shared mutation).
void FPCGExPropertyWriter::WriteProperties(int32 PointIndex, int32 SourceIndex)
{
	if (!Provider || SourceIndex < 0)
	{
		return;
	}

	for (auto& KV : WriterInstances)
	{
		const FName& PropName = KV.Key;
		FPCGExProperty* Writer = KV.Value.GetMutablePtr<FPCGExProperty>();
		if (!Writer)
		{
			continue;
		}

		if (const FInstancedStruct* SourceProp = Provider->GetPropertyAt(SourceIndex, PropName))
		{
			// Guards CopyValueFrom: the type-erased interface static_casts, so a mismatched source reinterprets memory.
			if (SourceProp->GetScriptStruct() == KV.Value.GetScriptStruct())
			{
				if (const FPCGExProperty* Source = SourceProp->GetPtr<FPCGExProperty>())
				{
					Writer->CopyValueFrom(Source);
				}
			}
		}

		Writer->WriteOutput(PointIndex);
	}
}

bool FPCGExPropertyWriter::HasOutputs() const
{
	return WriterInstances.Num() > 0;
}

namespace PCGExProperties
{
	bool WriteDataDomainValue(UPCGData* OutData, const FName OutName, const FPCGExProperty& InProperty)
	{
		if (!OutData || OutName.IsNone() || !InProperty.SupportsOutput())
		{
			return false;
		}

		bool bWritten = false;
		// An unsupported output type falls through the dispatcher's default arm, leaving bWritten false.
		PCGExMetaHelpers::ExecuteWithRightType(InProperty.GetOutputType(), [&](auto DummyValue)
		{
			using T = decltype(DummyValue);
			T Value{};
			if (!InProperty.TryGetValue<T>(Value))
			{
				return;
			}
			PCGExData::Helpers::SetDataValue<T>(OutData, OutName, Value);
			bWritten = true;
		});
		return bWritten;
	}

	void FlushSidecars(const TConstArrayView<const FPCGExProperty*> InProperties, const TFunctionRef<UPCGMetadata*(FName)> GetOrCreate)
	{
		for (const FPCGExProperty* Property : InProperties)
		{
			if (!Property)
			{
				continue;
			}
			const FName Pin = Property->GetOutputSidecarPin();
			if (Pin.IsNone())
			{
				continue;
			}
			if (UPCGMetadata* Sidecar = GetOrCreate(Pin))
			{
				Property->WriteOutputSidecar(Sidecar);
			}
		}
	}

	void StageSidecars(FPCGExContext* InContext, const TConstArrayView<const FPCGExProperty*> InProperties, const TFunctionRef<UPCGMetadata*(FName)> ExistingForPin)
	{
		TMap<FName, UPCGParamData*> Sidecars;
		FlushSidecars(InProperties, [&](const FName Pin) -> UPCGMetadata*
		{
			if (UPCGMetadata* Existing = ExistingForPin(Pin))
			{
				return Existing;
			}
			UPCGParamData*& Data = Sidecars.FindOrAdd(Pin);
			if (!Data)
			{
				Data = InContext->ManagedObjects->New<UPCGParamData>();
			}
			return Data->Metadata;
		});

		for (const TPair<FName, UPCGParamData*>& Pair : Sidecars)
		{
			FPCGTaggedData& OutTagged = InContext->OutputData.TaggedData.Emplace_GetRef();
			OutTagged.Pin = Pair.Key;
			OutTagged.Data = Pair.Value;
		}
	}

	void StageSidecars(FPCGExContext* InContext, const TConstArrayView<const FPCGExProperty*> InProperties)
	{
		StageSidecars(InContext, InProperties, [](FName) -> UPCGMetadata* { return nullptr; });
	}

	void StageSidecars(FPCGExContext* InContext, const TConstArrayView<FInstancedStruct> InClones, const TFunctionRef<UPCGMetadata*(FName)> ExistingForPin)
	{
		TArray<const FPCGExProperty*> Properties;
		Properties.Reserve(InClones.Num());
		for (const FInstancedStruct& Clone : InClones)
		{
			Properties.Add(Clone.GetPtr<FPCGExProperty>());
		}
		StageSidecars(InContext, Properties, ExistingForPin);
	}

	void StageSidecars(FPCGExContext* InContext, const TConstArrayView<FInstancedStruct> InClones)
	{
		StageSidecars(InContext, InClones, [](FName) -> UPCGMetadata* { return nullptr; });
	}

	void AddOutputMapPin(TArray<FPCGPinProperties>& PinProperties, const bool bVisible)
	{
		PCGEX_PIN_PARAM(Labels::OutputMapLabel, TEXT("External resources referenced by written properties (e.g. the Collection Map of Collection Entry picks)."), Normal)
		if (!bVisible)
		{
			PinProperties.Last().SetAdvancedPin();
		}
	}

	void GatherOutputDependencies(const TConstArrayView<FInstancedStruct> InProperties, TSet<FSoftObjectPath>& OutPaths)
	{
		for (const FInstancedStruct& Instance : InProperties)
		{
			if (const FPCGExProperty* Property = Instance.GetPtr<FPCGExProperty>())
			{
				Property->GatherOutputDependencies(OutPaths);
			}
		}
	}

	int32 WriteResolvedToDataDomain(UPCGData* OutData, const TConstArrayView<FPCGExPropertyResolved> InResolved)
	{
		int32 NumWritten = 0;
		for (const FPCGExPropertyResolved& Entry : InResolved)
		{
			const FPCGExProperty* Property = Entry.GetEffectiveProperty().GetPtr<FPCGExProperty>();
			if (!Property)
			{
				continue;
			}
			if (WriteDataDomainValue(OutData, Property->ResolveOutputAttributeName(PCGExMetaHelpers::SanitizeAttributeName(Entry.Source->Name)), *Property))
			{
				++NumWritten;
			}
		}
		return NumWritten;
	}
}
