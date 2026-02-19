// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExConnectorPatternAsset.h"

#pragma region FPCGExConnectorPatternSetCompiled

void FPCGExConnectorPatternSetCompiled::ResolveModuleNames(const TArray<FName>& RulesModuleNames)
{
	for (FPCGExConnectorPatternCompiled& Pattern : Patterns)
	{
		for (FPCGExConnectorPatternEntryCompiled& Entry : Pattern.Entries)
		{
			Entry.ModuleIndices.Reset();

			for (const FName& ModuleName : Entry.ModuleNames)
			{
				const int32 Idx = RulesModuleNames.IndexOfByKey(ModuleName);
				if (Idx != INDEX_NONE)
				{
					Entry.ModuleIndices.AddUnique(Idx);
				}
			}
		}
	}
}

#pragma endregion

#pragma region UPCGExConnectorPatternAsset

bool UPCGExConnectorPatternAsset::Compile(TArray<FText>* OutErrors)
{
	CompiledPatterns = FPCGExConnectorPatternSetCompiled();

	if (!ConnectorSet)
	{
		if (OutErrors) { OutErrors->Add(FText::FromString(TEXT("ConnectorSet is not assigned."))); }
		return false;
	}

	if (Patterns.IsEmpty())
	{
		return true; // Empty patterns = valid but nothing to match
	}

	CompiledPatterns.Patterns.Reserve(Patterns.Num());

	for (int32 PatternIdx = 0; PatternIdx < Patterns.Num(); ++PatternIdx)
	{
		const FPCGExConnectorPatternAuthored& Authored = Patterns[PatternIdx];

		if (Authored.Entries.IsEmpty())
		{
			if (OutErrors)
			{
				OutErrors->Add(FText::Format(
					FText::FromString(TEXT("Pattern '{0}' has no entries.")),
					FText::FromName(Authored.PatternName)));
			}
			continue;
		}

		FPCGExConnectorPatternCompiled Compiled;

		// Settings
		Compiled.Settings.PatternName = Authored.PatternName;
		Compiled.Settings.Weight = Authored.Weight;
		Compiled.Settings.MinMatches = Authored.MinMatches;
		Compiled.Settings.MaxMatches = Authored.MaxMatches;
		Compiled.Settings.bExclusive = Authored.bExclusive;
		Compiled.Settings.OutputStrategy = Authored.OutputStrategy;
		Compiled.Settings.TransformMode = Authored.TransformMode;
		Compiled.ConnectorSetRef = ConnectorSet;

		// Compile entries
		bool bPatternValid = true;
		int32 ActiveCount = 0;

		for (int32 EntryIdx = 0; EntryIdx < Authored.Entries.Num(); ++EntryIdx)
		{
			const FPCGExConnectorPatternEntryAuthored& AuthEntry = Authored.Entries[EntryIdx];
			FPCGExConnectorPatternEntryCompiled CompiledEntry;

			// Store module names for runtime resolution via ResolveModuleNames()
			CompiledEntry.ModuleNames = AuthEntry.ModuleNames;

			CompiledEntry.bIsActive = AuthEntry.bIsActive;
			if (AuthEntry.bIsActive) { ++ActiveCount; }

			// Build boundary connector mask
			for (const FName& TypeName : AuthEntry.BoundaryConnectorTypes)
			{
				const int32 TypeIdx = ConnectorSet->FindConnectorTypeIndex(TypeName);
				if (TypeIdx >= 0)
				{
					CompiledEntry.BoundaryConnectorMask |= (1LL << TypeIdx);
				}
				else if (OutErrors)
				{
					OutErrors->Add(FText::Format(
						FText::FromString(TEXT("Pattern '{0}', entry {1}: boundary connector type '{2}' not found in ConnectorSet.")),
						FText::FromName(Authored.PatternName),
						FText::AsNumber(EntryIdx),
						FText::FromName(TypeName)));
					bPatternValid = false;
				}
			}

			// Build wildcard connector mask
			for (const FName& TypeName : AuthEntry.WildcardConnectorTypes)
			{
				const int32 TypeIdx = ConnectorSet->FindConnectorTypeIndex(TypeName);
				if (TypeIdx >= 0)
				{
					CompiledEntry.WildcardConnectorMask |= (1LL << TypeIdx);
				}
				else if (OutErrors)
				{
					OutErrors->Add(FText::Format(
						FText::FromString(TEXT("Pattern '{0}', entry {1}: wildcard connector type '{2}' not found in ConnectorSet.")),
						FText::FromName(Authored.PatternName),
						FText::AsNumber(EntryIdx),
						FText::FromName(TypeName)));
					bPatternValid = false;
				}
			}

			// Validate mutual exclusivity
			if ((CompiledEntry.BoundaryConnectorMask & CompiledEntry.WildcardConnectorMask) != 0)
			{
				if (OutErrors)
				{
					OutErrors->Add(FText::Format(
						FText::FromString(TEXT("Pattern '{0}', entry {1}: boundary and wildcard masks overlap (mutually exclusive).")),
						FText::FromName(Authored.PatternName),
						FText::AsNumber(EntryIdx)));
				}
				bPatternValid = false;
			}

			// Compile adjacencies
			for (const FPCGExConnectorPatternAdjacencyAuthored& AuthAdj : AuthEntry.Adjacencies)
			{
				if (AuthAdj.TargetEntryIndex < 0 || AuthAdj.TargetEntryIndex >= Authored.Entries.Num())
				{
					if (OutErrors)
					{
						OutErrors->Add(FText::Format(
							FText::FromString(TEXT("Pattern '{0}', entry {1}: adjacency target index {2} out of range (0-{3}).")),
							FText::FromName(Authored.PatternName),
							FText::AsNumber(EntryIdx),
							FText::AsNumber(AuthAdj.TargetEntryIndex),
							FText::AsNumber(Authored.Entries.Num() - 1)));
					}
					bPatternValid = false;
					continue;
				}

				FPCGExConnectorPatternAdjacency CompiledAdj;
				CompiledAdj.TargetEntryIndex = AuthAdj.TargetEntryIndex;

				for (const FPCGExConnectorTypePairAuthored& AuthPair : AuthAdj.TypePairs)
				{
					FPCGExConnectorTypePair CompiledPair;

					// NAME_None = wildcard ("Any" pin in the graph editor)
					if (AuthPair.SourceType.IsNone())
					{
						CompiledPair.SourceTypeIndex = FPCGExConnectorTypePair::AnyTypeIndex;
					}
					else
					{
						CompiledPair.SourceTypeIndex = ConnectorSet->FindConnectorTypeIndex(AuthPair.SourceType);
						if (CompiledPair.SourceTypeIndex < 0)
						{
							if (OutErrors)
							{
								OutErrors->Add(FText::Format(
									FText::FromString(TEXT("Pattern '{0}', entry {1}: source connector type '{2}' not found.")),
									FText::FromName(Authored.PatternName),
									FText::AsNumber(EntryIdx),
									FText::FromName(AuthPair.SourceType)));
							}
							bPatternValid = false;
							continue;
						}
					}

					if (AuthPair.TargetType.IsNone())
					{
						CompiledPair.TargetTypeIndex = FPCGExConnectorTypePair::AnyTypeIndex;
					}
					else
					{
						CompiledPair.TargetTypeIndex = ConnectorSet->FindConnectorTypeIndex(AuthPair.TargetType);
						if (CompiledPair.TargetTypeIndex < 0)
						{
							if (OutErrors)
							{
								OutErrors->Add(FText::Format(
									FText::FromString(TEXT("Pattern '{0}', entry {1}: target connector type '{2}' not found.")),
									FText::FromName(Authored.PatternName),
									FText::AsNumber(EntryIdx),
									FText::FromName(AuthPair.TargetType)));
							}
							bPatternValid = false;
							continue;
						}
					}

					CompiledAdj.TypePairs.Add(CompiledPair);
				}

				if (CompiledAdj.TypePairs.Num() > 0)
				{
					CompiledEntry.Adjacencies.Add(MoveTemp(CompiledAdj));
				}
			}

			Compiled.Entries.Add(MoveTemp(CompiledEntry));
		}

		if (!bPatternValid)
		{
			continue;
		}

		Compiled.ActiveEntryCount = ActiveCount;

		// Categorize as exclusive or additive
		const int32 CompiledIdx = CompiledPatterns.Patterns.Num();
		CompiledPatterns.Patterns.Add(MoveTemp(Compiled));

		if (Authored.bExclusive)
		{
			CompiledPatterns.ExclusivePatternIndices.Add(CompiledIdx);
		}
		else
		{
			CompiledPatterns.AdditivePatternIndices.Add(CompiledIdx);
		}
	}

	return true;
}

#if WITH_EDITOR
void UPCGExConnectorPatternAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Auto-compile on any property change
	TArray<FText> Errors;
	Compile(&Errors);

	// Log errors
	for (const FText& Error : Errors)
	{
		UE_LOG(LogTemp, Warning, TEXT("ConnectorPattern Compile: %s"), *Error.ToString());
	}
}
#endif

void UPCGExConnectorPatternAsset::PostLoad()
{
	Super::PostLoad();

	// Ensure compiled data is available after loading
	if (!IsCompiled() && ConnectorSet && Patterns.Num() > 0)
	{
		Compile();
	}
}

#pragma endregion
