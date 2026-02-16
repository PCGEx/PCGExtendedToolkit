// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBondingRulesAssembler.h"

#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "PCGExProperty.h"

#define LOCTEXT_NAMESPACE "PCGExBondingRulesAssembler"

#pragma region FPCGExBondingRulesAssembler

FString FPCGExBondingRulesAssembler::FAssemblerModule::MakeKey() const
{
	const FPCGExValencyMaterialVariant* MaterialVariantPtr = Desc.bHasMaterialVariant ? &Desc.MaterialVariant : nullptr;
	return PCGExValency::MakeModuleKey(Desc.Asset.ToSoftObjectPath(), Desc.OrbitalMask, MaterialVariantPtr);
}

int32 FPCGExBondingRulesAssembler::AddModule(const FPCGExAssemblerModuleDesc& Desc)
{
	// Compute dedup key
	const FPCGExValencyMaterialVariant* MaterialVariantPtr = Desc.bHasMaterialVariant ? &Desc.MaterialVariant : nullptr;
	const FString Key = PCGExValency::MakeModuleKey(Desc.Asset.ToSoftObjectPath(), Desc.OrbitalMask, MaterialVariantPtr);

	if (const int32* ExistingIndex = KeyToIndex.Find(Key))
	{
		return *ExistingIndex;
	}

	const int32 NewIndex = Modules.Num();
	FAssemblerModule& NewModule = Modules.AddDefaulted_GetRef();
	NewModule.Desc = Desc;

	KeyToIndex.Add(Key, NewIndex);
	return NewIndex;
}

int32 FPCGExBondingRulesAssembler::AddModule(const TSoftObjectPtr<UObject>& Asset, int64 OrbitalMask)
{
	FPCGExAssemblerModuleDesc Desc;
	Desc.Asset = Asset;
	Desc.OrbitalMask = OrbitalMask;
	return AddModule(Desc);
}

void FPCGExBondingRulesAssembler::AddLocalTransform(int32 ModuleIndex, const FTransform& Transform)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].LocalTransforms.Add(Transform);
	}
}

void FPCGExBondingRulesAssembler::AddProperty(int32 ModuleIndex, const FInstancedStruct& Property)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].Properties.Add(Property);
	}
}

void FPCGExBondingRulesAssembler::AddTag(int32 ModuleIndex, FName Tag)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].Tags.AddUnique(Tag);
	}
}

void FPCGExBondingRulesAssembler::AddConnector(int32 ModuleIndex, const FPCGExValencyModuleConnector& Connector)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].Connectors.Add(Connector);
	}
}

void FPCGExBondingRulesAssembler::AddNeighbors(int32 ModuleIndex, const FName& OrbitalName, const TArray<int32>& NeighborModuleIndices)
{
	if (!Modules.IsValidIndex(ModuleIndex))
	{
		return;
	}

	TArray<int32>& Neighbors = Modules[ModuleIndex].OrbitalNeighbors.FindOrAdd(OrbitalName);
	for (const int32 NeighborIndex : NeighborModuleIndices)
	{
		Neighbors.AddUnique(NeighborIndex);
	}
}

void FPCGExBondingRulesAssembler::SetBoundaryOrbital(int32 ModuleIndex, int32 OrbitalBitIndex)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].BoundaryMask |= (1LL << OrbitalBitIndex);
	}
}

void FPCGExBondingRulesAssembler::SetWildcardOrbital(int32 ModuleIndex, int32 OrbitalBitIndex)
{
	if (Modules.IsValidIndex(ModuleIndex))
	{
		Modules[ModuleIndex].WildcardMask |= (1LL << OrbitalBitIndex);
	}
}

FPCGExAssemblerResult FPCGExBondingRulesAssembler::Validate(const UPCGExValencyOrbitalSet* OrbitalSet) const
{
	FPCGExAssemblerResult Result;
	Result.ModuleCount = Modules.Num();

	// Check all module indices in neighbor lists are in range
	for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
	{
		const FAssemblerModule& Module = Modules[ModuleIndex];

		for (const auto& Pair : Module.OrbitalNeighbors)
		{
			for (const int32 NeighborIndex : Pair.Value)
			{
				if (!Modules.IsValidIndex(NeighborIndex))
				{
					Result.Errors.Add(FText::Format(
						LOCTEXT("InvalidNeighborIndex", "Module {0}, orbital '{1}': neighbor index {2} is out of range (0..{3})."),
						FText::AsNumber(ModuleIndex),
						FText::FromName(Pair.Key),
						FText::AsNumber(NeighborIndex),
						FText::AsNumber(Modules.Num() - 1)
					));
				}
			}
		}
	}

	// Validate property type consistency
	ValidatePropertyTypes(Result.Errors);

	// Warn for orbitals in mask with no neighbors and no boundary/wildcard designation
	if (OrbitalSet)
	{
		for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
		{
			const FAssemblerModule& Module = Modules[ModuleIndex];
			const int64 OrbitalMask = Module.Desc.OrbitalMask;

			for (int32 Bit = 0; Bit < OrbitalSet->Num(); ++Bit)
			{
				if (!(OrbitalMask & (1LL << Bit)))
				{
					continue;
				}

				const bool bIsBoundary = (Module.BoundaryMask & (1LL << Bit)) != 0;
				const bool bIsWildcard = (Module.WildcardMask & (1LL << Bit)) != 0;

				if (bIsBoundary || bIsWildcard)
				{
					continue;
				}

				// Check if any orbital name for this bit index has neighbors
				const FName OrbitalName = OrbitalSet->Orbitals[Bit].GetOrbitalName();
				const TArray<int32>* Neighbors = Module.OrbitalNeighbors.Find(OrbitalName);

				if (!Neighbors || Neighbors->Num() == 0)
				{
					Result.Warnings.Add(FText::Format(
						LOCTEXT("OrbitalNoNeighborsAssembler", "Module {0}, orbital '{1}' (bit {2}): set in OrbitalMask but has no neighbors and is not boundary/wildcard."),
						FText::AsNumber(ModuleIndex),
						FText::FromName(OrbitalName),
						FText::AsNumber(Bit)
					));
				}
			}
		}
	}

	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

FPCGExAssemblerResult FPCGExBondingRulesAssembler::Apply(
	UPCGExValencyBondingRules* TargetRules,
	bool bClearExisting) const
{
	FPCGExAssemblerResult Result;

	if (!TargetRules)
	{
		Result.Errors.Add(LOCTEXT("NoTargetRulesAssembler", "No target BondingRules asset provided."));
		return Result;
	}

	// Validate first
	const FPCGExAssemblerResult ValidationResult = Validate(TargetRules->OrbitalSet);
	Result.Warnings = ValidationResult.Warnings;
	Result.Errors = ValidationResult.Errors;

	if (!ValidationResult.bSuccess)
	{
		return Result;
	}

	// Optionally clear existing modules
	if (bClearExisting)
	{
		TargetRules->Modules.Empty();
	}

	// Apply each assembled module
	for (const FAssemblerModule& SrcModule : Modules)
	{
		FPCGExValencyModuleDefinition& Dst = TargetRules->Modules.AddDefaulted_GetRef();
		ApplyModuleToDefinition(SrcModule, Dst);
	}

	// Compile
	if (!TargetRules->Compile())
	{
		Result.Errors.Add(LOCTEXT("CompileFailedAssembler", "Failed to compile BondingRules after assembly."));
		return Result;
	}

	// Rebuild generated collections
	TargetRules->RebuildGeneratedCollections();

	Result.bSuccess = true;
	Result.ModuleCount = Modules.Num();
	return Result;
}

int32 FPCGExBondingRulesAssembler::FindModule(
	const TSoftObjectPtr<UObject>& Asset,
	int64 OrbitalMask,
	const FPCGExValencyMaterialVariant* MaterialVariant) const
{
	const FString Key = PCGExValency::MakeModuleKey(Asset.ToSoftObjectPath(), OrbitalMask, MaterialVariant);
	if (const int32* Index = KeyToIndex.Find(Key))
	{
		return *Index;
	}
	return INDEX_NONE;
}

void FPCGExBondingRulesAssembler::ApplyModuleToDefinition(
	const FAssemblerModule& Src,
	FPCGExValencyModuleDefinition& Dst) const
{
	// Copy asset info
	Dst.Asset = Src.Desc.Asset;
	Dst.AssetType = Src.Desc.AssetType;

	// Copy settings
	Dst.Settings = Src.Desc.Settings;
	Dst.PlacementPolicy = Src.Desc.PlacementPolicy;
	Dst.ModuleName = Src.Desc.ModuleName;

	// Copy material variant
	if (Src.Desc.bHasMaterialVariant)
	{
		Dst.MaterialVariant = Src.Desc.MaterialVariant;
		Dst.bHasMaterialVariant = true;
	}

	// Copy local transforms
	for (const FTransform& Transform : Src.LocalTransforms)
	{
		Dst.AddLocalTransform(Transform);
	}

	// Copy properties, tags, connectors
	Dst.Properties = Src.Properties;
	Dst.Tags = Src.Tags;
	Dst.Connectors = Src.Connectors;

#if WITH_EDITOR
	// Generate variant name for editor debug display
	{
		FString AssetName = Src.Desc.Asset.GetAssetName();
		if (AssetName.IsEmpty()) { AssetName = TEXT("Unknown"); }

		int32 ConnectionCount = 0;
		int64 Mask = Src.Desc.OrbitalMask;
		while (Mask) { ConnectionCount += (Mask & 1); Mask >>= 1; }

		Dst.VariantName = FString::Printf(TEXT("%s_%dconn"), *AssetName, ConnectionCount);

		if (Dst.bHasLocalTransform)
		{
			Dst.VariantName += TEXT("_offset");
		}

		if (Dst.bHasMaterialVariant)
		{
			Dst.VariantName += TEXT("_mat");
		}
	}
#endif

	// Set up layer config
	FPCGExValencyModuleLayerConfig& LayerConfig = Dst.LayerConfig;
	LayerConfig.OrbitalMask = Src.Desc.OrbitalMask;
	LayerConfig.BoundaryOrbitalMask = Src.BoundaryMask;

	// Wildcard orbitals: set both WildcardMask AND OrbitalMask bits
	LayerConfig.WildcardOrbitalMask = Src.WildcardMask;
	LayerConfig.OrbitalMask |= Src.WildcardMask;

	// Populate neighbor relationships
	for (const auto& Pair : Src.OrbitalNeighbors)
	{
		FPCGExValencyNeighborIndices& Neighbors = LayerConfig.OrbitalNeighbors.FindOrAdd(Pair.Key);
		for (const int32 NeighborIndex : Pair.Value)
		{
			Neighbors.AddUnique(NeighborIndex);
		}
	}
}

bool FPCGExBondingRulesAssembler::ValidatePropertyTypes(TArray<FText>& OutErrors) const
{
	// Map: PropertyName -> (StructType, first module index that defined it)
	TMap<FName, TPair<const UScriptStruct*, int32>> NameToType;
	bool bValid = true;

	for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
	{
		const FAssemblerModule& Module = Modules[ModuleIndex];

		for (const FInstancedStruct& Prop : Module.Properties)
		{
			const FPCGExProperty* Base = Prop.GetPtr<FPCGExProperty>();
			if (!Base)
			{
				continue;
			}

			const FName PropName = Base->PropertyName;
			if (PropName.IsNone())
			{
				continue;
			}

			const UScriptStruct* PropType = Prop.GetScriptStruct();

			if (const auto* Existing = NameToType.Find(PropName))
			{
				if (Existing->Key != PropType)
				{
					OutErrors.Add(FText::Format(
						LOCTEXT("PropertyTypeConflictAssembler",
							"Property name '{0}' has conflicting types: '{1}' (module {2}) vs '{3}' (module {4})."),
						FText::FromName(PropName),
						FText::FromString(Existing->Key->GetName()),
						FText::AsNumber(Existing->Value),
						FText::FromString(PropType->GetName()),
						FText::AsNumber(ModuleIndex)
					));
					bValid = false;
				}
			}
			else
			{
				NameToType.Add(PropName, {PropType, ModuleIndex});
			}
		}
	}

	return bValid;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
