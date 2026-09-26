// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExDataForward.h"

#include "Core/PCGExMTCommon.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"

namespace PCGExData
{
	FPCGAttributeIdentifier FDataForwardHandler::GetTargetIdentifier(const FAttributeIdentity& Identity) const
	{
		return Domain == EForwardDomain::ToData ? FPCGAttributeIdentifier(Identity.Identifier.Name, PCGMetadataDomainID::Data) : Identity.Identifier;
	}

	bool FDataForwardHandler::RedirectsDomain(const FAttributeIdentity& Identity) const
	{
		return Domain != EForwardDomain::Inherit && !(GetTargetIdentifier(Identity) == Identity.Identifier);
	}

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const EForwardDomain InDomain)
		: Details(InDetails)
		  , SourceDataFacade(InSourceDataFacade)
		  , TargetDataFacade(nullptr)
		  , Domain(InDomain)
	{
		if (!Details.bEnabled)
		{
			return;
		}

		Details.Init();
		FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);
	}

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade, const EForwardDomain InDomain)
		: Details(InDetails)
		  , SourceDataFacade(InSourceDataFacade)
		  , TargetDataFacade(InTargetDataFacade)
		  , Domain(InDomain)
	{
		Details.Init();
		FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);

		const int32 NumAttributes = Identities.Num();

		// Index-aligned with Identities (null on failure): Forward casts Readers[i]/Writers[i] by Identities[i]'s type.
		Readers.Init(nullptr, NumAttributes);
		Writers.Init(nullptr, NumAttributes);

		// Init forwarded attributes on target
		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				TSharedPtr<TBuffer<T>> Reader = SourceDataFacade->GetReadable<T>(Identity.Identifier);
				if (!Reader)
				{
					return;
				}

				// The source attribute, not Reader->GetTypedInAttribute(): a reader first built as a broadcaster never sets it.
				const FPCGMetadataAttribute<T>* InAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(SourceDataFacade->GetIn(), Identity.Identifier);
				if (!InAttribute)
				{
					return;
				}

				TSharedPtr<TBuffer<T>> Writer = nullptr;
				if (RedirectsDomain(Identity))
				{
					const T DefaultValue = Identity.InDataDomain() ? Helpers::ReadDataValue(InAttribute) : InAttribute->GetValueFromItemKey(PCGDefaultValueKey);
					Writer = TargetDataFacade->GetWritable<T>(GetTargetIdentifier(Identity), DefaultValue, InAttribute->AllowsInterpolation(), EBufferInit::Inherit);
				}
				else
				{
					Writer = TargetDataFacade->GetWritable<T>(InAttribute, EBufferInit::Inherit);
				}

				if (!Writer)
				{
					return;
				}

				Readers[i] = Reader;
				Writers[i] = Writer;
			});
		}
	}

	void FDataForwardHandler::ValidateIdentities(FValidateFn&& Fn)
	{
		// Readers/Writers exist only on prepared (two-facade) handlers and must stay index-aligned with Identities.
		const bool bPrepared = Readers.Num() == Identities.Num();
		int32 WriteIndex = 0;

		for (int32 i = 0; i < Identities.Num(); i++)
		{
			if (!Fn(Identities[i]))
			{
				continue;
			}

			if (WriteIndex != i)
			{
				Identities[WriteIndex] = Identities[i];
				if (bPrepared)
				{
					Readers[WriteIndex] = Readers[i];
					Writers[WriteIndex] = Writers[i];
				}
			}

			WriteIndex++;
		}

		Identities.SetNum(WriteIndex);
		if (bPrepared)
		{
			Readers.SetNum(WriteIndex);
			Writers.SetNum(WriteIndex);
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const int32 TargetIndex)
	{
		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];
			if (!Readers.IsValidIndex(i) || !Readers[i] || !Writers[i])
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				TSharedPtr<TBuffer<T>> Reader = StaticCastSharedPtr<TBuffer<T>>(Readers[i]);
				TSharedPtr<TBuffer<T>> Writer = StaticCastSharedPtr<TBuffer<T>>(Writers[i]);
				// A @Data writer (ToData policy, or an inherited @Data source) has a single slot
				Writer->SetValue(Writer->GetUnderlyingDomain() == EDomainType::Elements ? TargetIndex : 0, Reader->Read(SourceIndex));
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

					if (RedirectsDomain(Identity))
					{
						Writer = InTargetDataFacade->GetWritable<T>(GetTargetIdentifier(Identity), EBufferInit::New);
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

				const FPCGAttributeIdentifier Identifier = GetTargetIdentifier(Identity);

				InTargetDataFacade->Source->DeleteAttribute(Identifier);
				FPCGMetadataAttribute<T>* TargetAtt = InTargetDataFacade->Source->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation());

				// Data-domain targets: SetDataValue writes the default-value slot -- the canonical
				// @Data store (FindOrCreateAttribute alone would miss the update when the attribute
				// already existed with a different default).
				if (TargetAtt && (Domain == EForwardDomain::ToData || Identity.InDataDomain()))
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

				const FPCGAttributeIdentifier Identifier = GetTargetIdentifier(Identity);

				InTargetMetadata->DeleteAttribute(Identifier);
				FPCGMetadataAttribute<T>* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation(), true, true);

				// Same rationale as the facade overload above: data-domain targets get a real entry.
				if (TargetAtt && (Domain == EForwardDomain::ToData || Identity.InDataDomain()))
				{
					Helpers::SetDataValue(TargetAtt, ForwardValue);
				}
			});
		}
	}

	void FDataForwardHandler::ForwardToCopies(const TConstArrayView<int32> SourceIndices, UPCGBasePointData* Target, const int32 Stride) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDataForwardHandler::ForwardToCopies);

		const int32 NumCopies = SourceIndices.Num();
		if (Identities.IsEmpty() || !Target || !Target->Metadata || NumCopies <= 0 || Stride <= 0)
		{
			return;
		}

		const UPCGBasePointData* InSourceData = SourceDataFacade->GetIn();

		// SetValuesFromValueKeys has no value-range overload here, so the target entries are gathered once.
		TArray<PCGMetadataEntryKey> TargetEntries;
		{
			const TConstPCGValueRange<int64> EntryRange = Target->GetConstMetadataEntryValueRange();
			check(EntryRange.Num() == NumCopies * Stride);
			TargetEntries.SetNumUninitialized(EntryRange.Num());
			for (int32 i = 0; i < TargetEntries.Num(); i++) { TargetEntries[i] = EntryRange[i]; }
		}

		// Attribute creation mutates the domain's attribute map, so it stays serial.
		const int32 NumAttributes = Identities.Num();
		TArray<const FPCGMetadataAttributeBase*> SourceAttributes;
		TArray<FPCGMetadataAttributeBase*> TargetAttributes;
		SourceAttributes.Init(nullptr, NumAttributes);
		TargetAttributes.Init(nullptr, NumAttributes);

		for (int32 a = 0; a < NumAttributes; a++)
		{
			PCGExMetaHelpers::ExecuteWithRightType(Identities[a].UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				SourceAttributes[a] = PCGExMetaHelpers::TryGetConstAttribute<T>(InSourceData, Identities[a].Identifier);
			});
		}

		// A name on both @Data and Elements maps to one attribute: the later identity wins, as on per-target copies.
		TMap<FName, int32> WinnerByName;
		WinnerByName.Reserve(NumAttributes);
		for (int32 a = 0; a < NumAttributes; a++)
		{
			if (SourceAttributes[a])
			{
				WinnerByName.Add(Identities[a].Identifier.Name, a);
			}
		}

		for (int32 a = 0; a < NumAttributes; a++)
		{
			const FAttributeIdentity& Identity = Identities[a];
			if (!SourceAttributes[a] || WinnerByName.FindChecked(Identity.Identifier.Name) != a)
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				const FPCGMetadataAttribute<T>* SourceAtt = static_cast<const FPCGMetadataAttribute<T>*>(SourceAttributes[a]);

				const FPCGAttributeIdentifier Identifier(Identity.Identifier.Name, PCGMetadataDomainID::Elements);
				Target->Metadata->DeleteAttribute(Identifier);

				// Never parented: an entry reset to the default must not fall through to a same-named source attribute.
				const T DefaultValue = Identity.InDataDomain() ? Helpers::ReadDataValue(SourceAtt) : SourceAtt->GetValueFromItemKey(PCGDefaultValueKey);
				TargetAttributes[a] = Target->Metadata->FindOrCreateAttribute<T>(Identifier, DefaultValue, SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/false, /*bOverwriteIfTypeMismatch=*/true);
			});
		}

		// One task per attribute: each attribute owns its value and entry locks.
		PCGExMT::ParallelOrSequential(
			NumAttributes, [&](const int32 a)
			{
				FPCGMetadataAttributeBase* TargetAtt = TargetAttributes[a];
				const FPCGMetadataAttributeBase* SourceAtt = SourceAttributes[a];
				if (!TargetAtt || !SourceAtt)
				{
					return;
				}

				const FAttributeIdentity& Identity = Identities[a];
				const bool bDataSource = Identity.InDataDomain();
				const PCGMetadataEntryKey DataEntry = bDataSource ? Helpers::GetDataValueKey(SourceAtt) : PCGInvalidEntryKey;

				// Copies reading the same source value share the value written once on the first such copy's first point.
				// Value keys are unique across an attribute's parent chain (child keys start at the parent's value count).
				TArray<PCGMetadataEntryKey> CopySourceEntries;
				TArray<int32> CopyWriter;
				TArray<int32> WritingCopies;
				CopySourceEntries.SetNumUninitialized(NumCopies);
				CopyWriter.SetNumUninitialized(NumCopies);

				TMap<PCGMetadataValueKey, int32> WriterBySourceValue;
				for (int32 k = 0; k < NumCopies; k++)
				{
					const PCGMetadataEntryKey SourceEntry = bDataSource ? DataEntry : InSourceData->GetMetadataEntry(SourceIndices[k]);
					const PCGMetadataValueKey SourceValue = SourceAtt->GetValueKey(SourceEntry);
					CopySourceEntries[k] = SourceEntry;

					if (const int32* ExistingWriter = WriterBySourceValue.Find(SourceValue))
					{
						CopyWriter[k] = *ExistingWriter;
						continue;
					}

					WriterBySourceValue.Add(SourceValue, k);
					CopyWriter[k] = k;
					WritingCopies.Add(k);
				}

				TArray<PCGMetadataEntryKey> WriterEntries;
				WriterEntries.SetNumUninitialized(WritingCopies.Num());
				for (int32 w = 0; w < WritingCopies.Num(); w++)
				{
					WriterEntries[w] = TargetEntries[WritingCopies[w] * Stride];
				}

				PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					const FPCGMetadataAttribute<T>* TypedSource = static_cast<const FPCGMetadataAttribute<T>*>(SourceAtt);

					TArray<T> WriterValues;
					WriterValues.SetNum(WritingCopies.Num());
					for (int32 w = 0; w < WritingCopies.Num(); w++)
					{
						WriterValues[w] = TypedSource->GetValueFromItemKey(CopySourceEntries[WritingCopies[w]]);
					}

					static_cast<FPCGMetadataAttribute<T>*>(TargetAtt)->SetValues(TArrayView<const PCGMetadataEntryKey>(WriterEntries), TArrayView<const T>(WriterValues));
				});

				TArray<PCGMetadataValueKey> WriterValueKeys;
				WriterValueKeys.SetNumUninitialized(NumCopies);
				for (int32 w = 0; w < WritingCopies.Num(); w++)
				{
					WriterValueKeys[WritingCopies[w]] = TargetAtt->GetValueKey(WriterEntries[w]);
				}

				TArray<PCGMetadataValueKey> PointValueKeys;
				PointValueKeys.SetNumUninitialized(NumCopies * Stride);
				for (int32 k = 0; k < NumCopies; k++)
				{
					const PCGMetadataValueKey CopyValueKey = WriterValueKeys[CopyWriter[k]];
					for (int32 i = 0, o = k * Stride; i < Stride; i++, o++)
					{
						PointValueKeys[o] = CopyValueKey;
					}
				}

				TargetAtt->SetValuesFromValueKeys(TArrayView<const PCGMetadataEntryKey>(TargetEntries), TArrayView<const PCGMetadataValueKey>(PointValueKeys));
			}, 2, EParallelForFlags::Unbalanced);
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
