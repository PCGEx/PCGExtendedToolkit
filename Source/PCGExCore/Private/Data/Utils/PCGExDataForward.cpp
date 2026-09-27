// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExDataForward.h"

#include "PCGExLog.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Core/PCGExMTCommon.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

namespace PCGExData
{
	FPCGAttributeIdentifier FDataForwardHandler::GetTargetIdentifier(const FAttributeIdentity& Identity) const
	{
		return Domain == EForwardDomain::ToData ? FPCGAttributeIdentifier(Identity.Name, PCGMetadataDomainID::Data) : Identity.GetIdentifier();
	}

	bool FDataForwardHandler::RedirectsDomain(const FAttributeIdentity& Identity) const
	{
		return Domain != EForwardDomain::Inherit && !(GetTargetIdentifier(Identity) == Identity.GetIdentifier());
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

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade, const EForwardDomain InDomain, const TSet<FName>* InIgnoredAttributes)
		: Details(InDetails)
		  , SourceDataFacade(InSourceDataFacade)
		  , TargetDataFacade(InTargetDataFacade)
		  , Domain(InDomain)
	{
		Details.Init();
		FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities, InIgnoredAttributes);
		Details.Filter(Identities);

		const int32 NumAttributes = Identities.Num();

		// Readers/Writers stay index-aligned with Identities (null on failure) -- the typed Forward
		// paths cast by Identities[i]'s type, so a compacted array would cast to the wrong type.
		Readers.Init(nullptr, NumAttributes);
		Writers.Init(nullptr, NumAttributes);

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
					// Identity.Attribute, not Reader->InAttribute: a reader first built as a broadcaster never sets InAttribute.
					const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
					if (!SourceAtt)
					{
						return;
					}
					TSharedPtr<TBuffer<T>> Reader = SourceDataFacade->GetReadable<T>(Identity.GetIdentifier());
					if (!Reader)
					{
						return;
					}
					TSharedPtr<TBuffer<T>> Writer = nullptr;
					if (RedirectsDomain(Identity))
					{
						const T DefaultValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(PCGDefaultValueKey);
						Writer = TargetDataFacade->GetWritable<T>(GetTargetIdentifier(Identity), DefaultValue, SourceAtt->AllowsInterpolation(), EBufferInit::Inherit);
					}
					else
					{
						Writer = TargetDataFacade->GetWritable<T>(SourceAtt, EBufferInit::Inherit);
					}
					if (!Writer)
					{
						return;
					}
					Readers[i] = Reader;
					Writers[i] = Writer;
				},
				[&]()
				{
					// Property-backed path -- extended/container types route through FFacade's
					// generic GetReadable/GetWritable which already fall back to FPropertyBuffer.
					if (!Identity.Attribute)
					{
						return;
					}
					if (RedirectsDomain(Identity))
					{
						UE_LOG(LogPCGEx, Warning, TEXT("Domain conversion not supported on property-backed attribute '%s' -- skipped."), *Identity.Name.ToString());
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
					Readers[i] = Reader;
					Writers[i] = Writer;
				});
		}
	}

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const UPCGMetadata* InSourceMetadata, const TSharedPtr<FFacade>& InTargetDataFacade, const TSet<FName>* InIgnoredAttributes)
		: Details(InDetails)
		  , SourceDataFacade(nullptr)
		  , TargetDataFacade(InTargetDataFacade)
		  , Domain(EForwardDomain::Inherit)
	{
		Details.Init();
		FAttributeIdentity::Get(InSourceMetadata, Identities, InIgnoredAttributes);
		Details.Filter(Identities);

		// ForwardEntry reads the source attribute by entry key, which only the Elements domain has.
		for (int i = Identities.Num() - 1; i >= 0; i--)
		{
			if (Identities[i].InDataDomain() || !Identities[i].Attribute)
			{
				Identities.RemoveAt(i);
			}
		}

		const int32 NumAttributes = Identities.Num();

		// Writers stay index-aligned with Identities (null on failure); no Readers on a metadata-sourced handler.
		Writers.Init(nullptr, NumAttributes);

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					Writers[i] = TargetDataFacade->GetWritable<T>(Identity.Attribute, EBufferInit::Inherit);
				},
				[&]()
				{
					// No entry-key read path for extended/container values outside a facade.
					UE_LOG(LogPCGEx, Warning, TEXT("Attribute '%s' is a container or extended type and cannot be forwarded from sampled metadata -- skipped."), *Identity.Name.ToString());
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

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<TBuffer<T>> Reader = StaticCastSharedPtr<TBuffer<T>>(Readers[i]);
					TSharedPtr<TBuffer<T>> Writer = StaticCastSharedPtr<TBuffer<T>>(Writers[i]);
					// A @Data writer (ToData policy, or an inherited @Data source) has a single slot
					Writer->SetValue(Writer->GetUnderlyingDomain() == EDomainType::Elements ? TargetIndex : 0, Reader->Read(SourceIndex));
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

	void FDataForwardHandler::ForwardEntry(const PCGMetadataEntryKey SourceKey, const int32 TargetIndex)
	{
		if (SourceKey == PCGInvalidEntryKey)
		{
			return;
		}

		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			if (!Writers.IsValidIndex(i) || !Writers[i])
			{
				continue;
			}

			const FAttributeIdentity& Identity = Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					// Reads through the child attribute: an inherited entry resolves to the parent's value.
					StaticCastSharedPtr<TBuffer<T>>(Writers[i])->SetValue(TargetIndex, Identity.Attribute->GetValueFromItemKey<T>(SourceKey));
				});
		}
	}

	void FDataForwardHandler::ForwardEntries(TConstArrayView<PCGMetadataEntryKey> SourceKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDataForwardHandler::ForwardEntries);
		ForwardEntriesScoped(PCGExMT::FScope(0, SourceKeys.Num()), SourceKeys);
	}

	void FDataForwardHandler::ForwardEntriesScoped(const PCGExMT::FScope& Scope, const TConstArrayView<PCGMetadataEntryKey> SourceKeyPerTarget) const
	{
		check(SourceKeyPerTarget.Num() >= Scope.End)

		if (Identities.IsEmpty())
		{
			return;
		}

		// Compacted once per scope: every attribute resolves the same key set.
		TArray<int32> TargetIndices;
		TArray<PCGMetadataEntryKey> ValidKeys;
		TargetIndices.Reserve(Scope.Count);
		ValidKeys.Reserve(Scope.Count);

		for (int32 t = Scope.Start; t < Scope.End; t++)
		{
			const PCGMetadataEntryKey Key = SourceKeyPerTarget[t];
			if (Key != PCGInvalidEntryKey)
			{
				TargetIndices.Add(t);
				ValidKeys.Add(Key);
			}
		}

		if (ValidKeys.IsEmpty())
		{
			return;
		}

		const int32 NumAttributes = Identities.Num();
		TArray<PCGMetadataValueKey> ValueKeys;

		for (int i = 0; i < NumAttributes; i++)
		{
			if (!Writers.IsValidIndex(i) || !Writers[i])
			{
				continue;
			}

			const FAttributeIdentity& Identity = Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TBuffer<T>* Writer = static_cast<TBuffer<T>*>(Writers[i].Get());

					if (Writer->GetUnderlyingDomain() != EDomainType::Elements)
					{
						// Single slot: the scope's last valid key wins, as it does when forwarding entry by entry.
						Writer->SetValue(0, Identity.Attribute->GetValueFromItemKey<T>(ValidKeys.Last()));
						return;
					}

					// Reads through the child attribute: an inherited entry resolves to the parent's value, one lock per attribute.
					ValueKeys.Reset();
					Identity.Attribute->GetValueKeys(TConstArrayView<PCGMetadataEntryKey>(ValidKeys), ValueKeys);

					TArray<T> Values;
					Values.SetNum(ValueKeys.Num());
					Identity.Attribute->GetValues<T>(TConstArrayView<PCGMetadataValueKey>(ValueKeys), TArrayView<T>(Values));

					T* Out = static_cast<TArrayBuffer<T>*>(Writer)->GetOutValues()->GetData();
					for (int32 k = 0; k < TargetIndices.Num(); k++)
					{
						Out[TargetIndices[k]] = MoveTemp(Values[k]);
					}
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const TArray<int32>& Indices)
	{
		// Prepared-target variant (requires the target-facade constructor): fans one source row out to
		// many target indices through the pre-created writers. No lazy buffer/attribute creation happens
		// here, so this is safe to call from concurrent tasks once the handler is built.
		if (Indices.IsEmpty())
		{
			return;
		}

		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];
			if (!Readers.IsValidIndex(i) || !Readers[i] || !Writers[i])
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

					const T ForwardValue = Reader->Read(SourceIndex);

					if (Writer->GetUnderlyingDomain() == EDomainType::Elements)
					{
						TSharedPtr<TArrayBuffer<T>> ElementsWriter = StaticCastSharedPtr<TArrayBuffer<T>>(Writer);
						TArray<T>& Values = *ElementsWriter->GetOutValues();
						for (const int32 TargetIndex : Indices)
						{
							Values[TargetIndex] = ForwardValue;
						}
					}
					else
					{
						Writer->SetValue(0, ForwardValue);
					}
				},
				[&]()
				{
					// Property-backed: read the source slot once to scratch, deep-copy onto every target.
					PCGExTypes::FScopedTypedValue Scratch = Readers[i]->MakeScopedValue();
					Readers[i]->ReadVoid(SourceIndex, Scratch);
					for (const int32 TargetIndex : Indices)
					{
						Writers[i]->SetVoid(TargetIndex, Scratch);
					}
				});
		}
	}

	void FDataForwardHandler::ForwardScoped(const PCGExMT::FScope& Scope, const TConstArrayView<int32> SourceIndexPerTarget)
	{
		check(SourceIndexPerTarget.Num() >= Scope.End)

		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const FAttributeIdentity& Identity = Identities[i];
			if (!Readers.IsValidIndex(i) || !Readers[i] || !Writers[i])
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TBuffer<T>* Reader = static_cast<TBuffer<T>*>(Readers[i].Get());
					TBuffer<T>* Writer = static_cast<TBuffer<T>*>(Writers[i].Get());

					if (Writer->GetUnderlyingDomain() != EDomainType::Elements)
					{
						// Single slot: the scope's last forwarded row wins, as it does when forwarding point by point.
						for (int32 t = Scope.End - 1; t >= Scope.Start; t--)
						{
							const int32 Row = SourceIndexPerTarget[t];
							if (Row >= 0)
							{
								Writer->SetValue(0, Reader->Read(Row));
								break;
							}
						}
						return;
					}

					T* Out = static_cast<TArrayBuffer<T>*>(Writer)->GetOutValues()->GetData();

					// Raw array only for a fully read Elements reader; a sparse one is fetched per scope of another data.
					if (Reader->GetUnderlyingDomain() == EDomainType::Elements && !Reader->IsSparse())
					{
						const T* In = static_cast<TArrayBuffer<T>*>(Reader)->GetInValues()->GetData();
						for (int32 t = Scope.Start; t < Scope.End; t++)
						{
							const int32 Row = SourceIndexPerTarget[t];
							if (Row >= 0)
							{
								Out[t] = In[Row];
							}
						}
					}
					else
					{
						for (int32 t = Scope.Start; t < Scope.End; t++)
						{
							const int32 Row = SourceIndexPerTarget[t];
							if (Row >= 0)
							{
								Out[t] = Reader->Read(Row);
							}
						}
					}
				},
				[&]()
				{
					PCGExTypes::FScopedTypedValue Scratch = Readers[i]->MakeScopedValue();
					for (int32 t = Scope.Start; t < Scope.End; t++)
					{
						const int32 Row = SourceIndexPerTarget[t];
						if (Row < 0)
						{
							continue;
						}
						Readers[i]->ReadVoid(Row, Scratch);
						Writers[i]->SetVoid(t, Scratch);
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
		const TConstPCGValueRange<int64> TargetEntries = Target->GetConstMetadataEntryValueRange();
		check(TargetEntries.Num() == NumCopies * Stride);

		// Attribute creation mutates the domain's attribute map, so it stays serial.
		const int32 NumAttributes = Identities.Num();
		TArray<FPCGMetadataAttributeBase*> TargetAttributes;
		TargetAttributes.Init(nullptr, NumAttributes);

		// A name on both @Data and Elements maps to one attribute: the later identity wins, as on per-target copies.
		TMap<FName, int32> WinnerByName;
		WinnerByName.Reserve(NumAttributes);
		for (int32 a = 0; a < NumAttributes; a++)
		{
			if (Identities[a].Attribute)
			{
				WinnerByName.Add(Identities[a].Name, a);
			}
		}

		for (int32 a = 0; a < NumAttributes; a++)
		{
			const FAttributeIdentity& Identity = Identities[a];
			const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
			if (!SourceAtt || WinnerByName.FindChecked(Identity.Name) != a)
			{
				continue;
			}

			const FPCGAttributeIdentifier Identifier(Identity.Name, PCGMetadataDomainID::Elements);
			if (PCGExMetaHelpers::HasAttribute(Target->Metadata, Identifier))
			{
				Target->Metadata->DeleteAttribute(Identifier);
			}

			// Never parented: an entry reset to the default must not fall through to a same-named source attribute.
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					const T DefaultValue = Identity.InDataDomain() ? Helpers::ReadDataValue<T>(SourceAtt) : SourceAtt->GetValueFromItemKey<T>(PCGDefaultValueKey);
					TargetAttributes[a] = Target->Metadata->FindOrCreateAttribute<T>(Identifier, DefaultValue, SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/false, /*bOverwriteIfTypeMismatch=*/true);
				},
				[&]()
				{
					TargetAttributes[a] = Target->Metadata->CreateAttribute(Identifier, SourceAtt->GetAttributeDesc(), SourceAtt->AllowsInterpolation(), /*bOverrideParent=*/false);
				});
		}

		FPCGMetadataDomain* TargetDomain = Target->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);

		// One task per attribute: each attribute owns its value and entry locks.
		PCGExMT::ParallelOrSequential(
			NumAttributes, [&](const int32 a)
			{
				FPCGMetadataAttributeBase* TargetAtt = TargetAttributes[a];
				if (!TargetAtt)
				{
					return;
				}

				const FAttributeIdentity& Identity = Identities[a];
				const FPCGMetadataAttributeBase* SourceAtt = Identity.Attribute;
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

				PCGExMetaHelpers::ExecuteWithRightType(
					Identity,
					[&](auto DummyValue)
					{
						using T = decltype(DummyValue);
						TArray<T> WriterValues;
						WriterValues.SetNum(WritingCopies.Num());
						for (int32 w = 0; w < WritingCopies.Num(); w++)
						{
							WriterValues[w] = SourceAtt->GetValueFromItemKey<T>(CopySourceEntries[WritingCopies[w]]);
						}

						// Accessor, not SetValues<T>: compressed types dedup through a hash instead of AddUnique.
						const TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(TargetAtt, TargetDomain);
						if (!ensure(Accessor.IsValid()))
						{
							return;
						}
						FPCGAttributeAccessorKeysEntries WriterKeys(MakeArrayView(WriterEntries));
						Accessor->SetRange<T>(TArrayView<const T>(WriterValues), 0, WriterKeys);
					},
					[&]()
					{
						for (int32 w = 0; w < WritingCopies.Num(); w++)
						{
							TargetAtt->SetValue(WriterEntries[w], SourceAtt, CopySourceEntries[WritingCopies[w]]);
						}
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

				TargetAtt->SetValuesFromValueKeys(TargetEntries, TArrayView<const PCGMetadataValueKey>(PointValueKeys));
			}, 2, EParallelForFlags::Unbalanced);
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

						// Inherit only: a second same-named forward would share or orphan the deduplicated property buffer.
						if (Domain != EForwardDomain::Inherit)
						{
							UE_LOG(LogPCGEx, Warning, TEXT("Property-backed attribute '%s' is only forwarded onto a facade with the Inherit domain -- skipped."), *Identity.Name.ToString());
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

					const FPCGAttributeIdentifier Identifier = GetTargetIdentifier(Identity);

					InTargetDataFacade->Source->DeleteAttribute(Identifier);

					FPCGMetadataAttributeBase* TargetAtt = InTargetDataFacade->Source->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation());

					if (Domain == EForwardDomain::ToData)
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

					// Inherit only: deleting a same-named attribute a live property buffer caches would leave it dangling.
					if (Domain != EForwardDomain::Inherit)
					{
						UE_LOG(LogPCGEx, Warning, TEXT("Property-backed attribute '%s' is only forwarded onto a facade with the Inherit domain -- skipped."), *Identity.Name.ToString());
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

					const FPCGAttributeIdentifier Identifier = GetTargetIdentifier(Identity);

					InTargetMetadata->DeleteAttribute(Identifier);
					FPCGMetadataAttributeBase* TargetAtt = InTargetMetadata->FindOrCreateAttribute<T>(Identifier, ForwardValue, SourceAtt->AllowsInterpolation(), true, true);
					if (Domain == EForwardDomain::ToData)
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

					const FPCGAttributeIdentifier Identifier = GetTargetIdentifier(Identity);

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
