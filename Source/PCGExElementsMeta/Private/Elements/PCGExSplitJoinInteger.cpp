// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSplitJoinInteger.h"

#include <type_traits>

#include "PCGContext.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

#define LOCTEXT_NAMESPACE "PCGExSplitJoinIntegerElement"
#define PCGEX_NAMESPACE SplitJoinInteger

#define PCGEX_SPLITJOIN_LOG_PREFIX TEXT("Split / Join Integer: ")

#pragma region UPCGSettings interface

#if WITH_EDITOR
TArray<FText> UPCGExSplitJoinIntegerSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Split Integer"), FTEXT("PCGEx | Join Integer")};
}

FString UPCGExSplitJoinIntegerSettings::GetDisplayName() const
{
	// No "PCGEx | " prefix: the default title already shows it on the line below.
	switch (Mode)
	{
	case EPCGExSplitJoinIntegerMode::Split:
		return TEXT("Split Integer");
	case EPCGExSplitJoinIntegerMode::Join:
		return TEXT("Join Integer");
	default:
		return TEXT("Split / Join Integer");
	}
}
#endif

TArray<FPCGPinProperties> UPCGExSplitJoinIntegerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Point or attribute-set data holding the packed attribute (Split) or the field attributes (Join).", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSplitJoinIntegerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "The input data with the field attributes (Split) or the packed attribute (Join) written. Data the operation cannot run on is forwarded untouched.", Required)
	return PinProperties;
}

FPCGElementPtr UPCGExSplitJoinIntegerSettings::CreateElement() const
{
	return MakeShared<FPCGExSplitJoinIntegerElement>();
}

#pragma endregion

namespace PCGExSplitJoinInteger
{
	int32 GetTotalBits(const TConstArrayView<FPCGExIntegerLayoutField> InLayout)
	{
		int32 TotalBits = 0;
		for (const FPCGExIntegerLayoutField& Entry : InLayout)
		{
			TotalBits += Entry.NumBits;
		}
		return TotalBits;
	}

	bool ResolveLayout(const TConstArrayView<FPCGExIntegerLayoutField> InLayout, const int32 InMaxBits, TArray<FField>& OutFields, FText& OutError)
	{
		OutFields.Reset();

		// Checked before the sum so a huge bit count cannot overflow it.
		for (int32 i = 0; i < InLayout.Num(); i++)
		{
			if (InLayout[i].NumBits < MinFieldBits || InLayout[i].NumBits > MaxFieldBits)
			{
				OutError = FText::Format(
					FTEXT("layout entry #{0} takes {1} bits; an entry takes {2} to {3} bits."),
					FText::AsNumber(i), FText::AsNumber(InLayout[i].NumBits), FText::AsNumber(MinFieldBits), FText::AsNumber(MaxFieldBits));
				return false;
			}
		}

		const int32 TotalBits = GetTotalBits(InLayout);
		if (TotalBits > InMaxBits)
		{
			OutError = FText::Format(
				FTEXT("the layout takes {0} bits, more than the {1} the packed integer holds."),
				FText::AsNumber(TotalBits), FText::AsNumber(InMaxBits));
			return false;
		}

		int32 Shift = TotalBits;
		for (const FPCGExIntegerLayoutField& Entry : InLayout)
		{
			Shift -= Entry.NumBits;

			if (Entry.Name.IsNone())
			{
				continue;
			}

			if (OutFields.ContainsByPredicate([&Entry](const FField& Field) { return Field.Name == Entry.Name; }))
			{
				OutError = FText::Format(FTEXT("the layout names \"{0}\" more than once."), FText::FromName(Entry.Name));
				OutFields.Reset();
				return false;
			}

			FField& Field = OutFields.Emplace_GetRef();
			Field.Name = Entry.Name;
			Field.Mask = (static_cast<uint64>(1) << Entry.NumBits) - 1;
			Field.Shift = Shift;
		}

		if (OutFields.IsEmpty())
		{
			OutError = FTEXT("the layout names no field.");
			return false;
		}

		return true;
	}

	// Names the engine resolves per data to whichever attribute was last read or created: writing through one overwrites that attribute.
	bool IsReservedAttributeName(const FName InName)
	{
		return InName == PCGMetadataAttributeConstants::LastAttributeName
			|| InName == PCGMetadataAttributeConstants::LastCreatedAttributeName
			|| InName == PCGMetadataAttributeConstants::SourceAttributeName
			|| InName == PCGMetadataAttributeConstants::SourceNameAttributeName;
	}

	// Element-domain attributes only: a name that carries a domain or an accessor, or that the engine reserves, is refused.
	bool IsPlainAttributeName(const FName InName)
	{
		if (InName.IsNone() || IsReservedAttributeName(InName))
		{
			return false;
		}

		FPCGAttributePropertyInputSelector Selector;
		if (!Selector.Update(InName.ToString()))
		{
			return false;
		}

		return Selector.GetSelection() == EPCGAttributePropertySelection::Attribute
			&& Selector.IsValid()
			&& Selector.GetDomainName().IsNone()
			&& Selector.GetExtraNames().IsEmpty();
	}

	// One past the highest bit a field reads or writes.
	int32 GetUsedBits(const TConstArrayView<FField> InFields)
	{
		int32 UsedBits = 0;
		for (const FField& Field : InFields)
		{
			UsedBits = FMath::Max(UsedBits, Field.Shift + static_cast<int32>(FMath::CountBits64(Field.Mask)));
		}
		return UsedBits;
	}

	// False unless InName is a single-value int32 or int64 attribute on InData.
	bool TryGetIntegerWidth(const UPCGData* InData, const FName InName, bool& bOutIs64)
	{
		const FPCGMetadataAttributeBase* Attribute = PCGExMetaHelpers::TryGetConstAttribute(InData, PCGExMetaHelpers::GetAttributeIdentifier(InName, InData));
		if (!Attribute || !PCGExMetaHelpers::IsBasicSingleValue(Attribute->GetAttributeDesc()))
		{
			return false;
		}

		switch (PCGExMetaHelpers::GetAttributeType(Attribute))
		{
		case EPCGMetadataTypes::Integer32:
			bOutIs64 = false;
			return true;
		case EPCGMetadataTypes::Integer64:
			bOutIs64 = true;
			return true;
		default:
			return false;
		}
	}

	// Reads InName in its own type (T must match it) so no conversion runs. False when the read is refused.
	template <typename T>
	bool ReadRows(const UPCGData* InData, const FName InName, const TSharedPtr<const IPCGAttributeAccessorKeys>& InKeys, const int32 NumRows, TArray<T>& OutValues)
	{
		PCGExData::Helpers::BulkReadRows<T>(InData, InName, OutValues, InKeys);
		return OutValues.Num() == NumRows;
	}

	// Creates the attribute even without rows, replacing one of another type that already uses the name.
	template <typename T>
	bool WriteRows(UPCGData* InData, const FName InName, const TArray<T>& InValues, IPCGAttributeAccessorKeys* InKeys)
	{
		UPCGMetadata* Metadata = InData->MutableMetadata();
		if (!Metadata)
		{
			return false;
		}

		FPCGMetadataAttribute<T>* Attribute = Metadata->FindOrCreateAttribute<T>(PCGExMetaHelpers::GetAttributeIdentifier(InName, InData), T{}, false, true, true);
		if (!Attribute)
		{
			return false;
		}

		if (InValues.IsEmpty())
		{
			return true;
		}

		if (!InKeys)
		{
			return false;
		}

		const TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(Attribute, Metadata);
		return Accessor && PCGExData::Helpers::BulkWriteRows<T>(*Accessor, InValues, *InKeys);
	}

	template <typename T>
	UPCGData* SplitTyped(FPCGExContext* InContext, const FName InPackedName, const TConstArrayView<FField> InFields, const UPCGData* InData)
	{
		using TUnsigned = std::make_unsigned_t<T>;

		const int32 NumRows = PCGExMetaHelpers::GetElementsCount(InData);

		// Read from the input, before duplicating: a duplicate answers every row through its parent.
		TArray<T> Packed;
		if (NumRows > 0)
		{
			const TSharedPtr<IPCGAttributeAccessorKeys> ReadKeys = PCGExMetaHelpers::MakeConstKeys(InData);
			if (!ReadKeys || !ReadRows<T>(InData, InPackedName, ReadKeys, NumRows, Packed))
			{
				return nullptr;
			}
		}

		UPCGData* OutData = InContext->ManagedObjects->DuplicateData<UPCGData>(InData);
		if (!OutData)
		{
			return nullptr;
		}

		const TSharedPtr<IPCGAttributeAccessorKeys> WriteKeys = NumRows > 0 ? PCGExMetaHelpers::MakeMutableKeys(OutData) : nullptr;

		TArray<int32> Values;
		Values.SetNumUninitialized(NumRows);

		for (const FField& Field : InFields)
		{
			for (int32 i = 0; i < NumRows; i++)
			{
				Values[i] = Extract(static_cast<TUnsigned>(Packed[i]), Field);
			}

			if (!WriteRows<int32>(OutData, Field.Name, Values, WriteKeys.Get()))
			{
				return nullptr;
			}
		}

		return OutData;
	}

	UPCGData* Split(FPCGExContext* InContext, const FName InPackedName, const TConstArrayView<FField> InFields, const UPCGData* InData, const int32 InInputIndex)
	{
		bool bIs64 = false;
		if (!TryGetIntegerWidth(InData, InPackedName, bIs64))
		{
			PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(
				                        FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "input #{0} has no int32 or int64 attribute \"{1}\" -- forwarded untouched."),
				                        FText::AsNumber(InInputIndex), FText::FromName(InPackedName)))
			return nullptr;
		}

		if (!bIs64 && GetUsedBits(InFields) > 32)
		{
			PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(
				                        FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "\"{0}\" is an int32 on input #{1} -- fields above bit 31 read zero."),
				                        FText::FromName(InPackedName), FText::AsNumber(InInputIndex)))
		}

		UPCGData* OutData = bIs64
			                    ? SplitTyped<int64>(InContext, InPackedName, InFields, InData)
			                    : SplitTyped<int32>(InContext, InPackedName, InFields, InData);

		if (!OutData)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				           FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "could not split \"{0}\" on input #{1} -- forwarded untouched."),
				           FText::FromName(InPackedName), FText::AsNumber(InInputIndex)));
		}

		return OutData;
	}

	// ORs one field into every packed row. InScratch is reused across the fields of its type.
	template <typename TField, typename TPacked>
	bool JoinField(const UPCGData* InData, const FField& InField, const TSharedPtr<const IPCGAttributeAccessorKeys>& InKeys, TArray<TField>& InScratch, TArray<TPacked>& InOutPacked)
	{
		using TUnsignedField = std::make_unsigned_t<TField>;
		using TUnsignedPacked = std::make_unsigned_t<TPacked>;

		const int32 NumRows = InOutPacked.Num();

		if (!ReadRows<TField>(InData, InField.Name, InKeys, NumRows, InScratch))
		{
			return false;
		}

		for (int32 i = 0; i < NumRows; i++)
		{
			const TUnsignedPacked Bits = static_cast<TUnsignedPacked>(Insert(static_cast<TUnsignedField>(InScratch[i]), InField));
			InOutPacked[i] = static_cast<TPacked>(static_cast<TUnsignedPacked>(InOutPacked[i]) | Bits);
		}

		return true;
	}

	template <typename TPacked>
	UPCGData* JoinTyped(FPCGExContext* InContext, const FName InPackedName, const TConstArrayView<FField> InFields, const TConstArrayView<bool> InIs64, const UPCGData* InData)
	{
		const int32 NumRows = PCGExMetaHelpers::GetElementsCount(InData);

		TArray<TPacked> Packed;
		Packed.SetNumZeroed(NumRows);

		// Read from the input, before duplicating: a duplicate answers every row through its parent.
		if (NumRows > 0)
		{
			const TSharedPtr<IPCGAttributeAccessorKeys> ReadKeys = PCGExMetaHelpers::MakeConstKeys(InData);
			if (!ReadKeys)
			{
				return nullptr;
			}

			TArray<int32> Scratch32;
			TArray<int64> Scratch64;

			for (int32 f = 0; f < InFields.Num(); f++)
			{
				const bool bRead = InIs64[f]
					                   ? JoinField<int64, TPacked>(InData, InFields[f], ReadKeys, Scratch64, Packed)
					                   : JoinField<int32, TPacked>(InData, InFields[f], ReadKeys, Scratch32, Packed);

				if (!bRead)
				{
					return nullptr;
				}
			}
		}

		UPCGData* OutData = InContext->ManagedObjects->DuplicateData<UPCGData>(InData);
		if (!OutData)
		{
			return nullptr;
		}

		const TSharedPtr<IPCGAttributeAccessorKeys> WriteKeys = NumRows > 0 ? PCGExMetaHelpers::MakeMutableKeys(OutData) : nullptr;
		return WriteRows<TPacked>(OutData, InPackedName, Packed, WriteKeys.Get()) ? OutData : nullptr;
	}

	UPCGData* Join(FPCGExContext* InContext, const FName InPackedName, const TConstArrayView<FField> InFields, const bool bInToInt64, const UPCGData* InData, const int32 InInputIndex)
	{
		TArray<bool, TInlineAllocator<8>> Is64;
		Is64.SetNumZeroed(InFields.Num());

		for (int32 f = 0; f < InFields.Num(); f++)
		{
			bool bIs64 = false;
			if (!TryGetIntegerWidth(InData, InFields[f].Name, bIs64))
			{
				PCGEX_LOG_INVALID_INPUT(InContext, FText::Format(
					                        FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "input #{0} has no int32 or int64 attribute \"{1}\" -- forwarded untouched."),
					                        FText::AsNumber(InInputIndex), FText::FromName(InFields[f].Name)))
				return nullptr;
			}
			Is64[f] = bIs64;
		}

		UPCGData* OutData = bInToInt64
			                    ? JoinTyped<int64>(InContext, InPackedName, InFields, Is64, InData)
			                    : JoinTyped<int32>(InContext, InPackedName, InFields, Is64, InData);

		if (!OutData)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				           FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "could not join \"{0}\" on input #{1} -- forwarded untouched."),
				           FText::FromName(InPackedName), FText::AsNumber(InInputIndex)));
		}

		return OutData;
	}
}

#pragma region FPCGExSplitJoinIntegerElement

bool FPCGExSplitJoinIntegerElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSplitJoinIntegerElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, SplitJoinInteger)
	FPCGExSplitJoinIntegerContext* Context = static_cast<FPCGExSplitJoinIntegerContext*>(InContext);

	switch (Settings->Mode)
	{
	case EPCGExSplitJoinIntegerMode::Split:
		Context->bJoin = false;
		break;
	case EPCGExSplitJoinIntegerMode::Join:
		Context->bJoin = true;
		break;
	default:
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "unknown Mode."));
		return false;
	}

	// Split reads whichever integer type the data holds, so only Join is bound by the packed type.
	int32 MaxBits = 64;
	if (Context->bJoin)
	{
		switch (Settings->PackedType)
		{
		case EPCGExPackedIntegerType::Int32:
			Context->bJoinToInt64 = false;
			MaxBits = 32;
			break;
		case EPCGExPackedIntegerType::Int64:
			Context->bJoinToInt64 = true;
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "unknown Packed Type."));
			return false;
		}
	}

	if (!PCGExSplitJoinInteger::IsPlainAttributeName(Settings->PackedAttribute))
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(
			           FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "\"{0}\" is not a valid packed attribute name. A plain attribute name is expected, without domain or accessor."),
			           FText::FromName(Settings->PackedAttribute)));
		return false;
	}

	FText LayoutError;
	if (!PCGExSplitJoinInteger::ResolveLayout(Settings->Layout, MaxBits, Context->Fields, LayoutError))
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "{0}"), LayoutError));
		return false;
	}

	for (const PCGExSplitJoinInteger::FField& Field : Context->Fields)
	{
		if (!PCGExSplitJoinInteger::IsPlainAttributeName(Field.Name))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(
				           FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "\"{0}\" is not a valid field name. A plain attribute name is expected, without domain or accessor."),
				           FText::FromName(Field.Name)));
			return false;
		}

		if (Field.Name == Settings->PackedAttribute)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(
				           FTEXT(PCGEX_SPLITJOIN_LOG_PREFIX "the layout names the packed attribute \"{0}\" as one of its fields."),
				           FText::FromName(Field.Name)));
			return false;
		}
	}

	// What the operation reads is what the node's cleanup may remove.
	if (Context->bJoin)
	{
		for (const PCGExSplitJoinInteger::FField& Field : Context->Fields)
		{
			Context->AddConsumableAttributeName(Field.Name);
		}
	}
	else
	{
		Context->AddConsumableAttributeName(Settings->PackedAttribute);
	}

	return true;
}

bool FPCGExSplitJoinIntegerElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSplitJoinIntegerElement::AdvanceWork);

	PCGEX_SETTINGS_C(InContext, SplitJoinInteger)
	const FPCGExSplitJoinIntegerContext* Context = static_cast<FPCGExSplitJoinIntegerContext*>(InContext);

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	const FName PackedName = Settings->PackedAttribute;

	// Null where the input is forwarded as is.
	TArray<UPCGData*> Processed;
	Processed.Init(nullptr, Inputs.Num());

	PCGExMT::ParallelOrSequential(Inputs.Num(), [&](const int32 InputIndex)
	{
		const UPCGData* InData = Inputs[InputIndex].Data;
		if (!InData)
		{
			return;
		}

		Processed[InputIndex] = Context->bJoin
			                        ? PCGExSplitJoinInteger::Join(InContext, PackedName, Context->Fields, Context->bJoinToInt64, InData, InputIndex)
			                        : PCGExSplitJoinInteger::Split(InContext, PackedName, Context->Fields, InData, InputIndex);
	}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	InContext->IncreaseStagedOutputReserve(Inputs.Num());
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		const FPCGTaggedData& Input = Inputs[i];
		if (!Input.Data)
		{
			continue;
		}

		if (UPCGData* OutData = Processed[i])
		{
			InContext->StageOutput(OutData, PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::MutableAndManaged, Input.Tags);
		}
		else
		{
			// Not Mutable: a forwarded input must keep the attributes the operation never read.
			InContext->StageOutput(const_cast<UPCGData*>(Input.Data.Get()), PCGPinConstants::DefaultOutputLabel, PCGExData::EStaging::None, Input.Tags);
		}
	}

	InContext->Done();
	return InContext->TryComplete();
}

#pragma endregion

#undef PCGEX_SPLITJOIN_LOG_PREFIX
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
