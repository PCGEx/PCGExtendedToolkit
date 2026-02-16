// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExValencyConnectorSet.generated.h"

/**
 * Connector polarity - determines which other polarities a connector can connect to.
 */
UENUM(BlueprintType)
enum class EPCGExConnectorPolarity : uint8
{
	Universal UMETA(DisplayName = "Universal", ToolTip = "Connects to any polarity"),
	Plug      UMETA(DisplayName = "Plug",      ToolTip = "Outward — connects to Port or Universal"),
	Port      UMETA(DisplayName = "Port",      ToolTip = "Inward — connects to Plug or Universal")
};

namespace PCGExValencyConnector
{
	FORCEINLINE bool ArePolaritiesCompatible(EPCGExConnectorPolarity A, EPCGExConnectorPolarity B)
	{
		if (A == EPCGExConnectorPolarity::Universal || B == EPCGExConnectorPolarity::Universal) return true;
		return A != B; // Plug ↔ Port
	}

	/** 64 visually distinct ASCII placeholder icons for connector types.
	 * Will be replaced with SVG/brush icons later — swap GetIconChar() for FSlateBrush* lookup. */
	inline constexpr TCHAR IconChars[64] = {
		'*', '+', '#', '@', '$', '&', '!', '~', '^', '%', '=', '?', '>', '<',
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P',
		'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'd', 'e', 'f', 'g', 'h', 'k', 'm', 'n', 'p', 'q', 'r', 't',
		'w', 'x',
	};
	inline constexpr int32 NumIcons = 64;

	/** Get the icon character for an index (returns '?' for out of range). */
	FORCEINLINE TCHAR GetIconChar(int32 Index)
	{
		return (Index >= 0 && Index < NumIcons) ? IconChars[Index] : TEXT('?');
	}
}

/**
 * Packed connector reference utilities.
 * Connectors are packed into int64 for attribute storage:
 *   - Bits 0-15:  Connector Set asset index (in context's asset registry)
 *   - Bits 16-31: Connector entry index within the set
 *   - Bits 32-63: Reserved for future use
 */
namespace PCGExValencyConnector
{
	/** Invalid connector reference sentinel */
	constexpr int64 INVALID_CONNECTOR = -1;

	/** Pack a connector reference into int64 */
	FORCEINLINE int64 Pack(uint16 RulesIndex, uint16 ConnectorIndex)
	{
		return static_cast<int64>(RulesIndex) | (static_cast<int64>(ConnectorIndex) << 16);
	}

	/** Extract the rules asset index from a packed connector reference */
	FORCEINLINE uint16 GetRulesIndex(int64 Packed)
	{
		return static_cast<uint16>(Packed & 0xFFFF);
	}

	/** Extract the connector entry index from a packed connector reference */
	FORCEINLINE uint16 GetConnectorIndex(int64 Packed)
	{
		return static_cast<uint16>((Packed >> 16) & 0xFFFF);
	}

	/** Check if a packed reference is valid */
	FORCEINLINE bool IsValid(int64 Packed)
	{
		return Packed != INVALID_CONNECTOR;
	}
}

/**
 * How instance constraint overrides interact with type-level defaults.
 */
UENUM(BlueprintType)
enum class EPCGExConstraintOverrideMode : uint8
{
	Append  UMETA(DisplayName = "Append",  ToolTip = "Run type defaults first, then instance constraints continue the pipeline"),
	Replace UMETA(DisplayName = "Replace", ToolTip = "Skip type defaults, use only instance constraints")
};

/**
 * Constraint role — determines how a constraint participates in the placement pipeline.
 */
UENUM(BlueprintType)
enum class EPCGExConstraintRole : uint8
{
	Generator UMETA(DisplayName = "Generator", ToolTip = "Produces multiple transform variants from a base"),
	Modifier  UMETA(DisplayName = "Modifier",  ToolTip = "Applies offset/jitter to each variant"),
	Filter    UMETA(DisplayName = "Filter",    ToolTip = "Rejects invalid variants"),
	Preset    UMETA(DisplayName = "Preset",    ToolTip = "Expands to inline sub-pipeline"),
	Branch    UMETA(DisplayName = "Branch",    ToolTip = "Conditional fork-join")
};

// Forward declaration
struct FPCGExConstraintContext;

/**
 * Base struct for connector constraints.
 * Subclass to define specific constraint types (angular range, surface offset, volume, etc.).
 * Constraints can be set as type-level defaults on FPCGExValencyConnectorEntry
 * and overridden per-instance on FPCGExValencyModuleConnector.
 *
 * Each constraint has a role (Generator/Modifier/Filter) that determines its place
 * in the placement pipeline:
 * 1. Generators produce N transform variants from a base transform
 * 2. Modifiers apply offsets to each variant
 * 3. Filters reject invalid variants
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorConstraint
{
	GENERATED_BODY()

	virtual ~FPCGExConnectorConstraint() = default;

	/** Whether this constraint is active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	bool bEnabled = true;

	/** Get the role of this constraint in the placement pipeline */
	virtual EPCGExConstraintRole GetRole() const { return EPCGExConstraintRole::Filter; }

	/** Max number of variants this constraint can produce (generators only) */
	virtual int32 GetMaxVariants() const { return 1; }

	/** Generate transform variants from the base attachment transform (generators only) */
	virtual void GenerateVariants(
		const FPCGExConstraintContext& Context,
		FRandomStream& Random,
		TArray<FTransform>& OutVariants) const
	{
	}

	/** Apply modification to a transform in-place (modifiers only) */
	virtual void ApplyModification(
		const FPCGExConstraintContext& Context,
		FTransform& InOutTransform,
		FRandomStream& Random) const
	{
	}

	/** Check if a candidate transform is valid (filters only). Return false to reject. */
	virtual bool IsValid(
		const FPCGExConstraintContext& Context,
		const FTransform& CandidateTransform) const
	{
		return true;
	}
};

/**
 * Intermediate base for Generator constraints.
 * Subclass this when creating constraints that produce multiple transform variants.
 */
USTRUCT(BlueprintType, meta=(Hidden))
struct PCGEXELEMENTSVALENCY_API FPCGExConstraintGenerator : public FPCGExConnectorConstraint
{
	GENERATED_BODY()
	virtual EPCGExConstraintRole GetRole() const override final { return EPCGExConstraintRole::Generator; }
};

/**
 * Intermediate base for Modifier constraints.
 * Subclass this when creating constraints that apply offsets/jitter to variants.
 */
USTRUCT(BlueprintType, meta=(Hidden))
struct PCGEXELEMENTSVALENCY_API FPCGExConstraintModifier : public FPCGExConnectorConstraint
{
	GENERATED_BODY()
	virtual EPCGExConstraintRole GetRole() const override final { return EPCGExConstraintRole::Modifier; }
};

/**
 * Intermediate base for Filter constraints.
 * Subclass this when creating constraints that reject invalid variants.
 */
USTRUCT(BlueprintType, meta=(Hidden))
struct PCGEXELEMENTSVALENCY_API FPCGExConstraintFilter : public FPCGExConnectorConstraint
{
	GENERATED_BODY()
	virtual EPCGExConstraintRole GetRole() const override final { return EPCGExConstraintRole::Filter; }
};

/**
 * A single connector type definition.
 * Connectors are abstract connection points - they ARE orbitals with non-directional identity.
 * Unlike directional orbitals, connectors are matched by type rather than spatial direction.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyConnectorEntry
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/**
	 * Stable identity for this connector type.
	 * Auto-generated, preserved through name changes.
	 * Used by CompatibleTypeIds for rename-safe references.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Internal", meta = (IgnoreForMemberInitializationTest))
	int32 TypeId = 0;
#endif

	/** Connector type name - used for compatibility matrix lookup and matching */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName ConnectorType;

#if WITH_EDITORONLY_DATA
	/**
	 * Connector types this type is compatible with (stored by TypeId).
	 * Displayed as type names in the editor via custom UI.
	 * Compiled to CompatibilityMatrix bitmask at runtime.
	 */
	UPROPERTY(EditAnywhere, Category = "Compatibility")
	TArray<int32> CompatibleTypeIds;
#endif

	/** Default transform offset relative to module origin (can be overridden per-module) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FTransform DefaultOffset = FTransform::Identity;

	/** Debug visualization color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FLinearColor DebugColor = FLinearColor::White;

	/** Default constraints applied to all connectors of this type (can be overridden per-instance) */
	UPROPERTY(EditAnywhere, Category = "Constraints", meta=(BaseStruct="/Script/PCGExElementsValency.PCGExConnectorConstraint", ExcludeBaseStruct))
	TArray<FInstancedStruct> DefaultConstraints;

#if WITH_EDITORONLY_DATA
	/** Visual icon index (0-63). -1 = auto-assign from array position. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = -1, ClampMax = 63))
	int32 IconIndex = -1;
#endif

	/** Bit index in compatibility mask (0-63, assigned at compile time) */
	int32 BitIndex = -1;

	FPCGExValencyConnectorEntry()
	{
#if WITH_EDITOR
		TypeId = GetTypeHash(FGuid::NewGuid());
#endif
	}

	bool operator==(const FPCGExValencyConnectorEntry& Other) const
	{
		return ConnectorType == Other.ConnectorType;
	}
};

/**
 * Data asset defining connector types and their compatibility rules.
 * Analogous to UPCGExValencyOrbitalSet but for non-directional connectors.
 *
 * Connectors enable:
 * - Chained solving: Solve A -> WriteModuleConnectors -> Solve B with different rules
 * - Mesh socket extraction: UStaticMesh sockets as connection points
 * - Non-directional module connections
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Valency | Connector Set")
class PCGEXELEMENTSVALENCY_API UPCGExValencyConnectorSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Layer name - for multi-layer connector systems, determines attribute naming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName LayerName = FName("Main");

	/**
	 * Connector type definitions (max 64).
	 * Each connector type can be marked compatible with other types via the compatibility matrix.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (TitleProperty = "{ConnectorType}"))
	TArray<FPCGExValencyConnectorEntry> ConnectorTypes;

	/**
	 * Compatibility matrix as bitmasks.
	 * Index = ConnectorTypeIndex, Value = bitmask of compatible connector types.
	 * If bit N is set in CompatibilityMatrix[M], connector type M can connect to connector type N.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<int64> CompatibilityMatrix;

	/** Get the number of connector types */
	int32 Num() const { return ConnectorTypes.Num(); }

	/** Check if an index is valid */
	bool IsValidIndex(int32 Index) const { return ConnectorTypes.IsValidIndex(Index); }

	/** Get the attribute name for connector references on edges */
	FName GetConnectorAttributeName() const
	{
		return FName(FString::Printf(TEXT("PCGEx/V/Connector/%s"), *LayerName.ToString()));
	}

	/**
	 * Find connector type index by name.
	 * @param ConnectorType The connector type name to find
	 * @return Index in ConnectorTypes array, or INDEX_NONE if not found
	 */
	int32 FindConnectorTypeIndex(FName ConnectorType) const;

	/**
	 * Check if two connector types are compatible.
	 * @param TypeIndexA First connector type index
	 * @param TypeIndexB Second connector type index
	 * @return True if the types can connect
	 */
	bool AreTypesCompatible(int32 TypeIndexA, int32 TypeIndexB) const;

	/**
	 * Get the compatibility mask for a connector type.
	 * @param TypeIndex Connector type index
	 * @return Bitmask of compatible types (bit N set = compatible with type N)
	 */
	int64 GetCompatibilityMask(int32 TypeIndex) const;

	/**
	 * Compile connector definitions - assigns BitIndex to each connector type.
	 * Called automatically when needed, but can be called explicitly after modifications.
	 */
	void Compile();

	/**
	 * Validate the connector set - checks for duplicates and valid configuration.
	 * @param OutErrors Array to receive error messages
	 * @return True if validation passed
	 */
	bool Validate(TArray<FText>& OutErrors) const;

	/**
	 * Set compatibility between two connector types.
	 * @param TypeIndexA First connector type index
	 * @param TypeIndexB Second connector type index
	 * @param bBidirectional If true, sets compatibility in both directions
	 */
	void SetCompatibility(int32 TypeIndexA, int32 TypeIndexB, bool bBidirectional = true);

	/**
	 * Clear all compatibility rules.
	 */
	void ClearCompatibility();

	/**
	 * Initialize the compatibility matrix with self-compatible types.
	 * Each connector type is compatible only with itself.
	 */
	void InitializeSelfCompatible();

#if WITH_EDITOR
	/** Get the effective icon index for a connector type (resolves auto-assign from IconIndex or array position). */
	int32 GetEffectiveIconIndex(int32 TypeArrayIndex) const
	{
		if (ConnectorTypes.IsValidIndex(TypeArrayIndex))
		{
			const int32 EntryIcon = ConnectorTypes[TypeArrayIndex].IconIndex;
			if (EntryIcon >= 0 && EntryIcon < PCGExValencyConnector::NumIcons)
			{
				return EntryIcon;
			}
		}
		return TypeArrayIndex;
	}

	/**
	 * Find connector type index by TypeId.
	 * @param TypeId The stable type identifier
	 * @return Index in ConnectorTypes array, or INDEX_NONE if not found
	 */
	int32 FindConnectorTypeIndexById(int32 TypeId) const;

	/**
	 * Get connector type name by TypeId.
	 * @param TypeId The stable type identifier
	 * @return Connector type name, or NAME_None if not found
	 */
	FName GetConnectorTypeNameById(int32 TypeId) const;

	/**
	 * Build compatibility matrix from CompatibleTypeIds on each connector definition.
	 * Called during Compile() to convert user-friendly data to runtime bitmask.
	 */
	void BuildCompatibilityMatrixFromTypeIds();

	/**
	 * Initialize all connector types as self-compatible using CompatibleTypeIds.
	 * Sets each type's CompatibleTypeIds to contain only its own TypeId.
	 */
	void InitializeSelfCompatibleTypeIds();

	/**
	 * Make all connector types compatible with each other using CompatibleTypeIds.
	 * Sets each type's CompatibleTypeIds to contain all TypeIds.
	 */
	void InitializeAllCompatibleTypeIds();
#endif

	/**
	 * Find a matching connector type for a mesh socket based on name and tag.
	 * Used for auto-extraction of sockets from static meshes.
	 *
	 * Matching priority:
	 * 1. If MeshSocketTag exactly matches a ConnectorType name -> return that type
	 * 2. If MeshSocketName exactly matches a ConnectorType name -> return that type
	 * 3. If MeshSocketName starts with a ConnectorType name (e.g., "Door_Left" matches "Door") -> return that type
	 *
	 * @param MeshSocketName The UStaticMeshSocket's SocketName
	 * @param MeshSocketTag The UStaticMeshSocket's Tag (optional)
	 * @return Matching connector type name, or NAME_None if no match
	 */
	FName FindMatchingConnectorType(const FName& MeshSocketName, const FString& MeshSocketTag) const;

	/**
	 * Check if a mesh socket name/tag matches any connector type definition.
	 * @param MeshSocketName The UStaticMeshSocket's SocketName
	 * @param MeshSocketTag The UStaticMeshSocket's Tag (optional)
	 * @return True if a matching connector type exists
	 */
	bool MatchesMeshSocket(const FName& MeshSocketName, const FString& MeshSocketTag) const
	{
		return !FindMatchingConnectorType(MeshSocketName, MeshSocketTag).IsNone();
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/**
 * A connector instance on a module.
 * Represents a specific connector connection point with optional transform override.
 *
 * Connectors map to orbital indices during compilation:
 * - Each unique connector type gets an orbital index (0-63)
 * - The solver then works identically to directional orbitals
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSVALENCY_API FPCGExValencyModuleConnector
{
	GENERATED_BODY()

	/** Connector identifier (unique per module, used for socket matching and pipeline output) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName Identifier;

	/** Connector type (references ConnectorSet.ConnectorTypes) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName ConnectorType;

	/** Transform offset relative to module (used if bOverrideOffset is true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "bOverrideOffset"))
	FTransform LocalOffset = FTransform::Identity;

	/** Whether LocalOffset overrides the ConnectorSet default offset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bOverrideOffset = false;

	/** Connector polarity - determines connection compatibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExConnectorPolarity Polarity = EPCGExConnectorPolarity::Universal;

	/** Per-instance constraint overrides (set by builder from cage connector component) */
	UPROPERTY()
	TArray<FInstancedStruct> ConstraintOverrides;

	/** How instance overrides interact with type-level default constraints */
	UPROPERTY()
	EPCGExConstraintOverrideMode OverrideMode = EPCGExConstraintOverrideMode::Append;

	/** Whether to manually assign orbital index instead of direction-based detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, AdvancedDisplay)
	bool bManualOrbitalOverride = false;

	/** Manual orbital index (0-63). Only used when bManualOrbitalOverride is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, AdvancedDisplay,
		meta = (EditCondition = "bManualOrbitalOverride", ClampMin = "0", ClampMax = "63"))
	int32 ManualOrbitalIndex = 0;

	/** Orbital index this connector maps to (assigned during compilation, runtime only) */
	int32 OrbitalIndex = -1;

	/** Check if this connector has been assigned an orbital index */
	bool IsCompiled() const { return OrbitalIndex >= 0; }

	/**
	 * Get the effective transform offset.
	 * @param ConnectorSet The connector set asset containing default offsets
	 * @return LocalOffset if bOverrideOffset is true, otherwise the default from ConnectorSet
	 */
	FTransform GetEffectiveOffset(const UPCGExValencyConnectorSet* ConnectorSet) const;

	bool operator==(const FPCGExValencyModuleConnector& Other) const
	{
		return Identifier == Other.Identifier && ConnectorType == Other.ConnectorType;
	}
};

/** Hash function for FPCGExValencyModuleConnector */
FORCEINLINE uint32 GetTypeHash(const FPCGExValencyModuleConnector& Connector)
{
	return HashCombine(GetTypeHash(Connector.Identifier), GetTypeHash(Connector.ConnectorType));
}
