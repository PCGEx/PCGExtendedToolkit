// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExMetaHelpers.h"
#include "CoreMinimal.h"
#include "PCGData.h"
#include "PCGExCommon.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

namespace PCGExMetaHelpers
{
	int32 GetElementsCount(const UPCGData* InData)
	{
		if (!InData) { return 0; }
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData)) { return PointData->GetNumPoints(); }
		if (const UPCGMetadata* Metadata = InData->ConstMetadata()) { return static_cast<int32>(Metadata->GetLocalItemCount()); }
		return 0;
	}

	TSharedPtr<IPCGAttributeAccessorKeys> MakeMutableKeys(UPCGData* InData)
	{
		if (UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			return MakeShared<FPCGAttributeAccessorKeysPointIndices>(PointData, true);
		}
		if (InData && InData->MutableMetadata())
		{
			return MakeShared<FPCGAttributeAccessorKeysEntries>(InData->MutableMetadata());
		}
		return nullptr;
	}
	
	TSharedPtr<IPCGAttributeAccessorKeys> MakeConstKeys(const UPCGData* InData)
	{
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			return MakeShared<FPCGAttributeAccessorKeysPointIndices>(PointData);
		}
		if (InData && InData->Metadata)
		{
			return MakeShared<FPCGAttributeAccessorKeysEntries>(InData->Metadata);
		}
		return nullptr;
	}

	void InitializeMetadataEntries(UPCGMetadata* Metadata, const TPCGValueRange<int64>& MetadataEntries, const bool bConservative)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIO::InitializeMetadataEntries);

		if (bConservative)
		{
			// Conservative: re-initialize only keys that don't reference a real LOCAL entry
			// of this metadata -- i.e. anything outside [ItemKeyOffset, ItemCount). This
			// preserves genuine local entries (the Duplicate case, where points already
			// carry valid attribute data) while healing three corruptions:
			//   Key == PCGInvalidEntryKey -> never set
			//   Key <  ItemKeyOffset      -> stale parent reference
			//   Key >= ItemCount          -> dangling: a positive-but-unbacked key (e.g.
			//                                inherited from an upstream node whose metadata
			//                                schema doesn't back it). Such keys look valid to
			//                                every later conservative pass, so they slip
			//                                through unrepaired and surface as "Output ... does
			//                                not have valid point metadata" on the graph output.
			TArray<int64*> KeysNeedingInit;
			KeysNeedingInit.Reserve(MetadataEntries.Num());
			const int64 ItemKeyOffset = Metadata->GetItemKeyCountForParent();
			const int64 ItemCount = Metadata->GetItemCountForChild();

			for (int64& Key : MetadataEntries)
			{
				if (Key == PCGInvalidEntryKey || Key < ItemKeyOffset || Key >= ItemCount)
				{
					KeysNeedingInit.Add(&Key);
				}
			}

			if (KeysNeedingInit.Num() > 0)
			{
				Metadata->AddEntriesInPlace(KeysNeedingInit);
			}
		}
		else
		{
			// Non-conservative: replace ALL metadata entries with fresh ones.
			// Old keys are preserved as parent references in DelayedEntries so attribute
			// values can still be inherited/interpolated from the original data.
			// Placeholder keys are handed out sequentially from the current item count,
			// so they are synthesized directly instead of paying one lock + shared
			// atomic per point. AddDelayedEntries sizes from the array we pass.
			const int32 NumEntries = MetadataEntries.Num();
			const int64 EntryStart = Metadata->GetItemCountForChild();

			TArray<TTuple<int64, int64>> DelayedEntries;
			DelayedEntries.SetNumUninitialized(NumEntries);

			for (int i = 0; i < NumEntries; i++)
			{
				const int64 Entry = EntryStart + i;
				DelayedEntries[i] = MakeTuple(Entry, MetadataEntries[i]);
				MetadataEntries[i] = Entry;
			}

			Metadata->AddDelayedEntries(DelayedEntries);
		}
	}

	bool IsPCGExAttribute(const FString& InStr)
	{
		return InStr.Contains(PCGExCommon::PCGExPrefix);
	}

	bool IsPCGExAttribute(const FName InName)
	{
		return IsPCGExAttribute(InName.ToString());
	}

	bool IsPCGExAttribute(const FText& InText)
	{
		return IsPCGExAttribute(InText.ToString());
	}

	FName MakePCGExAttributeName(const FString& Str0)
	{
		return FName(FText::Format(FText::FromString(TEXT("{0}{1}")), FText::FromString(PCGExCommon::PCGExPrefix), FText::FromString(Str0)).ToString());
	}

	FName MakePCGExAttributeName(const FString& Str0, const FString& Str1)
	{
		return FName(FText::Format(FText::FromString(TEXT("{0}{1}/{2}")), FText::FromString(PCGExCommon::PCGExPrefix), FText::FromString(Str0), FText::FromString(Str1)).ToString());
	}

	bool IsWritableAttributeName(const FName Name)
	{
		// This is a very expensive check, however it's also futureproofing
		if (Name.IsNone())
		{
			return false;
		}

		FPCGAttributePropertyInputSelector DummySelector;
		if (!DummySelector.Update(Name.ToString()))
		{
			return false;
		}
		return DummySelector.GetSelection() == EPCGAttributePropertySelection::Attribute && DummySelector.IsValid();
	}

	FName SanitizeAttributeName(const FName InName)
	{
		if (InName.IsNone())
		{
			return NAME_None;
		}

		// Allowed character set mirrors FPCGMetadataAttributeBase::IsValidName:
		// alphanumerics plus space, underscore, hyphen, forward slash.
		const FString Src = InName.ToString();
		FString Out;
		Out.Reserve(Src.Len());

		bool bLastUnderscore = false;
		for (int32 i = 0; i < Src.Len(); ++i)
		{
			const TCHAR C = Src[i];
			const bool bValid =
				FChar::IsAlpha(C) || FChar::IsDigit(C) ||
				C == TEXT(' ') || C == TEXT('_') || C == TEXT('-') || C == TEXT('/');

			if (bValid)
			{
				if (C == TEXT('_'))
				{
					if (bLastUnderscore)
					{
						continue;
					}
					bLastUnderscore = true;
				}
				else
				{
					bLastUnderscore = false;
				}
				Out.AppendChar(C);
			}
			else
			{
				if (bLastUnderscore)
				{
					continue;
				}
				Out.AppendChar(TEXT('_'));
				bLastUnderscore = true;
			}
		}

		// Trim leading/trailing underscores
		int32 Start = 0;
		while (Start < Out.Len() && Out[Start] == TEXT('_'))
		{
			++Start;
		}
		int32 End = Out.Len();
		while (End > Start && Out[End - 1] == TEXT('_'))
		{
			--End;
		}
		if (Start > 0 || End < Out.Len())
		{
			Out = Out.Mid(Start, End - Start);
		}

		if (Out.IsEmpty())
		{
			return NAME_None;
		}
		return FName(*Out);
	}

	FString StringTagFromName(const FName Name)
	{
		if (Name.IsNone())
		{
			return TEXT("");
		}
		return Name.ToString().TrimStartAndEnd();
	}

	bool IsValidStringTag(const FString& Tag)
	{
		if (Tag.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}
		return true;
	}

	FName GetCompoundName(const FName A, const FName B)
	{
		// PCGEx/A/B
		const FString Separator = TEXT("/");
		return *(TEXT("PCGEx") + Separator + A.ToString() + Separator + B.ToString());
	}

	FName GetCompoundName(const FName A, const FName B, const FName C)
	{
		// PCGEx/A/B/C
		const FString Separator = TEXT("/");
		return *(TEXT("PCGEx") + Separator + A.ToString() + Separator + B.ToString() + Separator + C.ToString());
	}

	bool TryGetAttributeName(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, FName& OutName)
	{
		FPCGAttributePropertyInputSelector FixedSelector = InSelector.CopyAndFixLast(InData);
		if (!FixedSelector.IsValid())
		{
			return false;
		}
		if (FixedSelector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			return false;
		}
		OutName = FixedSelector.GetName();
		return true;
	}

	bool IsDataDomainAttribute(const FName& InName)
	{
		return InName.ToString().TrimStartAndEnd().StartsWith("@Data.");
	}

	bool IsDataDomainAttribute(const FString& InName)
	{
		return InName.TrimStartAndEnd().StartsWith("@Data.");
	}

	bool IsDataDomainAttribute(const FPCGAttributePropertyInputSelector& InputSelector)
	{
		return InputSelector.GetDomainName() == PCGDataConstants::DataDomainName || IsDataDomainAttribute(InputSelector.GetName());
	}

	void AppendUniqueSelectorsFromCommaSeparatedList(const FString& InCommaSeparatedString, TArray<FPCGAttributePropertyInputSelector>& OutSelectors)
	{
		if (InCommaSeparatedString.IsEmpty())
		{
			return;
		}

		TArray<FString> Result;
		InCommaSeparatedString.ParseIntoArray(Result, TEXT(","));
		// Trim leading and trailing spaces
		for (int i = 0; i < Result.Num(); i++)
		{
			FString& String = Result[i];
			String.TrimStartAndEndInline();
			if (String.IsEmpty())
			{
				continue;
			}

			FPCGAttributePropertyInputSelector Selector;
			Selector.Update(String);

			OutSelectors.AddUnique(Selector);
		}
	}

	FName GetLongNameFromSelector(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const bool bInitialized)
	{
		// This return a domain-less unique identifier for the provided selector
		// It's mostly used to create uniquely identified value buffers

		if (!InData)
		{
			return InvalidName;
		}

		if (bInitialized)
		{
			if (InSelector.GetExtraNames().IsEmpty())
			{
				return FName(InSelector.GetName().ToString());
			}
			return FName(InSelector.GetName().ToString() + TEXT(".") + FString::Join(InSelector.GetExtraNames(), TEXT(".")));
		}
		if (InSelector.GetSelection() == EPCGAttributePropertySelection::Attribute && InSelector.GetName() == "@Last")
		{
			return GetLongNameFromSelector(InSelector.CopyAndFixLast(InData), InData, true);
		}

		return GetLongNameFromSelector(InSelector, InData, true);
	}

	FPCGAttributeIdentifier GetAttributeIdentifier(const FPCGAttributePropertyInputSelector& InSelector, const UPCGData* InData, const bool bInitialized)
	{
		// This return an identifier suitable to be used for data facade

		FPCGAttributeIdentifier Identifier;

		if (!InData)
		{
			return FPCGAttributeIdentifier(InvalidName, EPCGMetadataDomainFlag::Invalid);
		}

		if (bInitialized)
		{
			Identifier.Name = InSelector.GetAttributeName();
			Identifier.MetadataDomain = InData->GetMetadataDomainIDFromSelector(InSelector);
		}
		else
		{
			FPCGAttributePropertyInputSelector FixedSelector = InSelector.CopyAndFixLast(InData);

			check(FixedSelector.GetSelection() == EPCGAttributePropertySelection::Attribute)

			Identifier.Name = FixedSelector.GetAttributeName();
			Identifier.MetadataDomain = InData->GetMetadataDomainIDFromSelector(FixedSelector);
		}


		return Identifier;
	}

	FPCGAttributeIdentifier GetAttributeIdentifier(const FName InName, const UPCGData* InData)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InName.ToString());
		Selector = Selector.CopyAndFixLast(InData);
		return GetAttributeIdentifier(Selector, InData);
	}

	FPCGAttributeIdentifier GetAttributeIdentifier(const FName InName)
	{
		FString StrName = InName.ToString();
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InName.ToString());
		return FPCGAttributeIdentifier(Selector.GetAttributeName(), StrName.StartsWith(TEXT("@Data.")) ? PCGMetadataDomainID::Data : PCGMetadataDomainID::Elements);
	}

	EPCGMetadataTypes GetAttributeType(const FPCGMetadataAttributeBase* InAttribute)
	{
		if (!InAttribute)
		{
			return EPCGMetadataTypes::Unknown;
		}

		const EPCGMetadataTypes LegacyType = static_cast<EPCGMetadataTypes>(InAttribute->GetTypeId());
		if (LegacyType != EPCGMetadataTypes::Unknown)
		{
			return LegacyType;
		}

		return InAttribute->GetAttributeDesc().ValueType;
	}

	FPCGAttributePropertyInputSelector GetSelectorFromIdentifier(const FPCGAttributeIdentifier& InIdentifier)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.SetAttributeName(InIdentifier.Name);
		Selector.SetDomainName(InIdentifier.MetadataDomain.DebugName);
		return Selector;
	}

	FName StripDomainFromName(const FName& InName)
	{
		const FString NameStr = InName.ToString();

		// Check for @{Domain}. pattern
		if (NameStr.StartsWith(TEXT("@")))
		{
			const int32 DotIndex = NameStr.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
			if (DotIndex != INDEX_NONE)
			{
				return FName(NameStr.Mid(DotIndex + 1));
			}
		}

		return InName;
	}

	FPCGAttributeIdentifier MakeDataIdentifier(const FName& BaseName)
	{
		return FPCGAttributeIdentifier(StripDomainFromName(BaseName), PCGMetadataDomainID::Data);
	}

	FPCGAttributeIdentifier MakeElementIdentifier(const FName& BaseName)
	{
		return FPCGAttributeIdentifier(StripDomainFromName(BaseName), PCGMetadataDomainID::Elements);
	}

	bool HasAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata)
		{
			return false;
		}
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain))
		{
			return false;
		}
		return InMetadata->HasAttribute(Identifier);
	}

	const FPCGMetadataAttributeBase* TryGetConstAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata)
		{
			return nullptr;
		}
		// GetConstMetadataDomain returns null quietly for supported-but-never-instantiated domains,
		// unlike UPCGMetadata::GetConstAttribute which error-logs through WithConstMetadataDomain.
		const FPCGMetadataDomain* Domain = InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain);
		return Domain ? Domain->GetConstAttribute(Identifier.Name) : nullptr;
	}

	FString GetSelectorDisplayName(const FPCGAttributePropertyInputSelector& InSelector)
	{
		const FName Name = InSelector.GetName();
		if (Name.IsNone())
		{
			return TEXT("<none>");
		}

		const TArray<FString>& ExtraNames = InSelector.GetExtraNames();
		if (ExtraNames.IsEmpty())
		{
			return Name.ToString();
		}

		return Name.ToString() + TEXT(".") + FString::Join(ExtraNames, TEXT("."));
	}
}
