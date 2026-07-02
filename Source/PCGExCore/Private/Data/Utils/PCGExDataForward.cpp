// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExDataForward.h"

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

			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				TSharedPtr<TBuffer<T>> Reader = SourceDataFacade->GetReadable<T>(Identity.Identifier);
				TSharedPtr<TBuffer<T>> Writer = TargetDataFacade->GetWritable<T>(Reader->GetTypedInAttribute(), EBufferInit::Inherit);

				if (!Reader || !Writer)
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

			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				TSharedPtr<TBuffer<T>> Reader = StaticCastSharedPtr<TBuffer<T>>(Readers[i]);
				TSharedPtr<TBuffer<T>> Writer = StaticCastSharedPtr<TBuffer<T>>(Writers[i]);
				Writer->SetValue(TargetIndex, Reader->Read(SourceIndex));
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
				PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					const FPCGMetadataAttribute<T>* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.Identifier);
					if (!SourceAtt)
					{
						return;
					}

					const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(InSourceData->GetMetadataEntry(SourceIndex));

					TSharedPtr<TBuffer<T>> Writer = nullptr;

					if (bElementDomainToDataDomain)
					{
						const FPCGAttributeIdentifier ToDataIdentifier(Identity.Identifier.Name, PCGMetadataDomainID::Data);
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
				});
			}

			return;
		}

		for (const FAttributeIdentity& Identity : Identities)
		{
			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				const FPCGMetadataAttribute<T>* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.Identifier);
				if (!SourceAtt)
				{
					return;
				}

				const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(InSourceData->GetMetadataEntry(SourceIndex));

				const FPCGAttributeIdentifier Identifier = bElementDomainToDataDomain ? FPCGAttributeIdentifier(Identity.Identifier.Name, PCGMetadataDomainID::Data) : Identity.Identifier;

				InTargetDataFacade->Source->DeleteAttribute(Identifier);
				FPCGMetadataAttribute<T>* TargetAtt = InTargetDataFacade->Source->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation());

				// Data-domain targets: SetDataValue writes the default-value slot -- the canonical
				// @Data store (FindOrCreateAttribute alone would miss the update when the attribute
				// already existed with a different default).
				if (TargetAtt && (bElementDomainToDataDomain || Identity.InDataDomain()))
				{
					Helpers::SetDataValue(TargetAtt, ForwardValue);
				}
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
			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				const FPCGMetadataAttribute<T>* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.Identifier);
				if (!SourceAtt)
				{
					return;
				}

				const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(InSourceData->GetMetadataEntry(SourceIndex));

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
			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				const FPCGMetadataAttribute<T>* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.Identifier);
				if (!SourceAtt)
				{
					return;
				}

				const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(InSourceData->GetMetadataEntry(SourceIndex));

				const FPCGAttributeIdentifier Identifier = bElementDomainToDataDomain ? FPCGAttributeIdentifier(Identity.Identifier.Name, PCGMetadataDomainID::Data) : Identity.Identifier;

				InTargetMetadata->DeleteAttribute(Identifier);
				FPCGMetadataAttribute<T>* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation(), true, true);

				// Same rationale as the facade overload above: data-domain targets get a real entry.
				if (TargetAtt && (bElementDomainToDataDomain || Identity.InDataDomain()))
				{
					Helpers::SetDataValue(TargetAtt, ForwardValue);
				}
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
			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);

				const FPCGMetadataAttribute<T>* SourceAtt = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identity.Identifier);
				if (!SourceAtt)
				{
					return;
				}

				const T ForwardValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(InSourceData->GetMetadataEntry(SourceIndex));

				// Single target entry on the element (per-row) domain.
				const FPCGAttributeIdentifier TargetIdentifier(Identity.Identifier.Name);
				FPCGMetadataAttribute<T>* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(TargetIdentifier, T{}, SourceAtt->AllowsInterpolation());
				if (TargetAtt)
				{
					TargetAtt->SetValue(TargetKey, ForwardValue);
				}
			});
		}
	}
}
