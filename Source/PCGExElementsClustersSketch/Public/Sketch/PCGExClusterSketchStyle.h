// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExClusterSketchStyle.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * How one kind of sketch element draws. STRUCTURE is project-wide (mesh, material, size) -- a sketch
 * should not invent its own geometry vocabulary -- while COLOR is the per-sketch expression and can be
 * overridden on the component.
 *
 * With no Mesh set, consumers fall back to immediate-mode drawing (screen-space points / thin lines),
 * so a project that never assigns meshes still sees its sketches.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchElementStyle
{
	GENERATED_BODY()

	FPCGExSketchElementStyle() = default;

	explicit FPCGExSketchElementStyle(const FLinearColor& InColor, const double InSize = 4.0, const float InFallbackThickness = 1.5f)
		: Color(InColor), Size(InSize), FallbackThickness(InFallbackThickness)
	{
	}

	/** None = fall back to immediate-mode drawing for this element kind. */
	UPROPERTY(EditAnywhere, Config, Category = Settings)
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Applied to every instance; None keeps the mesh's own materials. Colors reach it as per-instance
	 *  custom data 0..2 (RGB), so one material serves every sketch. */
	UPROPERTY(EditAnywhere, Config, Category = Settings)
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	FLinearColor Color = FLinearColor::White;

	/** Vertices: world radius of the marker. Edges: world radius of the tube. The mesh is normalized
	 *  by its own bounds to hit this exactly, whatever size it was authored at. */
	UPROPERTY(EditAnywhere, Config, Category = Settings, meta = (ClampMin = "0.01"))
	double Size = 4.0;

	/** EDGES only: which mesh axis runs along the edge. Everything else is derived from bounds, but the
	 *  length axis cannot be inferred -- a 1x1x1 tube has no dominant axis to guess from. */
	UPROPERTY(EditAnywhere, Config, Category = Settings)
	EPCGExAxis LengthAxis = EPCGExAxis::Forward;

	/** Thickness used when Mesh is unset and drawing falls back to immediate mode. */
	UPROPERTY(EditAnywhere, Config, Category = Settings, meta = (ClampMin = "0.0"))
	float FallbackThickness = 1.5f;
};

namespace PCGExSketchStyle
{
	/**
	 * Instance transform for a POINT marker: the mesh's bounds are normalized so its half-extent
	 * becomes exactly InRadius, and its bounds CENTRE lands on InLocation -- so a mesh authored at any
	 * size, and centred anywhere, marks the vertex correctly.
	 *
	 * Half-extent rather than sphere radius: a unit cube and a unit sphere then read as the same
	 * "radius", where the sphere-radius normalizer would shrink the cube by its diagonal.
	 */
	PCGEXELEMENTSCLUSTERSSKETCH_API FTransform MakePointInstanceTransform(const FBoxSphereBounds& InMeshBounds, const FVector& InLocation, double InRadius);

	/**
	 * Instance transform for a SEGMENT: the mesh's length axis is stretched so its full bounds length
	 * spans InStart->InEnd exactly, the two cross axes are normalized to InRadius, and the bounds
	 * centre lands on the segment midpoint -- so a mesh spanning 0..1, -0.5..0.5 or 0..100 all behave
	 * identically.
	 */
	PCGEXELEMENTSCLUSTERSSKETCH_API FTransform MakeSegmentInstanceTransform(const FBoxSphereBounds& InMeshBounds, EPCGExAxis InLengthAxis, const FVector& InStart, const FVector& InEnd, double InRadius);
}

/**
 * Project-wide look of Cluster Sketches, in both the PREVIEW state (a sketch sitting in a level,
 * unedited) and the EDIT state (the standalone Cluster Sketch editor and the in-level editor mode,
 * which style identically BY CONSTRUCTION because they read this same object).
 *
 * Structure lives here on purpose: per-sketch geometry/material choices would make every sketch look
 * like a different tool. Only colors are overridable per component.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "PCGEx | Cluster Sketch"))
class PCGEXELEMENTSCLUSTERSSKETCH_API UPCGExClusterSketchStyleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Assigns the default MESHES and MATERIALS. Soft paths, so nothing loads until something draws --
	 *  and a project missing one (the phantom/outline marker lives in the ControlRig plugin's content)
	 *  simply falls back to immediate-mode drawing for that kind. Everything else defaults inline. */
	UPCGExClusterSketchStyleSettings();

	static const UPCGExClusterSketchStyleSettings* Get();

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnSketchStyleChanged);
	/** Broadcast on any edit here. Live sketches read this object at BUILD time (a mesh swap has to
	 *  reach an existing component's instances), so without this a settings change would only show up
	 *  the next time something else happened to repaint. */
	static FOnSketchStyleChanged& OnChanged();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~ Begin UDeveloperSettings
	virtual FName GetContainerName() const override { return "Editor"; }
	virtual FName GetCategoryName() const override { return "Plugins"; }
	virtual FName GetSectionName() const override { return FName("PCGEx | Cluster Sketch"); }
	//~ End UDeveloperSettings

	// ========== Preview (unedited) ==========

	UPROPERTY(EditAnywhere, Config, Category = "Preview")
	FPCGExSketchElementStyle PreviewVertex = FPCGExSketchElementStyle(FLinearColor::White, 5.0, 4.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Preview")
	FPCGExSketchElementStyle PreviewEdge = FPCGExSketchElementStyle(FLinearColor(0.307292f, 0.307292f, 0.307292f, 0.4f), 1.5, 2.0f);

	// ========== Edit (standalone editor + in-level mode) ==========

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Vertex")
	FPCGExSketchElementStyle EditVertexIdle = FPCGExSketchElementStyle(FLinearColor(0.78f, 0.78f, 0.78f), 8.0, 4.0f);

	/** Lattice-BOUND vertices: same geometry as idle, different read, so snapped and free are
	 *  distinguishable at a glance. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Vertex")
	FLinearColor EditVertexBoundColor = FLinearColor(0.048172f, 0.533276f, 0.246201f);

	/** UNMATERIALIZED vertices -- the ghost markers a crossing offers, not yet real. Clicking one
	 *  materializes it. Edges taking part in a crossing carry this colour too, so an offer reads as
	 *  one gesture. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Vertex")
	FPCGExSketchElementStyle EditVertexPhantom = FPCGExSketchElementStyle(FLinearColor(FColor(0x70, 0x8D, 0xCC)), 8.0, 2.0f);

	/** Tool-INSERTED vertices (edge splits, crossings the tool materialized). Real geometry, but not
	 *  hand-placed: they read as secondary until edited or removed. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Vertex")
	FLinearColor EditVertexSideEffectColor = FLinearColor(0.42f, 0.42f, 0.45f);

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Edge")
	FPCGExSketchElementStyle EditEdge = FPCGExSketchElementStyle(FLinearColor(0.55f, 0.55f, 0.6f), 1.5, 2.0f);

	/** Drawn BEHIND a hovered element at a slight scale-up, inverted-normal style, to fake an outline. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Hover")
	TSoftObjectPtr<UStaticMesh> HoverOutlineMesh;

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Hover")
	TSoftObjectPtr<UMaterialInterface> HoverOutlineMaterial;

	/** Scale applied to the outline relative to the element it wraps. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Hover", meta = (ClampMin = "1.0"))
	double HoverOutlineScale = 1.5;

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Hover")
	FLinearColor HoverColor = FLinearColor(FColor(0x3E, 0xC1, 0x88));

	/** Hover while the delete modifier is held. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Hover")
	FLinearColor DeleteIntentColor = FLinearColor(FColor(0xEB, 0x1D, 0x78));

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Selection")
	FLinearColor SelectedColor = FLinearColor(1.0f, 0.65f, 0.1f);

	/** Added to an element's size while hovered. Hover WINS over selection, matching the colour rule:
	 *  what the cursor is about to act on outranks what is already picked. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Selection", meta = (ClampMin = "0.0"))
	double HoverSizeBonus = 0.2;

	/** Added to a selected element's size, when it is not also hovered. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Selection", meta = (ClampMin = "0.0"))
	double SelectedSizeBonus = 0.15;

	/** Ephemeral affordances (drag ghost, connect preview, merge tether) -- immediate-mode only. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Affordances")
	FLinearColor PreviewAffordanceColor = FLinearColor(0.3f, 1.0f, 0.4f);

	UPROPERTY(EditAnywhere, Config, Category = "Edit|Affordances")
	FLinearColor MergeAffordanceColor = FLinearColor(1.0f, 0.35f, 0.15f);

	/** Lattice axes drawn at the basis origin while editing. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Affordances")
	FLinearColor BasisColor = FLinearColor(0.35f, 0.5f, 0.9f, 0.6f);

	/** Canonical three-quarter view of a sketch: the standalone editor opens on it, and thumbnails are
	 *  projected along it. Deliberately neither axis-aligned nor 45 degrees -- a lattice sketch is mostly
	 *  axis-aligned, and either would collapse whole rows of edges onto a single line. */
	UPROPERTY(EditAnywhere, Config, Category = "Edit|Viewport")
	FRotator DefaultViewRotation = FRotator(-25.0, 145.0, 0.0);
};
