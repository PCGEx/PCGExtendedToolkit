// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class AActor;
class FPrimitiveDrawInterface;
class FCanvas;
class FSceneView;
class APCGExValencyCageBase;
class APCGExValencyCage;
class APCGExValencyCagePattern;
class APCGExValencyAssetPalette;
class AValencyContextVolume;
class UPCGExValencyEditorSettings;
class UPCGExValencyCageConnectorComponent;
enum class EPCGExConnectorPolarity : uint8;
enum class EPCGExConstraintDetailLevel : uint8;

/**
 * Stateless helper class for Valency editor mode drawing operations.
 * All methods are static and use settings from UPCGExValencyEditorSettings.
 */
class PCGEXELEMENTSVALENCYEDITOR_API FPCGExValencyDrawHelper
{
public:
	// ========== High-Level Drawing ==========

	/**
	 * Draw a cage's orbital connections and visual indicators.
	 * @param PDI Primitive draw interface
	 * @param Cage The cage to draw
	 */
	static void DrawCage(FPrimitiveDrawInterface* PDI, const APCGExValencyCageBase* Cage);

	/**
	 * Draw connector component polarity shapes for a cage.
	 * Bypasses the component visualizer to work for ALL cages (selected and unselected).
	 * @param PDI Primitive draw interface
	 * @param Cage The cage whose connectors to draw
	 */
	static void DrawCageConnectors(FPrimitiveDrawInterface* PDI, const APCGExValencyCageBase* Cage);

	/**
	 * Draw a volume's wireframe bounds.
	 * @param PDI Primitive draw interface
	 * @param Volume The volume to draw
	 */
	static void DrawVolume(FPrimitiveDrawInterface* PDI, const AValencyContextVolume* Volume);

	/**
	 * Draw an asset palette's detection bounds.
	 * @param PDI Primitive draw interface
	 * @param Palette The palette to draw
	 */
	static void DrawPalette(FPrimitiveDrawInterface* PDI, const APCGExValencyAssetPalette* Palette);

	/**
	 * Draw labels for an asset palette.
	 * @param Canvas Canvas for text rendering
	 * @param View Current scene view
	 * @param Palette The palette to label
	 * @param bIsSelected Whether the palette is selected (affects label color)
	 */
	static void DrawPaletteLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyAssetPalette* Palette, bool bIsSelected);

	/**
	 * Draw labels for a cage (name and orbital labels).
	 * @param Canvas Canvas for text rendering
	 * @param View Current scene view
	 * @param Cage The cage to label
	 * @param bIsSelected Whether the cage is selected (affects label color)
	 */
	static void DrawCageLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyCageBase* Cage, bool bIsSelected);

	/**
	 * Draw mirror connection indicator (dashed line from mirror cage to source).
	 * @param PDI Primitive draw interface
	 * @param MirrorCage The cage that mirrors another
	 * @param SourceActor The source actor being mirrored (cage or palette)
	 */
	static void DrawMirrorConnection(FPrimitiveDrawInterface* PDI, const APCGExValencyCage* MirrorCage, const AActor* SourceActor);

	/**
	 * Draw a pattern cage's proxy connections and pattern network.
	 * @param PDI Primitive draw interface
	 * @param PatternCage The pattern cage to draw
	 */
	static void DrawPatternCage(FPrimitiveDrawInterface* PDI, const APCGExValencyCagePattern* PatternCage);

	/**
	 * Draw labels for a pattern cage.
	 * @param Canvas Canvas for text rendering
	 * @param View Current scene view
	 * @param PatternCage The pattern cage to label
	 * @param bIsSelected Whether the cage is selected (affects label color)
	 */
	static void DrawPatternCageLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyCagePattern* PatternCage, bool bIsSelected);

	/**
	 * Draw a single connector's polarity shape (Port/Plug/Universal).
	 * Shared by both the component visualizer and the editor mode render callback.
	 * @param PDI Primitive draw interface
	 * @param Location Connector world position
	 * @param Forward Connector's forward axis (face normal)
	 * @param Right Connector's right axis
	 * @param Up Connector's up axis
	 * @param Polarity The connector polarity
	 * @param Size Base shape radius
	 * @param PinLength Length of pin/line extensions along forward
	 * @param Color Draw color
	 */
	static void DrawConnectorShape(FPrimitiveDrawInterface* PDI, const FVector& Location, const FVector& Forward, const FVector& Right, const FVector& Up, EPCGExConnectorPolarity Polarity, float Size, float PinLength, const FLinearColor& Color, bool bSelected = false);

	/**
	 * Draw a disabled connector indicator (gray cross on the connector plane).
	 */
	static void DrawDisabledConnector(FPrimitiveDrawInterface* PDI, const FVector& Location, const FQuat& Rotation, float Size, bool bSelected);

	/**
	 * Compute a mirrored relative transform by applying mirror operations per axis in the mask.
	 * @param InTransform Source relative transform
	 * @param AxisMask Bitmask of axes to mirror (bit 0=X, 1=Y, 2=Z)
	 * @param bCageRelative If true, mirror position + orientation relative to cage. If false, flip-in-place (180° rotation).
	 * @return The mirrored transform
	 */
	static FTransform ComputeMirroredTransform(const FTransform& InTransform, int32 AxisMask, bool bCageRelative);

	// ========== Constraint Visualization ==========

	/**
	 * Draw constraint indicators/zones for all connectors on a cage.
	 * @param PDI Primitive draw interface
	 * @param Cage The cage whose connector constraints to draw
	 * @param DetailLevel Controls how much detail to render
	 * @param SelectedConnector The currently selected connector (nullptr if none)
	 */
	static void DrawCageConstraints(
		FPrimitiveDrawInterface* PDI,
		const APCGExValencyCageBase* Cage,
		EPCGExConstraintDetailLevel DetailLevel,
		const UPCGExValencyCageConnectorComponent* SelectedConnector = nullptr);

	/**
	 * Draw constraints for a single connector at the given detail level.
	 * @param PDI Primitive draw interface
	 * @param Connector The connector component
	 * @param DetailLevel Controls rendering detail
	 * @param bIsSelectedConnector Whether this connector is currently selected
	 */
	static void DrawConnectorConstraints(
		FPrimitiveDrawInterface* PDI,
		const UPCGExValencyCageConnectorComponent* Connector,
		EPCGExConstraintDetailLevel DetailLevel,
		bool bIsSelectedConnector);

	/**
	 * Draw constraints for a connector at an overridden world transform (used for ghost previews).
	 * Uses the connector's type/constraints but renders at the specified transform and color.
	 */
	static void DrawConnectorConstraintsAt(
		FPrimitiveDrawInterface* PDI,
		const UPCGExValencyCageConnectorComponent* Connector,
		const FTransform& WorldTransform,
		EPCGExConstraintDetailLevel DetailLevel,
		const FLinearColor& Color);

	// ========== Low-Level Drawing Primitives ==========

	/**
	 * Draw a line segment (solid or dashed).
	 * @param PDI Primitive draw interface
	 * @param Start Start position
	 * @param End End position
	 * @param Color Line color
	 * @param Thickness Line thickness
	 * @param bDashed Whether to draw as dashed line
	 */
	static void DrawLineSegment(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness, bool bDashed);

	/**
	 * Draw a 3D arrowhead at a location pointing in a direction.
	 * @param PDI Primitive draw interface
	 * @param TipLocation Position of the arrowhead tip
	 * @param Direction Direction the arrow points (normalized)
	 * @param Color Arrowhead color
	 * @param Size Size of the arrowhead
	 * @param Thickness Line thickness
	 */
	static void DrawArrowhead(FPrimitiveDrawInterface* PDI, const FVector& TipLocation, const FVector& Direction, const FLinearColor& Color, float Size, float Thickness);

	/**
	 * Draw a connection line along a direction with optional arrowhead.
	 * @param PDI Primitive draw interface
	 * @param From Start position
	 * @param Along Direction to draw along (normalized)
	 * @param Distance Length of the line
	 * @param Color Line color
	 * @param bDrawArrowhead Whether to draw arrowhead at end
	 * @param bDashed Whether to draw as dashed line
	 */
	static void DrawConnection(FPrimitiveDrawInterface* PDI, const FVector& From, const FVector& Along, float Distance, const FLinearColor& Color, bool bDrawArrowhead, bool bDashed);

	/**
	 * Draw an orbital arrow from start to end with optional arrowhead.
	 * @param PDI Primitive draw interface
	 * @param Start Start position
	 * @param End End position
	 * @param Color Arrow color
	 * @param bDashed Whether to draw as dashed
	 * @param bDrawArrowhead Whether to draw arrowhead
	 */
	static void DrawOrbitalArrow(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, bool bDashed, bool bDrawArrowhead);

	/**
	 * Draw a text label in the viewport.
	 * @param Canvas Canvas for text rendering
	 * @param View Current scene view
	 * @param WorldLocation World position for the label
	 * @param Text Text to display
	 * @param Color Text color
	 */
	static void DrawLabel(FCanvas* Canvas, const FSceneView* View, const FVector& WorldLocation, const FString& Text, const FLinearColor& Color);

private:
	/** Draw a circle (or dot) in the plane defined by two axes. */
	static void DrawConnectorCircle(FPrimitiveDrawInterface* PDI, const FVector& Center, const FVector& AxisX, const FVector& AxisY, float Radius, const FLinearColor& Color, float Thickness, int32 NumSegments = 24);

	/** Get cached settings (avoids repeated GetDefault calls) */
	static const UPCGExValencyEditorSettings* GetSettings();
};
