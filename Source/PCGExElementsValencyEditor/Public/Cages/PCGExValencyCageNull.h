// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyCageBase.h"
#include "Core/PCGExValencyCommon.h"

#include "PCGExValencyCageNull.generated.h"

class USphereComponent;

/**
 * A placeholder cage representing boundary, wildcard, or spatial placeholder constraints.
 *
 * Placeholder modes (see Orbital_Bitmask_Reference.md for mask behavior):
 * - Boundary: Orbital MUST have NO neighbor at runtime (BoundaryMask SET, OrbitalMask NOT set)
 * - Wildcard: Orbital MUST have ANY neighbor at runtime (WildcardMask SET, OrbitalMask SET)
 * - Any: No runtime constraint - pure spatial placeholder (neither mask set)
 *
 * Null cages can participate in pattern networks when near pattern cages,
 * enabling patterns like: A -> NULL(boundary) -> NULL(boundary) -> NULL(wildcard) -> B
 *
 * Visualized as a colored sphere based on mode:
 * - Red: Boundary
 * - Magenta: Wildcard
 * - Cyan: Any
 */
UCLASS(Blueprintable, meta = (DisplayName = "Valency Cage (Null)", IsBlueprintBase = "false"))
class PCGEXELEMENTSVALENCYEDITOR_API APCGExValencyCageNull : public APCGExValencyCageBase
{
	GENERATED_BODY()

public:
	APCGExValencyCageNull();

	//~ Begin APCGExValencyCageBase Interface
	virtual FString GetCageDisplayName() const override;
	virtual void SetDebugComponentsVisible(bool bVisible) override;
	virtual bool DetectNearbyConnections() override;
	//~ End APCGExValencyCageBase Interface

	/** Get the placeholder mode */
	EPCGExPlaceholderMode GetPlaceholderMode() const { return PlaceholderMode; }

	/** Check if this cage acts as a boundary (no neighbor allowed) */
	bool IsBoundaryMode() const { return PlaceholderMode == EPCGExPlaceholderMode::Boundary; }

	/** Check if this cage acts as a wildcard (any neighbor required) */
	bool IsWildcardMode() const { return PlaceholderMode == EPCGExPlaceholderMode::Wildcard; }

	/** Check if this cage acts as an "any" placeholder (no constraint) */
	bool IsAnyMode() const { return PlaceholderMode == EPCGExPlaceholderMode::Any; }

	/** Check if this cage is currently participating in pattern networks (transient state) */
	bool IsParticipatingInPatterns() const { return bIsParticipatingInPatterns; }

public:
	/**
	 * How this placeholder constrains connections at runtime.
	 * - Boundary: Orbital must have NO neighbor (current default)
	 * - Wildcard: Orbital must have ANY neighbor
	 * - Any: No constraint (spatial placeholder only)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placeholder", meta = (PCGEX_ValencyRebuild))
	EPCGExPlaceholderMode PlaceholderMode = EPCGExPlaceholderMode::Boundary;

	/**
	 * Optional description of this placeholder.
	 * E.g., "Edge of map", "Must connect to something", etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placeholder")
	FString Description;

protected:
	/** Sphere component for visualization and selection */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> DebugSphereComponent;

	/** Transient: Whether this cage is near pattern cages and participating in pattern networks */
	UPROPERTY(Transient)
	bool bIsParticipatingInPatterns = false;

	//~ Begin APCGExValencyCageBase Interface
	virtual bool ShouldConsiderCageForConnection(const APCGExValencyCageBase* CandidateCage) const override;
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End APCGExValencyCageBase Interface

	/** Update sphere color based on current mode */
	void UpdateVisualization();

	/** Check if any nearby cages are pattern cages (determines auto-participation) */
	bool HasNearbyPatternCages() const;
};
