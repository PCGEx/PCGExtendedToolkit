// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExDataValue.h"

#include "PCGData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExSubSelection.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExTypeOps.h"
#include "Types/PCGExTypeOpsImpl.h"
#include "Types/PCGExTypeTraits.h"
#include "Types/PCGExTypes.h"

namespace PCGExData
{
	bool IDataValue::SameValue(const TSharedPtr<IDataValue>& Other)
	{
		if (IsNumeric() && Other->IsNumeric())
		{
			return AsDouble() == Other->AsDouble();
		}
		if (IsText() && Other->IsText())
		{
			return AsString() == Other->AsString();
		}
		return false;
	}

	template <typename T>
	T IDataValue::GetValue()
	{
		if (GetTypeId() == PCGExTypes::TTraits<T>::Type)
		{
			return static_cast<TDataValue<T>*>(this)->Value;
		}
		if (IsNumeric())
		{
			return PCGExTypeOps::Convert<double, T>(AsDouble());
		}
		return PCGExTypeOps::Convert<FString, T>(AsString());
	}

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template PCGEXCORE_API _TYPE IDataValue::GetValue<_TYPE>();
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

	template <typename T>
	TDataValue<T>::TDataValue(const T& InValue)
		: IDataValue()
		  , Value(InValue)
	{
		Type = PCGExTypes::TTraits<T>::Type;
	}

	template <typename T>
	FString TDataValue<T>::Flatten(const FString& LeftSide)
	{
		return FString::Printf(TEXT("%s:%s"), *LeftSide, *PCGExTypeOps::FTypeOps<T>::template ConvertTo<FString>(Value));
	}

	template <typename T>
	bool TDataValue<T>::IsNumeric() const
	{
		return PCGExTypes::TTraits<T>::bIsNumeric;
	}

	template <typename T>
	bool TDataValue<T>::IsText() const
	{
		return PCGExTypes::TTraits<T>::bIsString;
	}

	template <typename T>
	double TDataValue<T>::AsDouble()
	{
		if (CachedDouble.IsSet())
		{
			return CachedDouble.GetValue();
		}

		const double V = PCGExTypeOps::FTypeOps<T>::template ConvertTo<double>(Value);
		CachedDouble.Emplace(V);
		return V;
	}

	template <typename T>
	FString TDataValue<T>::AsString()
	{
		if (CachedString.IsSet())
		{
			return CachedString.GetValue();
		}

		FString V = PCGExTypeOps::FTypeOps<T>::template ConvertTo<FString>(Value);
		CachedString.Emplace(V);
		return V;
	}

	template <typename T>
	void TDataValue<T>::GetVoid(void* OutValue) const
	{
		*static_cast<T*>(OutValue) = Value;
	}

#define PCGEX_TPL(_TYPE, _NAME, ...)\
template class PCGEXCORE_API TDataValue<_TYPE>;

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

	// Parses a "Key:Value" tag string into a typed IDataValue.
	// Type inference cascade: numeric with decimal → double, integer → int64,
	// then FVector/FVector2D/FVector4 via InitFromString, bool (true/false),
	// and finally falls back to FString.
	TSharedPtr<IDataValue> TryGetValueFromTag(const FString& InTag, FString& OutLeftSide)
	{
		int32 DividerPosition = INDEX_NONE;
		if (!InTag.FindChar(':', DividerPosition))
		{
			return nullptr;
		}

		OutLeftSide = InTag.Left(DividerPosition);
		FString RightSide = InTag.RightChop(DividerPosition + 1);

		if (OutLeftSide.IsEmpty())
		{
			return nullptr;
		}
		if (RightSide.IsEmpty())
		{
			return nullptr;
		}
		if (RightSide.IsNumeric())
		{
			int32 FloatingPointPosition = INDEX_NONE;
			if (RightSide.FindChar('.', FloatingPointPosition))
			{
				return MakeShared<TDataValue<double>>(FCString::Atod(*RightSide));
			}
			return MakeShared<TDataValue<int64>>(FCString::Atoi64(*RightSide));
		}

		if (FVector ParsedVector;
			ParsedVector.InitFromString(RightSide))
		{
			return MakeShared<TDataValue<FVector>>(ParsedVector);
		}

		if (FVector2D ParsedVector2D;
			ParsedVector2D.InitFromString(RightSide))
		{
			return MakeShared<TDataValue<FVector2D>>(ParsedVector2D);
		}

		if (FVector4 ParsedVector4;
			ParsedVector4.InitFromString(RightSide))
		{
			return MakeShared<TDataValue<FVector4>>(ParsedVector4);
		}

		if (const FString ToUpper = RightSide.ToUpper();
			ToUpper == TEXT("TRUE"))
		{
			return MakeShared<TDataValue<bool>>(true);
		}
		else if (ToUpper == TEXT("FALSE"))
		{
			return MakeShared<TDataValue<bool>>(false);
		}

		return MakeShared<TDataValue<FString>>(RightSide);
	}

	TSharedPtr<IDataValue> TryGetValueFromData(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector)
	{
		TSharedPtr<IDataValue> DataValue = nullptr;
		const UPCGMetadata* InMetadata = InData->Metadata;

		if (!InMetadata)
		{
			return nullptr;
		}

		FPCGAttributePropertyInputSelector Selector = InSelector.CopyAndFixLast(InData);

		// Attribute unsupported
		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			return nullptr;
		}

		FPCGAttributeIdentifier SanitizedIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Selector, InData);
		SanitizedIdentifier.MetadataDomain = PCGMetadataDomainID::Data; // Force data domain

		// Non-data domain unsupported
		if (SanitizedIdentifier.MetadataDomain.Flag != EPCGMetadataDomainFlag::Data)
		{
			return nullptr;
		}

		// Domain-safe lookup: the data may have never instantiated its @Data domain.
		if (const FPCGMetadataAttributeBase* SourceAttribute = PCGExMetaHelpers::TryGetConstAttribute(InMetadata, SanitizedIdentifier))
		{
			// Container/extended source types fall through -- TDataValue<T> is templated on basic types only,
			// so we can't represent them. Caller gets nullptr DataValue and skips.
			PCGExMetaHelpers::ExecuteWithRightType(SourceAttribute, [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				const T Value = Helpers::ReadDataValue<T>(SourceAttribute);

				FSubSelection SubSelection(Selector);
				TSharedPtr<IDataValue> TypedDataValue = nullptr;

				if (SubSelection.HasSelection())
				{
					PCGExMetaHelpers::ExecuteWithRightType(SourceAttribute, [&](auto WorkingValue)
					{
						using T_WORKING = decltype(DummyValue);
						TypedDataValue = MakeShared<TDataValue<T_WORKING>>(SubSelection.Get<T, T_WORKING>(Value));
					});
				}
				else
				{
					TypedDataValue = MakeShared<TDataValue<T>>(Value);
				}

				DataValue = TypedDataValue;
			});
		}

		return DataValue;
	}

	TSharedPtr<IDataValue> TryGetValueFromData(const UPCGData* InData, const FName& InName)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InName.ToString());

		return TryGetValueFromData(InData, Selector);
	}

	TSharedPtr<IDataValue> TryGetValueFromData(const FPCGTaggedData& InTaggedData, const FPCGAttributePropertyInputSelector& InSelector)
	{
		// Tag-first: match the selector name against the "key:value" tags.
		const FString TagName = InSelector.GetName().ToString();
		if (!TagName.IsEmpty())
		{
			for (const FString& Tag : InTaggedData.Tags)
			{
				FString LeftSide;
				if (TSharedPtr<IDataValue> TagValue = TryGetValueFromTag(Tag, LeftSide);
					TagValue && LeftSide == TagName)
				{
					return TagValue;
				}
			}
		}

		// Fallback: data-domain attribute.
		return TryGetValueFromData(InTaggedData.Data, InSelector);
	}
}
