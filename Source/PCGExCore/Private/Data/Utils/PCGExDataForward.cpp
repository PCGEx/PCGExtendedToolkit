// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExDataForward.h"

#include "PCGExLog.h"
#include "Algo/RemoveIf.h"
#include "Algo/Unique.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"

namespace PCGExData
{
	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const bool ElementDomainToDataDomain)
		: Details(InDetails)
		  , SourceDataFacade(InSourceDataFacade)
		  , TargetDataFacade(nullptr)
		  , bElementDomainToDataDomain(ElementDomainToDataDomain)
	{
		if (!Details.bEnabled)
		{
			return;
		}

		Details.Init();
		FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);
	}

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade, const bool ElementDomainToDataDomain)
		: Details(InDetails)
		  , SourceDataFacade(InSourceDataFacade)
		  , TargetDataFacade(InTargetDataFacade)
		  , bElementDomainToDataDomain(ElementDomainToDataDomain)
	{
		Details.Init();
		FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);

		const int32 NumAttributes = Identities.Num();
		Readers.Reserve(NumAttributes);
		Writers.Reserve(NumAttributes);

		// Init forwarded attributes on target
		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					// Typed path -- fast, directly typed buffers stored as IBuffer.
					using T = decltype(DummyValue);
					TSharedPtr<TBuffer<T>> Reader = SourceDataFacade->GetReadable<T>(Identity.GetIdentifier());
					if (!Reader)
					{
						return;
					}
					TSharedPtr<TBuffer<T>> Writer = TargetDataFacade->GetWritable<T>(Reader->InAttribute, EBufferInit::Inherit);
					if (!Writer)
					{
						return;
					}
					Readers.Add(Reader);
					Writers.Add(Writer);
				},
				[&]()
				{
					// Property-backed path -- extended/container types route through FFacade's
					// generic GetReadable/GetWritable which already fall back to FPropertyBuffer.
					if (!Identity.Attribute)
					{
						return;
					}
					TSharedPtr<IBuffer> Reader = SourceDataFacade->GetReadable(Identity, EIOSide::In, false);
					if (!Reader)
					{
						return;
					}
					TSharedPtr<IBuffer> Writer = TargetDataFacade->GetWritable(Identity.GetType(), Identity.Attribute, EBufferInit::Inherit);
					if (!Writer)
					{
						return;
					}
					Readers.Add(Reader);
					Writers.Add(Writer);
				});
		}
	}

	void FDataForwardHandler::ValidateIdentities(FValidateFn&& Fn)
	{
		Identities.SetNum(Algo::RemoveIf(Identities, [&Fn](const FAttributeIdentity& Identity)
		{
			return !Fn(Identity);
		}));
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const int32 TargetIndex)
	{
		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];
			if (!Readers.IsValidIndex(i) || !Writers.IsValidIndex(i))
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<TBuffer<T>> Reader = StaticCastSharedPtr<TBuffer<T>>(Readers[i]);
					TSharedPtr<TBuffer<T>> Writer = StaticCastSharedPtr<TBuffer<T>>(Writers[i]);
					Writer->SetValue(TargetIndex, Reader->Read(SourceIndex));
				},
				[&]()
				{
					// Property-backed: read source slot to scratch via property reflection, then write to target.
					// Both buffers cache the same FProperty; void* + property handle deep-copy.
					PCGExTypes::FScopedTypedValue Scratch = Readers[i]->MakeScopedValue();
					Readers[i]->ReadVoid(SourceIndex, Scratch);
					Writers[i]->SetVoid(TargetIndex, Scratch);
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade)
	{
		if (Identities.IsEmpty())
		{
			return;
		}

		const UPCGBasePointData* InSourceData = SourceDataFacade->GetIn();

		if (Details.bPreserveAttributesDefaultValue)
		{
			for (const FAttributeIdentity& Identity : Identities)
			{
				PCGExMetaHelpers::ExecuteWithRightType(
					Identity,
					[&](auto DummyValue)
					{
						using T = decltype(DummyValue);

						const FPCGMetadataAttributeBase* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.GetIdentifier());
						if (!SourceAtt)
						{
							return;
						}

						const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(InSourceData->GetMetadataEntry(SourceIndex));

						TSharedPtr<TBuffer<T>> Writer = nullptr;

						if (bElementDomainToDataDomain)
						{
							const FPCGAttributeIdentifier ToDataIdentifier(Identity.Name, PCGMetadataDomainID::Data);
							Writer = InTargetDataFacade->GetWritable<T>(ToDataIdentifier, EBufferInit::New);
						}
						else
						{
							Writer = InTargetDataFacade->GetWritable<T>(SourceAtt, EBufferInit::New);
						}

						if (!Writer)
						{
							return;
						}

						if (Writer->GetUnderlyingDomain() == EDomainType::Elements)
						{
							TSharedPtr<TArrayBuffer<T>> ElementsWriter = StaticCastSharedPtr<TArrayBuffer<T>>(Writer);
							TArray<T>& Values = *ElementsWriter->GetOutValues();
							for (T& Value : Values)
							{
								Value = ForwardValue;
							}
						}
						else
						{
							Writer->SetValue(0, ForwardValue);
						}
					},
					[&]()
					{
						// Property-backed: read source value via void*, get property-backed writer, fan-out to all slots.
						const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
						if (!SourceAtt)
						{
							return;
						}

						const PCGMetadataEntryKey SourceKey = Identity.InDataDomain() ? PCGDefaultValueKey : InSourceData->GetMetadataEntry(SourceIndex);
						const void* SrcAddr = SourceAtt->GetReadAddressFromEntryKey_Unsafe(SourceKey);
						if (!SrcAddr)
						{
							return;
						}

						// bElementDomainToDataDomain only matters for naming: same source attr, target identifier renamed.
						// GetWritable's IBuffer fallback takes the source attribute as template, but we override the identifier
						// via the buffer's domain. For simplicity in the property path, only support the same-domain case here.
						if (bElementDomainToDataDomain)
						{
							UE_LOG(LogPCGEx, Warning, TEXT("Element-to-Data domain conversion not supported on property-backed attribute '%s' -- skipped."), *Identity.Name.ToString());
							return;
						}

						TSharedPtr<IBuffer> Writer = InTargetDataFacade->GetWritable(Identity.GetType(), SourceAtt, EBufferInit::New);
						Helpers::PropertyBroadcastAttribute(SourceAtt, SourceKey, Writer);
					});
			}

			return;
		}

		for (const FAttributeIdentity& Identity : Identities)
		{
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					const FPCGMetadataAttributeBase* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.GetIdentifier());
					if (!SourceAtt)
					{
						return;
					}

					const T ForwardValue =
						Identity.InDataDomain()
						? Helpers::ReadDataValue<T>(SourceAtt)
						: SourceAtt->GetValueFromItemKey<T>(InSourceData->GetMetadataEntry(SourceIndex));

					const FPCGAttributeIdentifier Identifier =
						bElementDomainToDataDomain
						? FPCGAttributeIdentifier(Identity.Name, PCGMetadataDomainID::Data)
						: Identity.GetIdentifier();

					InTargetDataFacade->Source->DeleteAttribute(Identifier);

					FPCGMetadataAttributeBase* TargetAtt = InTargetDataFacade->Source->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation());

					if (bElementDomainToDataDomain)
					{
						Helpers::SetDataValue(TargetAtt, ForwardValue);
					}
				},
				[&]()
				{
					// Property-backed forward: read source via void*, recreate target attribute matching source desc, copy.
					const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
					if (!SourceAtt)
					{
						return;
					}

					const PCGMetadataEntryKey SourceKey = Identity.InDataDomain() ? PCGDefaultValueKey : InSourceData->GetMetadataEntry(SourceIndex);
					const void* SrcAddr = SourceAtt->GetReadAddressFromEntryKey_Unsafe(SourceKey);
					if (!SrcAddr)
					{
						return;
					}

					if (bElementDomainToDataDomain)
					{
						UE_LOG(LogPCGEx, Warning, TEXT("Element-to-Data domain conversion not supported on property-backed attribute '%s' -- skipped."), *Identity.Name.ToString());
						return;
					}

					const FPCGAttributeIdentifier Identifier = Identity.GetIdentifier();
					InTargetDataFacade->Source->DeleteAttribute(Identifier);

					TSharedPtr<IBuffer> Writer = InTargetDataFacade->GetWritable(Identity.GetType(), SourceAtt, EBufferInit::New);
					Helpers::PropertyBroadcastAttribute(SourceAtt, SourceKey, Writer);
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade, const TArray<int32>& Indices)
	{
		if (Identities.IsEmpty())
		{
			return;
		}

		const UPCGBasePointData* InSourceData = SourceDataFacade->GetIn();

		for (const FAttributeIdentity& Identity : Identities)
		{
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					const FPCGMetadataAttributeBase* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.GetIdentifier());
					if (!SourceAtt)
					{
						return;
					}

					const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(InSourceData->GetMetadataEntry(SourceIndex));

					TSharedPtr<TBuffer<T>> Writer = InTargetDataFacade->GetWritable<T>(SourceAtt, EBufferInit::Inherit);
					if (Writer->GetUnderlyingDomain() == EDomainType::Elements)
					{
						TSharedPtr<TArrayBuffer<T>> ElementsWriter = StaticCastSharedPtr<TArrayBuffer<T>>(Writer);
						TArray<T>& Values = *ElementsWriter->GetOutValues();
						for (int32 Index : Indices)
						{
							Values[Index] = ForwardValue;
						}
					}
					else
					{
						Writer->SetValue(0, ForwardValue);
					}
				},
				[&]()
				{
					// Property-backed: scatter source value to specified target indices via property reflection.
					const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
					if (!SourceAtt)
					{
						return;
					}

					const PCGMetadataEntryKey SourceKey = Identity.InDataDomain() ? PCGDefaultValueKey : InSourceData->GetMetadataEntry(SourceIndex);
					const void* SrcAddr = SourceAtt->GetReadAddressFromEntryKey_Unsafe(SourceKey);
					if (!SrcAddr)
					{
						return;
					}

					TSharedPtr<IBuffer> Writer = InTargetDataFacade->GetWritable(Identity.GetType(), SourceAtt, EBufferInit::Inherit);
					if (Writer && Writer->GetUnderlyingDomain() == EDomainType::Elements)
					{
						Helpers::PropertyScatterAttribute(SourceAtt, SourceKey, Writer, Indices);
					}
					else
					{
						Helpers::PropertyBroadcastAttribute(SourceAtt, SourceKey, Writer);
					}
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, UPCGMetadata* InTargetMetadata)
	{
		if (Identities.IsEmpty())
		{
			return;
		}

		const UPCGBasePointData* InSourceData = SourceDataFacade->GetIn();

		for (const FAttributeIdentity& Identity : Identities)
		{
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					const FPCGMetadataAttributeBase* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.GetIdentifier());
					if (!SourceAtt)
					{
						return;
					}

					const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(InSourceData->GetMetadataEntry(SourceIndex));

					const FPCGAttributeIdentifier Identifier = bElementDomainToDataDomain ? FPCGAttributeIdentifier(Identity.Name, PCGMetadataDomainID::Data) : Identity.GetIdentifier();

					InTargetMetadata->DeleteAttribute(Identifier);
					FPCGMetadataAttributeBase* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation(), true, true);
					if (bElementDomainToDataDomain)
					{
						Helpers::SetDataValue(TargetAtt, ForwardValue);
					}
				},
				[&]()
				{
					// Property-backed: create matching target attribute via desc, copy via PropertyCopyAttribute helper.
					const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
					if (!SourceAtt || !InTargetMetadata)
					{
						return;
					}

					const FPCGAttributeIdentifier Identifier = bElementDomainToDataDomain
						? FPCGAttributeIdentifier(Identity.Name, PCGMetadataDomainID::Data)
						: Identity.GetIdentifier();

					InTargetMetadata->DeleteAttribute(Identifier);
					FPCGMetadataAttributeBase* TargetAtt = InTargetMetadata->CreateAttribute(
						Identifier, SourceAtt->GetAttributeDesc(), SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/true);
					if (!TargetAtt)
					{
						return;
					}

					const PCGMetadataEntryKey SourceKey = Identity.InDataDomain() ? PCGDefaultValueKey : InSourceData->GetMetadataEntry(SourceIndex);
					Helpers::PropertyCopyAttribute(SourceAtt, SourceKey, TargetAtt, PCGDefaultValueKey);
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, UPCGMetadata* InTargetMetadata, const int64 TargetKey)
	{
		if (Identities.IsEmpty())
		{
			return;
		}

		const UPCGBasePointData* InSourceData = SourceDataFacade->GetIn();

		for (const FAttributeIdentity& Identity : Identities)
		{
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					const FPCGMetadataAttributeBase* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.GetIdentifier());
					if (!SourceAtt)
					{
						return;
					}

					const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(InSourceData->GetMetadataEntry(SourceIndex));

					// Single target entry on the element (per-row) domain. Find-or-create (not delete+create):
					// successive calls forward distinct rows into the same target metadata.
					const FPCGAttributeIdentifier TargetIdentifier(Identity.Name);
					FPCGMetadataAttribute<T>* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(TargetIdentifier, T{}, SourceAtt->AllowsInterpolation());
					if (TargetAtt)
					{
						TargetAtt->SetValue(TargetKey, ForwardValue);
					}
				},
				[&]()
				{
					// Property-backed (Struct/Enum/Object/container) source -> deep-copy the single source value onto TargetKey.
					const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
					if (!SourceAtt || !InTargetMetadata)
					{
						return;
					}

					const FPCGAttributeIdentifier TargetIdentifier(Identity.Name);
					FPCGMetadataAttributeBase* TargetAtt = InTargetMetadata->GetMutableAttribute(TargetIdentifier);
					if (!TargetAtt)
					{
						TargetAtt = InTargetMetadata->CreateAttribute(TargetIdentifier, SourceAtt->GetAttributeDesc(), SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/true);
					}
					if (!TargetAtt)
					{
						return;
					}

					const PCGMetadataEntryKey SourceKey = Identity.InDataDomain() ? PCGDefaultValueKey : InSourceData->GetMetadataEntry(SourceIndex);
					Helpers::PropertyCopyAttribute(SourceAtt, SourceKey, TargetAtt, TargetKey);
				});
		}
	}
}
