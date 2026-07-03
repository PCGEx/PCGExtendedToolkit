// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Grammit/PCGExGrammitExport.h"

#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetGrammar.h"
#include "Elements/Grammar/PCGSubdivisionBase.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace PCGExGrammitExport
{
	namespace
	{
		constexpr const TCHAR* GrammitBaseURL = TEXT("https://pcgex.github.io/grammit/");

		// Browsers and OS shells truncate hash fragments past various thresholds; ~64KB is
		// the lowest known ceiling. Warn but still attempt the launch beyond this.
		constexpr int32 UrlLengthWarnThreshold = 60000;

		const TCHAR* MoleculeSwatches[] = {
			TEXT("#7c9cff"), TEXT("#ff9a7c"), TEXT("#9affb1"), TEXT("#ffd479"),
			TEXT("#c79aff"), TEXT("#7ce3ff"), TEXT("#ff7ca8"), TEXT("#b9ff7c"),
		};

		FString PickSwatch(int32 Index)
		{
			return MoleculeSwatches[Index % UE_ARRAY_COUNT(MoleculeSwatches)];
		}

		FString LinearColorToHex(const FLinearColor& In)
		{
			const FColor C = In.ToFColor(true);
			return FString::Printf(TEXT("#%02x%02x%02x"), C.R, C.G, C.B);
		}

		FString EscapeJsonString(const FString& In)
		{
			FString Out;
			Out.Reserve(In.Len() + 2);
			for (TCHAR Ch : In)
			{
				switch (Ch)
				{
				case TEXT('\\'):
					Out += TEXT("\\\\");
					break;
				case TEXT('"'):
					Out += TEXT("\\\"");
					break;
				case TEXT('\b'):
					Out += TEXT("\\b");
					break;
				case TEXT('\f'):
					Out += TEXT("\\f");
					break;
				case TEXT('\n'):
					Out += TEXT("\\n");
					break;
				case TEXT('\r'):
					Out += TEXT("\\r");
					break;
				case TEXT('\t'):
					Out += TEXT("\\t");
					break;
				default:
					if (Ch < 0x20)
					{
						Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Ch));
					}
					else
					{
						Out.AppendChar(Ch);
					}
					break;
				}
			}
			return Out;
		}

		// Doubles that happen to be integral render as e.g. "100" rather than "100.0" to keep
		// the encoded payload short. Non-integral values fall back to UE's sanitized formatter.
		FString FormatNumber(double Value)
		{
			if (FMath::IsFinite(Value) && FMath::Floor(Value) == Value && FMath::Abs(Value) < 1e15)
			{
				return FString::Printf(TEXT("%lld"), static_cast<int64>(Value));
			}
			return FString::SanitizeFloat(Value);
		}

		FString BytesToBase64Url(const TArray<uint8>& Bytes)
		{
			FString B64 = FBase64::Encode(Bytes);
			B64.ReplaceInline(TEXT("+"), TEXT("-"));
			B64.ReplaceInline(TEXT("/"), TEXT("_"));
			while (B64.EndsWith(TEXT("=")))
			{
				B64.LeftChopInline(1);
			}
			return B64;
		}

		void Notify(const FString& Message, SNotificationItem::ECompletionState State)
		{
			FNotificationInfo Info(FText::FromString(Message));
			Info.ExpireDuration = 4.0f;
			Info.bUseSuccessFailIcons = true;
			Info.bFireAndForget = true;
			if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(State);
			}
		}

		struct FAtomOut
		{
			FString Name;
			FString Color;
			double Size = 0.0;
			bool bScalable = false;
			// Non-empty only when Name differs from the underlying grammar symbol (per-axis
			// suffixed module). Encoded as the optional 5th tuple element so the grammar
			// evaluator on the web side sees the bare symbol regardless of which axis-module ran.
			FString ExportSymbol;
		};

		// Mirrors FPCGExCollectionToModuleInfosElement::FlattenCollection: walks the cache
		// (so Weight=0 / invalid entries are filtered out the same way the runtime pipeline
		// filters them), delegates per-entry resolution to FixModuleInfos so all SubGrammar
		// modes (Inherit/Override) and GrammarSource (Local/Global) are handled identically
		// to the runtime, and recurses on Flatten. FlattenStack guards against cycles.
		void EmitEntries(
			const UPCGExAssetCollection* InCollection,
			TSet<const UPCGExAssetCollection*>& FlattenStack,
			TArray<FAtomOut>& OutAtoms,
			TArray<FName>& OutCategoryOrder,
			TMap<FName, TArray<int32>>& OutCategoryMembers)
		{
			if (!InCollection)
			{
				return;
			}

			const PCGExAssetCollection::FCache* Cache = const_cast<UPCGExAssetCollection*>(InCollection)->LoadCache();
			if (!Cache || !Cache->Main)
			{
				return;
			}

			const int32 NumEntries = Cache->Main->Order.Num();
			for (int32 i = 0; i < NumEntries; ++i)
			{
				const FPCGExEntryAccessResult Result = InCollection->GetEntryAt(i);
				const FPCGExAssetCollectionEntry* Entry = Result.Entry;
				if (!Entry)
				{
					continue;
				}

				if (Entry->bIsSubCollection && Entry->SubGrammarMode == EPCGExGrammarSubCollectionMode::Flatten)
				{
					const UPCGExAssetCollection* Inner = Entry->GetSubCollectionPtr();
					if (Inner && !FlattenStack.Contains(Inner))
					{
						FlattenStack.Add(Inner);
						EmitEntries(Inner, FlattenStack, OutAtoms, OutCategoryOrder, OutCategoryMembers);
						FlattenStack.Remove(Inner);
					}
					continue;
				}

				// Resolve the entry's effective grammar struct -- gives us shared Symbol / DebugColor /
				// Axes without paying for a per-axis Fix call unless the axis is actually emitted.
				const FPCGExAssetGrammarDetails* Grammar = Entry->GetEffectiveGrammar(InCollection);
				if (!Grammar || Grammar->Axes == 0)
				{
					continue;
				}

				// Empty / None Symbol = opt-out of grammar export.
				if (Grammar->Symbol.IsNone())
				{
					continue;
				}
				const FString BaseSymbolStr = Grammar->Symbol.ToString();
				if (BaseSymbolStr.IsEmpty())
				{
					continue;
				}

				// Read DebugColor directly from the source grammar struct rather than from
				// Submodule.DebugColor -- that field is FVector4 and the FLinearColor → FVector4
				// assignment inside Fix() goes silently wrong on some collection types (the
				// resulting V.X/Y/Z come through as 1.0 even when the user picked a color).
				// Native FLinearColor read is unambiguous.
				const FLinearColor SourceColor = Grammar->DebugColor;

				// Treat pure-white (the default DebugColor) as "user didn't pick one" and
				// substitute a hue derived from the symbol, so atoms in Grammit aren't all
				// identical pills AND the same symbol always renders the same color across
				// re-exports. Hash off BaseSymbolStr (pre-suffix) so per-axis siblings share a hue.
				const bool bIsDefaultWhite =
					FMath::IsNearlyEqual(SourceColor.R, 1.0f) &&
					FMath::IsNearlyEqual(SourceColor.G, 1.0f) &&
					FMath::IsNearlyEqual(SourceColor.B, 1.0f);

				const FLinearColor DebugLC = bIsDefaultWhite
					? FLinearColor::MakeFromHSV8(static_cast<uint8>(GetTypeHash(BaseSymbolStr) & 0xFF), 180, 220)
					: SourceColor;
				const FString ColorHex = LinearColorToHex(DebugLC);

				// Multi-axis entries emit a suffixed module per axis (Grammit has no attribute concept,
				// so distinct modules are how we surface per-axis size differences); all axes share the
				// same exportSymbol so the bare grammar symbol is what the evaluator emits.
				const bool bSuffix = PCGExGrammarAxes::CountAxes(Grammar->Axes) > 1;
				const bool bIsSub = Entry->bIsSubCollection;

				for (int32 a = 0; a < 3; ++a)
				{
					if (!(Grammar->Axes & static_cast<uint8>(PCGExGrammarAxes::Bits[a])))
					{
						continue;
					}

					FPCGSubdivisionSubmodule Submodule;
					const bool bFixed = bIsSub
						? Grammar->FixSubCollection(Entry->SubCollection, PCGExGrammarAxes::Bits[a], Submodule, /*SizeCache=*/nullptr)
						: Grammar->FixLeaf(Entry->Staging.Bounds, PCGExGrammarAxes::Bits[a], Submodule);
					if (!bFixed)
					{
						continue;
					}

					FAtomOut Atom;
					Atom.Name = bSuffix ? (BaseSymbolStr + PCGExGrammarAxes::Suffixes[a]) : BaseSymbolStr;
					Atom.ExportSymbol = bSuffix ? BaseSymbolStr : FString();
					Atom.Color = ColorHex;
					Atom.Size = Submodule.Size;
					Atom.bScalable = Submodule.bScalable;

					const int32 AtomIdx = OutAtoms.Add(MoveTemp(Atom));

					if (!Entry->Category.IsNone())
					{
						TArray<int32>& Bucket = OutCategoryMembers.FindOrAdd(Entry->Category);
						if (Bucket.Num() == 0)
						{
							OutCategoryOrder.Add(Entry->Category);
						}
						Bucket.Add(AtomIdx);
					}
				}
			}
		}

	}

	void OpenInGrammit(const UPCGExAssetCollection* InCollection)
	{
		if (!InCollection)
		{
			Notify(TEXT("Open in Grammit: no collection."), SNotificationItem::CS_Fail);
			return;
		}

		TArray<FAtomOut> Atoms;
		Atoms.Reserve(InCollection->NumEntries());

		// First-seen category ordering keeps the on-the-web layout stable across exports.
		TArray<FName> CategoryOrder;
		TMap<FName, TArray<int32>> CategoryMembers;
		TSet<const UPCGExAssetCollection*> FlattenStack;

		EmitEntries(InCollection, FlattenStack, Atoms, CategoryOrder, CategoryMembers);

		if (Atoms.Num() == 0)
		{
			Notify(TEXT("Open in Grammit: collection has no exportable entries."), SNotificationItem::CS_Fail);
			return;
		}

		// Build v2 compact payload: {"v":2,"A":[...],"M":[...]}
		// Atom tuple: [name, color, size, scalable, exportSymbol?]  (5th omitted when == name)
		// Molecule tuple: [name, kindCode, color, children[]]
		// M[0] is always MAIN (empty per design); subsequent are sequence-kind category groups.
		// AtomRef inside a molecule: minimal form [0, atomIdx] (repCode=0, weight=1, alias="" all trimmed).
		FString Json;
		Json.Reserve(256 + Atoms.Num() * 48 + CategoryOrder.Num() * 32);
		Json += TEXT("{\"v\":2,\"A\":[");
		for (int32 i = 0; i < Atoms.Num(); ++i)
		{
			if (i > 0)
			{
				Json.AppendChar(',');
			}
			const FAtomOut& A = Atoms[i];
			if (!A.ExportSymbol.IsEmpty() && A.ExportSymbol != A.Name)
			{
				Json += FString::Printf(
					TEXT("[\"%s\",\"%s\",%s,%d,\"%s\"]"),
					*EscapeJsonString(A.Name),
					*A.Color,
					*FormatNumber(A.Size),
					A.bScalable ? 1 : 0,
					*EscapeJsonString(A.ExportSymbol));
			}
			else
			{
				Json += FString::Printf(
					TEXT("[\"%s\",\"%s\",%s,%d]"),
					*EscapeJsonString(A.Name),
					*A.Color,
					*FormatNumber(A.Size),
					A.bScalable ? 1 : 0);
			}
		}
		Json += TEXT("],\"M\":[");

		// MAIN is required as M[0] by the Grammit schema; left empty so users wire it up
		// themselves on the web side rather than guessing at structure here.
		Json += FString::Printf(TEXT("[\"main\",0,\"%s\",[]]"), *PickSwatch(0));

		for (int32 mi = 0; mi < CategoryOrder.Num(); ++mi)
		{
			Json.AppendChar(',');
			const FName& Cat = CategoryOrder[mi];
			const TArray<int32>& Members = CategoryMembers[Cat];
			Json += FString::Printf(
				TEXT("[\"%s\",1,\"%s\",["),
				*EscapeJsonString(Cat.ToString()),
				*PickSwatch(mi + 1));
			for (int32 j = 0; j < Members.Num(); ++j)
			{
				if (j > 0)
				{
					Json.AppendChar(',');
				}
				Json += FString::Printf(TEXT("[0,%d]"), Members[j]);
			}
			Json += TEXT("]]");
		}
		Json += TEXT("]}");

		const FTCHARToUTF8 Utf8(*Json);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		const FString Payload = BytesToBase64Url(Bytes);

		const FString Url = FString::Printf(TEXT("%s#Snapshot=%s"), GrammitBaseURL, *Payload);

		if (Url.Len() > UrlLengthWarnThreshold)
		{
			Notify(
				FString::Printf(TEXT("Open in Grammit: payload is large (%d chars); some browsers may truncate."), Url.Len()),
				SNotificationItem::CS_Pending);
		}

		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);

		Notify(
			FString::Printf(TEXT("Opened Grammit: %d atom(s), %d group(s)."), Atoms.Num(), CategoryOrder.Num()),
			SNotificationItem::CS_Success);
	}
}
