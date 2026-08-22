// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "PCGExClusterSketchAuthoringSettings.generated.h"

/**
 * What the authoring GESTURES do on the user's behalf, where UPCGExClusterSketchStyleSettings is only
 * how a sketch looks. Surfaced both in Editor Preferences and on the sketch panel's Options page.
 *
 * Per-user on purpose: these are drafting habits, not a project fact. EditorPerProjectUserSettings also
 * persists through plain SaveConfig(), where a DefaultConfig class would need
 * TryUpdateDefaultConfigFile() -- which warns and returns false whenever its ini is checked in read-only,
 * so a panel toggle would appear to take and then revert on restart.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "PCGEx | Cluster Sketch Authoring"))
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API UPCGExClusterSketchAuthoringSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UPCGExClusterSketchAuthoringSettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetContainerName() const override
	{
		return "Editor";
	}

	virtual FName GetCategoryName() const override
	{
		return "Plugins";
	}

	virtual FName GetSectionName() const override
	{
		return FName("PCGEx | Cluster Sketch Authoring");
	}

	//~ End UDeveloperSettings

	/** Extruding hands the new vertex the source's data record, so a drafted chain carries its authored
	 *  values instead of resolving every field to the schema default. */
	UPROPERTY(EditAnywhere, Config, Category = "Extrude")
	bool bExtrudeInheritsVertexData = true;

	/** Extruding hands the new edge the record of the source vertex's ONLY edge; a junction or a loose
	 *  end has no unambiguous parent and inherits nothing. */
	UPROPERTY(EditAnywhere, Config, Category = "Extrude")
	bool bExtrudeInheritsEdgeData = false;

	/** Guides capture the placement on their own once the cursor runs near one. Off leaves them
	 *  available to cycle through by hand, but never latches one for you. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement")
	bool bInferPlacementGuides = true;

	/** How near a guide the cursor must run to capture it, in multiples of the on-screen pick radius.
	 *  Raise it for grabbier guides, lower it for a freer hand. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ClampMin = "0.25", ClampMax = "8.0"))
	double GuideCaptureRadius = 2.0;

	/** Preview every guide the gesture could snap onto as a faint rail, brightening as the cursor
	 *  swings toward one. Off draws only the guide actually in force. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement")
	bool bShowGuideCandidates = true;

	/** Guides along the lattice AXES. A 3-axis lattice gives a free drag a horizontal work plane, so
	 *  these are what carry it vertically. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement|Guides")
	bool bGuideLatticeAxes = true;

	/** Guides along the lattice's other declared step directions -- diagonals, hex steps -- excluding
	 *  the axes above, which the walk set also contains. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement|Guides")
	bool bGuideLatticeWalks = true;

	/** Guide along the direction a rank-deficient lattice does NOT span: the deliberate "lift it off
	 *  the lattice plane" move. Withheld whenever the result would be snapped, which discards it. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement|Guides")
	bool bGuideComplementAxis = true;

	/** Guides along the lines of the anchor's own edges, so a vertex can travel straight out of, or
	 *  back along, an edge it already carries. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement|Guides")
	bool bGuideIncidentEdges = true;
};
