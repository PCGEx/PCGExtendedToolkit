// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionPropertySetWriter.h"

#include "PCGExProperty.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

namespace PCGExCollections
{
	const FInstancedStruct* FindPrototypeProperty(const FName PropertyName, TConstArrayView<const UPCGExAssetCollection*> Collections)
	{
		for (const UPCGExAssetCollection* Collection : Collections)
		{
			if (!Collection)
			{
				continue;
			}
			if (const FInstancedStruct* Found = Collection->CollectionProperties.GetPropertyByName(PropertyName);
				Found && Found->IsValid())
			{
				return Found;
			}
		}
		return nullptr;
	}

	const FInstancedStruct* ResolveEntrySourceProperty(const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Host, const FName PropertyName)
	{
		if (Entry)
		{
			if (const FInstancedStruct* Override = Entry->PropertyOverrides.GetOverride(PropertyName);
				Override && Override->IsValid())
			{
				return Override;
			}
		}
		if (Host)
		{
			if (const FInstancedStruct* Default = Host->CollectionProperties.GetPropertyByName(PropertyName);
				Default && Default->IsValid())
			{
				return Default;
			}
		}
		return nullptr;
	}

	const FInstancedStruct* FPCGExCollectionPropertySetWriter::FCollectionPrototypeProvider::FindPrototypeProperty(const FName PropertyName) const
	{
		return PCGExCollections::FindPrototypeProperty(PropertyName, SearchOrder);
	}

	bool FPCGExCollectionPropertySetWriter::Initialize(
		FPCGExContext* InContext,
		const FPCGExPropertyOutputSettings& OutputSettings,
		const UPCGExAssetCollection* RootCollection,
		TConstArrayView<const UPCGExAssetCollection*> FallbackHosts,
		UPCGMetadata* Metadata)
	{
		// Build the search list onto our member provider (Inner holds a const* to it during writes,
		// so it has to outlive the call -- member storage is the right scope).
		Provider.SearchOrder.Reset();
		Provider.SearchOrder.Reserve(FallbackHosts.Num() + 1);
		if (RootCollection)
		{
			Provider.SearchOrder.Add(RootCollection);
		}
		for (const UPCGExAssetCollection* Host : FallbackHosts)
		{
			if (Host && Host != RootCollection)
			{
				Provider.SearchOrder.Add(Host);
			}
		}

		return Inner.Initialize(InContext, &Provider, OutputSettings, Metadata);
	}

	void WriteSchemaToDataDomain(
		FPCGExContext* InContext,
		const FPCGExPropertyOutputSettings& OutputSettings,
		const UPCGExAssetCollection* Host,
		UPCGData* InData)
	{
		if (!Host || !InData)
		{
			return;
		}

		TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
		OutputSettings.GetEffectiveConfigs(EffectiveConfigs);
		if (EffectiveConfigs.IsEmpty())
		{
			return;
		}

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

			const FInstancedStruct* Source = Host->CollectionProperties.GetPropertyByName(Config.PropertyName);
			if (!Source || !Source->IsValid())
			{
				continue;
			}

			const FPCGExProperty* SourceProp = Source->GetPtr<FPCGExProperty>();
			if (!SourceProp || !SourceProp->SupportsOutput())
			{
				continue;
			}

			// Type dispatch: pull the property's value as its declared output type and write it as
			// a single @Data attribute. SetDataValue handles attribute creation + default-value.
			PCGExMetaHelpers::ExecuteWithRightType(SourceProp->GetOutputType(), [&](auto Dummy)
			{
				using T_VALUE = decltype(Dummy);
				T_VALUE Value = T_VALUE{};
				if (SourceProp->TryGetValue<T_VALUE>(Value))
				{
					PCGExData::Helpers::SetDataValue<T_VALUE>(InData, OutputName, Value);
				}
			});
		}
	}

	void WriteSchemaToElementDomain(
		FPCGExContext* InContext,
		const FPCGExPropertyOutputSettings& OutputSettings,
		UPCGData* InData,
		TConstArrayView<const UPCGExAssetCollection*> PerRowHosts)
	{
		if (!InData || PerRowHosts.IsEmpty())
		{
			return;
		}

		TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
		OutputSettings.GetEffectiveConfigs(EffectiveConfigs);
		if (EffectiveConfigs.IsEmpty())
		{
			return;
		}

		// Build unique collection list once for prototype lookups (same pattern as Initialize's
		// SearchOrder). Skip if no hosts at all -- no prototype, no type info, nothing to declare.
		TArray<const UPCGExAssetCollection*> UniqueHosts;
		{
			TSet<const UPCGExAssetCollection*> Seen;
			UniqueHosts.Reserve(PerRowHosts.Num());
			for (const UPCGExAssetCollection* H : PerRowHosts)
			{
				if (H)
				{
					bool bAlreadySeen = false;
					Seen.Add(H, &bAlreadySeen);
					if (!bAlreadySeen)
					{
						UniqueHosts.Add(H);
					}
				}
			}
		}
		if (UniqueHosts.IsEmpty())
		{
			return;
		}

		// Build accessor keys once -- same uniform pattern as the identity-attr write block.
		TSharedPtr<IPCGAttributeAccessorKeys> Keys = PCGExMetaHelpers::MakeMutableKeys(InData);

		if (!Keys)
		{
			return;
		}

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

			const FInstancedStruct* Prototype = FindPrototypeProperty(Config.PropertyName, UniqueHosts);
			if (!Prototype)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext,
				           FText::FromString(FString::Printf(TEXT("Property '%s' not found in any source collection schema."), *Config.PropertyName.ToString())));
				continue;
			}

			const FPCGExProperty* ProtoProp = Prototype->GetPtr<FPCGExProperty>();
			if (!ProtoProp || !ProtoProp->SupportsOutput())
			{
				continue;
			}

			// Type dispatch: the prototype's GetOutputType drives the values array's T. Per-row,
			// pull the value from the row's own host schema (with N-to-N type coercion via the
			// FConversionTable inside TryGetValue) -- falls back to the prototype's value when the
			// row's host doesn't declare the property OR when the row's host is null.
			PCGExMetaHelpers::ExecuteWithRightType(ProtoProp->GetOutputType(), [&](auto Dummy)
			{
				using T_VALUE = decltype(Dummy);

				// Per-property null-host / missing-property default = prototype value.
				T_VALUE DefaultValue = T_VALUE{};
				ProtoProp->TryGetValue<T_VALUE>(DefaultValue);

				TArray<T_VALUE> Values;
				Values.Init(DefaultValue, PerRowHosts.Num());

				for (int32 r = 0; r < PerRowHosts.Num(); r++)
				{
					const UPCGExAssetCollection* RowHost = PerRowHosts[r];
					if (!RowHost)
					{
						continue;
					} // keep DefaultValue
					const FInstancedStruct* Source = RowHost->CollectionProperties.GetPropertyByName(Config.PropertyName);
					if (!Source || !Source->IsValid())
					{
						continue;
					}
					const FPCGExProperty* SrcProp = Source->GetPtr<FPCGExProperty>();
					if (!SrcProp)
					{
						continue;
					}
					T_VALUE V = T_VALUE{};
					if (SrcProp->TryGetValue<T_VALUE>(V))
					{
						Values[r] = V;
					}
				}

				UPCGMetadata* M = InData->MutableMetadata();
				if (!M)
				{
					return;
				}
				const FPCGAttributeIdentifier Id = PCGExMetaHelpers::GetAttributeIdentifier(OutputName, InData);
				M->FindOrCreateAttribute<T_VALUE>(Id, DefaultValue, false, true);

				FPCGAttributePropertyInputSelector Selector;
				Selector.Update(OutputName.ToString());
				Selector = Selector.CopyAndFixLast(InData);
				TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(InData, Selector);
				if (Accessor)
				{
					Accessor->SetRange<T_VALUE>(Values, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
				}
			});
		}
	}

	void FPCGExCollectionPropertySetWriter::WriteEntry(const int64 Key, const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Host)
	{
		// Per-entry source resolution doesn't fit Inner's provider-based WriteEntry shape (the
		// provider expects a SourceIndex into a fixed schema array, but ResolveEntrySourceProperty
		// has to look at both the entry's overrides and the host's defaults each call). Drive the
		// per-writer loop here and use the WriteAt primitive for the actual copy+write.
		for (int32 w = 0; w < Inner.Num(); w++)
		{
			const FName PropertyName = Inner.GetPropertyName(w);
			if (const FInstancedStruct* Source = ResolveEntrySourceProperty(Entry, Host, PropertyName))
			{
				Inner.WriteAt(w, Key, *Source);
			}
		}
	}
}
