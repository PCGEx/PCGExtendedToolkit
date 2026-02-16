// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "EditorMode/PCGExValencyDrawHelper.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "SceneManagement.h"

#include "PCGExValencyEditorSettings.h"
#include "SceneView.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "EditorMode/PCGExConstraintVisualizer.h"
#include "EditorMode/PCGExValencyCageConnectorVisualizer.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyCageOrbital.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Volumes/ValencyContextVolume.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "Engine/Engine.h"
#include "StructUtils/InstancedStruct.h"

const UPCGExValencyEditorSettings* FPCGExValencyDrawHelper::GetSettings()
{
	return UPCGExValencyEditorSettings::Get();
}

void FPCGExValencyDrawHelper::DrawCage(FPrimitiveDrawInterface* PDI, const APCGExValencyCageBase* Cage)
{
	if (!PDI || !Cage)
	{
		return;
	}

	// Pattern cages have their own dedicated drawing
	if (const APCGExValencyCagePattern* PatternCage = Cast<APCGExValencyCagePattern>(Cage))
	{
		DrawPatternCage(PDI, PatternCage);
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	const FVector CageLocation = Cage->GetActorLocation();
	const FTransform CageTransform = Cage->GetActorTransform();

	// Null cages (placeholders) are valid without orbital sets when not participating in patterns
	// Non-participating null cages have their own debug sphere component, no additional drawing needed
	// For participating null cages, we cache the mode color to use for all orbital arrows
	const APCGExValencyCageNull* SelfAsNullCage = nullptr;
	FLinearColor NullCageModeColor = FLinearColor::White;
	
	if (Cage->IsNullCage())
	{
		SelfAsNullCage = Cast<APCGExValencyCageNull>(Cage);
		if (SelfAsNullCage)
		{
			if (!SelfAsNullCage->IsParticipatingInPatterns())
			{
				return; // Passive placeholder - drawn by sphere component only
			}
			// Cache the mode color for orbital drawing
			switch (SelfAsNullCage->GetPlaceholderMode())
			{
			case EPCGExPlaceholderMode::Boundary:
				NullCageModeColor = Settings->BoundaryConnectionColor;
				break;
			case EPCGExPlaceholderMode::Wildcard:
				NullCageModeColor = Settings->WildcardConnectionColor;
				break;
			case EPCGExPlaceholderMode::Any:
				NullCageModeColor = Settings->AnyConnectionColor;
				break;
			}
			// Participating null cages fall through to draw their orbitals
		}
		else
		{
			return; // Legacy fallback
		}
	}

	// Draw mirror connections if this cage mirrors other actors
	if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
	{
		for (const FPCGExMirrorSource& MirrorEntry : RegularCage->MirrorSources)
		{
			if (MirrorEntry.Source)
			{
				DrawMirrorConnection(PDI, RegularCage, MirrorEntry.Source);
			}
		}
	}

	// Get the orbital set for direction info
	const UPCGExValencyOrbitalSet* OrbitalSet = Cage->GetEffectiveOrbitalSet();

	// Non-null cages need an orbital set
	if (!OrbitalSet)
	{
		// Draw warning box if no orbital set
		DrawWireBox(PDI, FBox(CageLocation - FVector(25), CageLocation + FVector(25)), Settings->WarningColor, SDPG_World);
		return;
	}

	// Use probe radius for calculating arrow positions
	const float ProbeRadius = Cage->GetEffectiveProbeRadius();
	const float StartOffset = ProbeRadius * Settings->ArrowStartOffsetPct;

	// Draw orbital arrows
	const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();

	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (Orbital.OrbitalIndex < 0 || Orbital.OrbitalIndex >= OrbitalSet->Num())
		{
			continue;
		}

		const FPCGExValencyOrbitalEntry& Entry = OrbitalSet->Orbitals[Orbital.OrbitalIndex];

		// Get direction from bitmask reference
		FVector Direction;
		int64 Bitmask;
		if (!Entry.GetDirectionAndBitmask(Direction, Bitmask))
		{
			continue;
		}

		// Transform orbital direction to world space
		FVector WorldDir = CageTransform.TransformVectorNoScale(Direction);
		WorldDir.Normalize();

		if (!Orbital.bEnabled)
		{
			// Disabled orbital - skip drawing
			continue;
		}
		else if (const APCGExValencyCageBase* ConnectedCage = Orbital.GetDisplayConnection())
		{
			const FVector ConnectedLocation = ConnectedCage->GetActorLocation();
			const float ConnectionDistance = FVector::Dist(CageLocation, ConnectedLocation);
			const FVector ToConnected = (ConnectedLocation - CageLocation).GetSafeNormal();

			// Thick arrow starts offset from center in direction toward connected cage
			const FVector ArrowStart = CageLocation + ToConnected * StartOffset;

			// Thin line length: shorter when connected to reduce noise
			const float ThinLineLength = ProbeRadius * Settings->ConnectedThinLinePct;

			FLinearColor ArrowColor;
			bool bDashed = false;
			bool bDrawArrowhead = true;
			FVector ArrowEnd;

			// If the source cage is a null cage, use its mode color for all orbitals
			if (SelfAsNullCage)
			{
				ArrowColor = NullCageModeColor;
				bDashed = true;
				bDrawArrowhead = true; // Show direction for null cage orbitals

				// Arrow ends at connected cage's surface or midpoint
				if (ConnectedCage->IsNullCage())
				{
					const float TargetSphereRadius = ConnectedCage->GetEffectiveProbeRadius();
					const float DistanceToSurface = ConnectionDistance - TargetSphereRadius;
					ArrowEnd = CageLocation + ToConnected * FMath::Max(DistanceToSurface, StartOffset + 10.0f);
				}
				else
				{
					ArrowEnd = CageLocation + ToConnected * (ConnectionDistance * 0.5f);
				}
			}
			else if (ConnectedCage->IsNullCage())
			{
				// Connection to null cage (placeholder): color based on target's mode, dashed, no arrowhead
				if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(ConnectedCage))
				{
					switch (NullCage->GetPlaceholderMode())
					{
					case EPCGExPlaceholderMode::Boundary:
						ArrowColor = Settings->BoundaryConnectionColor;
						break;
					case EPCGExPlaceholderMode::Wildcard:
						ArrowColor = Settings->WildcardConnectionColor;
						break;
					case EPCGExPlaceholderMode::Any:
						ArrowColor = Settings->AnyConnectionColor;
						break;
					}
				}
				else
				{
					ArrowColor = Settings->BoundaryConnectionColor; // Legacy fallback
				}
				bDashed = true;
				bDrawArrowhead = false;

				// Thick arrow ends at null cage's sphere surface
				const float NullSphereRadius = ConnectedCage->GetEffectiveProbeRadius();
				const float DistanceToSurface = ConnectionDistance - NullSphereRadius;
				ArrowEnd = CageLocation + ToConnected * FMath::Max(DistanceToSurface, StartOffset + 10.0f);
			}
			else
			{
				// Thick arrow ends at midpoint for regular connections
				ArrowEnd = CageLocation + ToConnected * (ConnectionDistance * 0.5f);

				if (ConnectedCage->HasConnectionTo(Cage))
				{
					// Bidirectional connection: green with arrowhead
					ArrowColor = Settings->BidirectionalColor;
				}
				else
				{
					// Unilateral connection: teal with arrowhead
					ArrowColor = Settings->UnilateralColor;
				}
			}

			// Draw thin line along orbital direction (WorldDir), shorter length when connected
			DrawConnection(PDI, CageLocation, WorldDir, ThinLineLength, ArrowColor, false, false);

			// Draw the thick arrow - points toward connected cage
			DrawOrbitalArrow(PDI, ArrowStart, ArrowEnd, ArrowColor, bDashed, bDrawArrowhead);
		}
		else
		{
			// No connection: draw dashed thin line along orbital direction at full probe radius
			// with arrowhead to show direction
			// For null cages, use the mode color; for regular cages, use NoConnectionColor
			const FLinearColor NoConnColor = SelfAsNullCage ? NullCageModeColor : Settings->NoConnectionColor;
			DrawConnection(PDI, CageLocation, WorldDir, ProbeRadius, NoConnColor, true, true);
		}
	}
}

void FPCGExValencyDrawHelper::DrawCageConnectors(FPrimitiveDrawInterface* PDI, const APCGExValencyCageBase* Cage)
{
	if (!PDI || !Cage)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	if (!Settings || !Settings->bShowConnectorVisualizers)
	{
		return;
	}

	TArray<UPCGExValencyCageConnectorComponent*> ConnectorComponents;
	Cage->GetConnectorComponents(ConnectorComponents);

	if (ConnectorComponents.IsEmpty())
	{
		return;
	}

	const UPCGExValencyConnectorSet* ConnectorSet = Cage->GetEffectiveConnectorSet();
	const float DiamondSize = Settings->ConnectorVisualizerSize;
	const float ArrowLength = Settings->ConnectorArrowLength;
	const UPCGExValencyCageConnectorComponent* SelectedConnector = UPCGExValencyCageEditorMode::GetSelectedConnector();

	for (UPCGExValencyCageConnectorComponent* ConnectorComp : ConnectorComponents)
	{
		if (!ConnectorComp)
		{
			continue;
		}

		// Get connector world transform
		const FTransform ConnectorTransform = ConnectorComp->GetComponentTransform();
		const FVector ConnectorLocation = ConnectorTransform.GetLocation();

		// Enqueue hit proxy for click detection
		PDI->SetHitProxy(new HPCGExConnectorHitProxy(ConnectorComp));

		const FQuat Rotation = ConnectorTransform.GetRotation();
		const float ScaleFactor = ConnectorTransform.GetScale3D().GetAbsMax();
		const float ScaledDiamondSize = DiamondSize * ScaleFactor;
		const float ScaledArrowLength = ArrowLength * ScaleFactor;
		const bool bIsSelected = (ConnectorComp == SelectedConnector);

		if (!ConnectorComp->bEnabled)
		{
			DrawDisabledConnector(PDI, ConnectorLocation, Rotation, ScaledDiamondSize, bIsSelected);
		}
		else
		{
			FLinearColor Color = ConnectorComp->GetEffectiveDebugColor(ConnectorSet);
			DrawConnectorShape(PDI, ConnectorLocation, Rotation.GetForwardVector(), Rotation.GetRightVector(), Rotation.GetUpVector(), ConnectorComp->Polarity, ScaledDiamondSize, ScaledArrowLength, Color, bIsSelected);
		}

		// Clear hit proxy
		PDI->SetHitProxy(nullptr);
	}
}

void FPCGExValencyDrawHelper::DrawDisabledConnector(FPrimitiveDrawInterface* PDI, const FVector& Location, const FQuat& Rotation, float Size, bool bSelected)
{
	const FLinearColor DisabledColor(0.35f, 0.35f, 0.35f, 0.6f);
	const float CrossSize = Size * 0.7f;
	const float CrossThickness = bSelected ? 2.0f : 1.0f;
	const FVector Right = Rotation.GetRightVector();
	const FVector Up = Rotation.GetUpVector();
	PDI->DrawLine(Location - Right * CrossSize - Up * CrossSize, Location + Right * CrossSize + Up * CrossSize, DisabledColor, SDPG_Foreground, CrossThickness);
	PDI->DrawLine(Location - Right * CrossSize + Up * CrossSize, Location + Right * CrossSize - Up * CrossSize, DisabledColor, SDPG_Foreground, CrossThickness);
}

FTransform FPCGExValencyDrawHelper::ComputeMirroredTransform(const FTransform& InTransform, int32 AxisMask, bool bCageRelative)
{
	FTransform T = InTransform;
	FQuat Rot = T.GetRotation();

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!(AxisMask & (1 << Axis))) { continue; }

		if (bCageRelative)
		{
			FVector Forward = Rot.GetForwardVector();
			FVector Up = Rot.GetUpVector();
			Forward[Axis] = -Forward[Axis];
			Up[Axis] = -Up[Axis];
			Rot = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();

			FVector Loc = T.GetTranslation();
			Loc[Axis] = -Loc[Axis];
			T.SetTranslation(Loc);
		}
		else
		{
			static const FVector Axes[] = { FVector::ForwardVector, FVector::RightVector, FVector::UpVector };
			static const int32 RotAxisMap[] = { 2, 2, 0 };
			Rot = Rot * FQuat(Axes[RotAxisMap[Axis]], UE_PI);
		}
	}

	T.SetRotation(Rot);
	return T;
}

void FPCGExValencyDrawHelper::DrawConnectorCircle(FPrimitiveDrawInterface* PDI, const FVector& Center, const FVector& AxisX, const FVector& AxisY, float Radius, const FLinearColor& Color, float Thickness, int32 NumSegments)
{
	const float AngleStep = 2.0f * UE_PI / NumSegments;
	FVector PrevPoint = Center + AxisX * Radius;

	for (int32 i = 1; i <= NumSegments; ++i)
	{
		const float Angle = AngleStep * i;
		const FVector NextPoint = Center + (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle)) * Radius;
		PDI->DrawLine(PrevPoint, NextPoint, Color, SDPG_Foreground, Thickness);
		PrevPoint = NextPoint;
	}
}

void FPCGExValencyDrawHelper::DrawConnectorShape(FPrimitiveDrawInterface* PDI, const FVector& Location, const FVector& Forward, const FVector& Right, const FVector& Up, EPCGExConnectorPolarity Polarity, float Size, float PinLength, const FLinearColor& Color, bool bSelected)
{
	const float ShapeThickness = bSelected ? 2.0f : 1.0f;
	const float LineThickness = bSelected ? 1.5f : 0.75f;
	const float PinSpacing = Size * 0.4f;

	if (Polarity == EPCGExConnectorPolarity::Port)
	{
		// Port (female socket): circle face + 2 horizontal dot holes offset slightly forward
		DrawConnectorCircle(PDI, Location, Right, Up, Size, Color, ShapeThickness);

		const float DotRadius = Size * 0.12f;
		const FVector DotOffset = Forward * Size * 0.15f;
		DrawConnectorCircle(PDI, Location + Right * PinSpacing + DotOffset, Right, Up, DotRadius, Color, LineThickness, 8);
		DrawConnectorCircle(PDI, Location - Right * PinSpacing + DotOffset, Right, Up, DotRadius, Color, LineThickness, 8);
	}
	else if (Polarity == EPCGExConnectorPolarity::Plug)
	{
		// Plug (male): smaller circle + 2 horizontal parallel pin lines (30% length)
		const float PlugRadius = Size * 0.85f;
		DrawConnectorCircle(PDI, Location, Right, Up, PlugRadius, Color, ShapeThickness);

		const float ShortPinLength = PinLength * 0.3f;
		const FVector Pin1Start = Location + Right * PinSpacing;
		const FVector Pin2Start = Location - Right * PinSpacing;
		PDI->DrawLine(Pin1Start, Pin1Start + Forward * ShortPinLength, Color, SDPG_Foreground, LineThickness);
		PDI->DrawLine(Pin2Start, Pin2Start + Forward * ShortPinLength, Color, SDPG_Foreground, LineThickness);
	}
	else
	{
		// Universal: hexagon (same radius as plug) + forward line + center dot
		const float UniversalRadius = Size * 0.85f;
		DrawConnectorCircle(PDI, Location, Right, Up, UniversalRadius, Color, ShapeThickness, 6);

		PDI->DrawLine(Location, Location + Forward * PinLength, Color, SDPG_Foreground, LineThickness);

		const float DotRadius = Size * 0.12f;
		DrawConnectorCircle(PDI, Location, Right, Up, DotRadius, Color, LineThickness, 8);
	}
}

void FPCGExValencyDrawHelper::DrawVolume(FPrimitiveDrawInterface* PDI, const AValencyContextVolume* Volume)
{
	if (!PDI || !Volume)
	{
		return;
	}

	// Volumes handle their own brush rendering, but we can add overlay
	// Get volume bounds and draw with our color
	FVector Origin, BoxExtent;
	Volume->GetActorBounds(false, Origin, BoxExtent);

	const FBox VolumeBox(Origin - BoxExtent, Origin + BoxExtent);

	// Use volume's debug color if available
	const FLinearColor DrawColor = Volume->DebugColor;

	DrawWireBox(PDI, VolumeBox, DrawColor, SDPG_World);
}

void FPCGExValencyDrawHelper::DrawCageLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyCageBase* Cage, bool bIsSelected)
{
	if (!Canvas || !View || !Cage)
	{
		return;
	}

	// Pattern cages have their own dedicated label drawing
	if (const APCGExValencyCagePattern* PatternCage = Cast<APCGExValencyCagePattern>(Cage))
	{
		DrawPatternCageLabels(Canvas, View, PatternCage, bIsSelected);
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();

	// Check if labels should be shown at all
	if (Settings->bOnlyShowSelectedLabels && !bIsSelected)
	{
		return;
	}

	// Check if any labels are enabled
	if (!Settings->bShowCageLabels && !Settings->bShowOrbitalLabels)
	{
		return;
	}

	const FLinearColor LabelColor = bIsSelected ? Settings->SelectedLabelColor : Settings->UnselectedLabelColor;
	const FVector CageLocation = Cage->GetActorLocation();

	// Null cages (placeholder): show mode-based label
	// If participating in patterns, fall through to draw orbitals
	if (Cage->IsNullCage())
	{
		if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(Cage))
		{
			if (Settings->bShowCageLabels)
			{
				FString ModeLabel;
				switch (NullCage->GetPlaceholderMode())
				{
				case EPCGExPlaceholderMode::Boundary:
					ModeLabel = TEXT("Boundary");
					break;
				case EPCGExPlaceholderMode::Wildcard:
					ModeLabel = TEXT("Wildcard");
					break;
				case EPCGExPlaceholderMode::Any:
					ModeLabel = TEXT("Any");
					break;
				}

				// Add participation indicator
				if (NullCage->IsParticipatingInPatterns())
				{
					ModeLabel += TEXT(" [Pattern]");
				}

				DrawLabel(Canvas, View, CageLocation + FVector(0, 0, Settings->CageLabelVerticalOffset), ModeLabel, LabelColor);
			}

			// If not participating in patterns, don't draw orbitals
			if (!NullCage->IsParticipatingInPatterns())
			{
				return;
			}
			// Otherwise fall through to draw orbitals
		}
		else
		{
			// Legacy fallback
			if (Settings->bShowCageLabels)
			{
				DrawLabel(Canvas, View, CageLocation + FVector(0, 0, Settings->CageLabelVerticalOffset), TEXT("Placeholder"), LabelColor);
			}
			return;
		}
	}

	// Draw cage name label
	if (Settings->bShowCageLabels)
	{
		const FString CageName = Cage->GetCageDisplayName();
		if (!CageName.IsEmpty())
		{
			DrawLabel(Canvas, View, CageLocation + FVector(0, 0, Settings->CageLabelVerticalOffset), CageName, LabelColor);
		}
	}

	// Skip orbital labels if disabled
	if (!Settings->bShowOrbitalLabels)
	{
		return;
	}

	// Draw orbital labels if cage has an orbital set
	const UPCGExValencyOrbitalSet* OrbitalSet = Cage->GetEffectiveOrbitalSet();
	if (!OrbitalSet)
	{
		return;
	}

	const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
	const FTransform CageTransform = Cage->GetActorTransform();
	const float ProbeRadius = Cage->GetEffectiveProbeRadius();

	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (Orbital.OrbitalIndex >= 0 && Orbital.OrbitalIndex < OrbitalSet->Num())
		{
			const FPCGExValencyOrbitalEntry& Entry = OrbitalSet->Orbitals[Orbital.OrbitalIndex];

			// Get direction from bitmask reference
			FVector Direction;
			int64 Bitmask;
			if (Entry.GetDirectionAndBitmask(Direction, Bitmask))
			{
				FVector WorldDir = CageTransform.TransformVectorNoScale(Direction);
				WorldDir.Normalize();

				// Position at configured percentage of probe radius
				const FVector LabelPos = CageLocation + WorldDir * (ProbeRadius * Settings->OrbitalLabelRadiusPct);
				DrawLabel(Canvas, View, LabelPos, Entry.GetDisplayName().ToString(), LabelColor);
			}
		}
	}
}

void FPCGExValencyDrawHelper::DrawLineSegment(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness, bool bDashed)
{
	if (!PDI)
	{
		return;
	}

	if (bDashed)
	{
		const UPCGExValencyEditorSettings* Settings = GetSettings();
		const FVector Direction = (End - Start).GetSafeNormal();
		const float TotalLength = FVector::Dist(Start, End);
		const float DashCycle = Settings->DashLength + Settings->DashGap;
		float CurrentPos = 0.0f;

		while (CurrentPos < TotalLength)
		{
			const float DashStart = CurrentPos;
			const float DashEnd = FMath::Min(CurrentPos + Settings->DashLength, TotalLength);

			const FVector SegStart = Start + Direction * DashStart;
			const FVector SegEnd = Start + Direction * DashEnd;

			PDI->DrawLine(SegStart, SegEnd, Color, SDPG_World, Thickness);
			CurrentPos += DashCycle;
		}
	}
	else
	{
		PDI->DrawLine(Start, End, Color, SDPG_World, Thickness);
	}
}

void FPCGExValencyDrawHelper::DrawArrowhead(FPrimitiveDrawInterface* PDI, const FVector& TipLocation, const FVector& Direction, const FLinearColor& Color, float Size, float Thickness)
{
	if (!PDI)
	{
		return;
	}

	const FVector Right = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Right, Direction).GetSafeNormal();

	// Four prongs for 3D arrowhead
	PDI->DrawLine(TipLocation, TipLocation - Direction * Size + Right * Size * 0.5f, Color, SDPG_World, Thickness);
	PDI->DrawLine(TipLocation, TipLocation - Direction * Size - Right * Size * 0.5f, Color, SDPG_World, Thickness);
	PDI->DrawLine(TipLocation, TipLocation - Direction * Size + Up * Size * 0.5f, Color, SDPG_World, Thickness);
	PDI->DrawLine(TipLocation, TipLocation - Direction * Size - Up * Size * 0.5f, Color, SDPG_World, Thickness);
}

void FPCGExValencyDrawHelper::DrawConnection(FPrimitiveDrawInterface* PDI, const FVector& From, const FVector& Along, float Distance, const FLinearColor& Color, bool bDrawArrowhead, bool bDashed)
{
	if (!PDI)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	const FVector To = From + Along * Distance;
	DrawLineSegment(PDI, From, To, Color, Settings->ConnectionLineThickness, bDashed);

	if (bDrawArrowhead)
	{
		DrawArrowhead(PDI, To, Along, Color, Settings->ConnectionArrowheadSize, Settings->ConnectionLineThickness);
	}
}

void FPCGExValencyDrawHelper::DrawOrbitalArrow(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, bool bDashed, bool bDrawArrowhead)
{
	if (!PDI)
	{
		return;
	}

	const FVector Direction = (End - Start).GetSafeNormal();
	const float TotalLength = FVector::Dist(Start, End);

	if (TotalLength < KINDA_SMALL_NUMBER)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();

	// Arrow geometry when arrowhead is drawn:
	// - Main line up to configured percentage of length
	// - Arrowhead prongs at the end
	const float MainLinePct = bDrawArrowhead ? Settings->ArrowMainLinePct : 1.0f;
	const FVector MainLineEnd = Start + Direction * (TotalLength * MainLinePct);

	DrawLineSegment(PDI, Start, MainLineEnd, Color, Settings->OrbitalArrowThickness, bDashed);

	if (bDrawArrowhead)
	{
		DrawArrowhead(PDI, End, Direction, Color, Settings->ArrowheadSize, Settings->ArrowheadThickness);
	}
}

void FPCGExValencyDrawHelper::DrawLabel(FCanvas* Canvas, const FSceneView* View, const FVector& WorldLocation, const FString& Text, const FLinearColor& Color)
{
	if (!Canvas || !View || Text.IsEmpty())
	{
		return;
	}

	// Project world location to screen
	FVector2D ScreenPos;
	if (View->WorldToPixel(WorldLocation, ScreenPos))
	{
		// Check if on screen
		const FIntRect ViewRect = View->UnscaledViewRect;
		if (ScreenPos.X >= ViewRect.Min.X && ScreenPos.X <= ViewRect.Max.X &&
			ScreenPos.Y >= ViewRect.Min.Y && ScreenPos.Y <= ViewRect.Max.Y)
		{
			// Draw text
			FCanvasTextItem TextItem(ScreenPos, FText::FromString(Text), GEngine->GetSmallFont(), Color);
			TextItem.bCentreX = true;
			TextItem.bCentreY = true;
			TextItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(TextItem);
		}
	}
}

void FPCGExValencyDrawHelper::DrawPalette(FPrimitiveDrawInterface* PDI, const APCGExValencyAssetPalette* Palette)
{
	if (!PDI || !Palette)
	{
		return;
	}

	const FTransform PaletteTransform = Palette->GetActorTransform();
	const FLinearColor PaletteColor = Palette->PaletteColor;
	const FVector Extent = Palette->DetectionExtent;

	// Draw wireframe box using palette transform
	const FBox LocalBox(-Extent, Extent);
	DrawWireBox(PDI, PaletteTransform.ToMatrixWithScale(), LocalBox, PaletteColor, SDPG_World);
}

void FPCGExValencyDrawHelper::DrawPaletteLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyAssetPalette* Palette, bool bIsSelected)
{
	if (!Canvas || !View || !Palette)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();

	// Check if labels should be shown at all
	if (!Settings->bShowCageLabels)
	{
		return;
	}

	if (Settings->bOnlyShowSelectedLabels && !bIsSelected)
	{
		return;
	}

	const FLinearColor LabelColor = bIsSelected ? Settings->SelectedLabelColor : Palette->PaletteColor;
	const FVector PaletteLocation = Palette->GetActorLocation();

	// Draw palette display name
	const FString PaletteName = Palette->GetPaletteDisplayName();
	if (!PaletteName.IsEmpty())
	{
		DrawLabel(Canvas, View, PaletteLocation + FVector(0, 0, Settings->CageLabelVerticalOffset), PaletteName, LabelColor);
	}
}

void FPCGExValencyDrawHelper::DrawMirrorConnection(FPrimitiveDrawInterface* PDI, const APCGExValencyCage* MirrorCage, const AActor* SourceActor)
{
	if (!PDI || !MirrorCage || !SourceActor)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	const FVector MirrorLocation = MirrorCage->GetActorLocation();
	const FVector SourceLocation = SourceActor->GetActorLocation();

	// Get color from source actor (cage or palette)
	FLinearColor SourceColor = Settings->MirrorConnectionColor; // Fallback
	if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(SourceActor))
	{
		SourceColor = SourceCage->CageColor;
	}
	else if (const APCGExValencyAssetPalette* SourcePalette = Cast<APCGExValencyAssetPalette>(SourceActor))
	{
		SourceColor = SourcePalette->PaletteColor;
	}

	// Adjust brightness - darken light colors, lighten dark colors
	// Calculate perceived luminance using standard coefficients
	const float Luminance = 0.299f * SourceColor.R + 0.587f * SourceColor.G + 0.114f * SourceColor.B;
	constexpr float BrightnessAdjust = 0.25f;

	FLinearColor ArcColor;
	if (Luminance > 0.5f)
	{
		// Light color - darken it
		ArcColor = SourceColor * (1.0f - BrightnessAdjust);
	}
	else
	{
		// Dark color - lighten it
		ArcColor = SourceColor + FLinearColor(BrightnessAdjust, BrightnessAdjust, BrightnessAdjust, 0.0f);
	}
	ArcColor.A = 1.0f; // Ensure full opacity

	// Draw a soft arc from mirror to source instead of a straight line
	// Arc curves upward to avoid overlapping with other debug elements
	const FVector MidPoint = (MirrorLocation + SourceLocation) * 0.5f;
	const float Distance = FVector::Dist(MirrorLocation, SourceLocation);
	const float ArcHeight = Distance * 0.15f; // Subtle arc height

	// Calculate arc control point (curves upward)
	const FVector ArcControl = MidPoint + FVector::UpVector * ArcHeight;

	// Draw arc as segments using quadratic bezier curve
	constexpr int32 NumSegments = 16;
	constexpr float ThinLineThickness = 1.0f;

	FVector PrevPoint = MirrorLocation;
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(NumSegments);

		// Quadratic bezier: B(t) = (1-t)^2 * P0 + 2(1-t)t * P1 + t^2 * P2
		const float OneMinusT = 1.0f - T;
		const FVector CurrentPoint =
			OneMinusT * OneMinusT * MirrorLocation +
			2.0f * OneMinusT * T * ArcControl +
			T * T * SourceLocation;

		PDI->DrawLine(PrevPoint, CurrentPoint, ArcColor, SDPG_World, ThinLineThickness);
		PrevPoint = CurrentPoint;
	}

	// Draw a small diamond marker at the mirror cage to indicate it's a mirror
	const float MarkerSize = 10.0f;
	const FVector Up = FVector::UpVector * MarkerSize;
	const FVector Right = FVector::RightVector * MarkerSize;

	// Simple 2D diamond shape (less cluttered than 3D version)
	PDI->DrawLine(MirrorLocation + Up, MirrorLocation + Right, ArcColor, SDPG_World, ThinLineThickness);
	PDI->DrawLine(MirrorLocation + Right, MirrorLocation - Up, ArcColor, SDPG_World, ThinLineThickness);
	PDI->DrawLine(MirrorLocation - Up, MirrorLocation - Right, ArcColor, SDPG_World, ThinLineThickness);
	PDI->DrawLine(MirrorLocation - Right, MirrorLocation + Up, ArcColor, SDPG_World, ThinLineThickness);
}

void FPCGExValencyDrawHelper::DrawPatternCage(FPrimitiveDrawInterface* PDI, const APCGExValencyCagePattern* PatternCage)
{
	if (!PDI || !PatternCage)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	const FVector CageLocation = PatternCage->GetActorLocation();

	// Determine the cage's role color (matches sphere component color logic)
	// Pattern cage is visually "wildcard" if ProxiedCages is empty (matches any module)
	const bool bIsVisualWildcard = PatternCage->ProxiedCages.IsEmpty();

	FLinearColor RoleColor;
	if (bIsVisualWildcard)
	{
		RoleColor = Settings->PatternWildcardColor;
	}
	else if (!PatternCage->bIsActiveInPattern)
	{
		RoleColor = Settings->PatternConstraintColor;
	}
	else if (PatternCage->bIsPatternRoot)
	{
		RoleColor = Settings->PatternRootColor;
	}
	else
	{
		RoleColor = Settings->PatternConnectionColor;
	}

	// Draw proxy connections to regular cages (thin dashed lines)
	for (const TObjectPtr<APCGExValencyCage>& ProxiedCage : PatternCage->ProxiedCages)
	{
		if (ProxiedCage)
		{
			const FVector ProxiedLocation = ProxiedCage->GetActorLocation();

			// Draw a soft arc from pattern cage to proxied cage
			const FVector MidPoint = (CageLocation + ProxiedLocation) * 0.5f;
			const float Distance = FVector::Dist(CageLocation, ProxiedLocation);
			const float ArcHeight = Distance * 0.1f; // Subtle arc height

			// Calculate arc control point (curves slightly to the side to avoid overlap)
			const FVector ToProxied = (ProxiedLocation - CageLocation).GetSafeNormal();
			const FVector SideDir = FVector::CrossProduct(ToProxied, FVector::UpVector).GetSafeNormal();
			const FVector ArcControl = MidPoint + SideDir * ArcHeight + FVector::UpVector * ArcHeight * 0.5f;

			// Draw arc as dashed segments using quadratic bezier curve
			constexpr int32 NumSegments = 12;
			constexpr float ThinLineThickness = 1.0f;

			FVector PrevPoint = CageLocation;
			for (int32 i = 1; i <= NumSegments; ++i)
			{
				const float T = static_cast<float>(i) / static_cast<float>(NumSegments);

				// Quadratic bezier: B(t) = (1-t)^2 * P0 + 2(1-t)t * P1 + t^2 * P2
				const float OneMinusT = 1.0f - T;
				const FVector CurrentPoint =
					OneMinusT * OneMinusT * CageLocation +
					2.0f * OneMinusT * T * ArcControl +
					T * T * ProxiedLocation;

				// Alternating segments for dashed effect
				if ((i % 2) == 1)
				{
					PDI->DrawLine(PrevPoint, CurrentPoint, Settings->PatternProxyColor, SDPG_World, ThinLineThickness);
				}
				PrevPoint = CurrentPoint;
			}
		}
	}

	// Draw orbital connections (same system as regular cages)
	const UPCGExValencyOrbitalSet* OrbitalSet = PatternCage->GetEffectiveOrbitalSet();
	const TArray<FPCGExValencyCageOrbital>& Orbitals = PatternCage->GetOrbitals();
	const FTransform CageTransform = PatternCage->GetActorTransform();
	const float ProbeRadius = PatternCage->GetEffectiveProbeRadius();

	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (!Orbital.bEnabled)
		{
			continue;
		}

		// Get orbital direction if we have an orbital set
		FVector WorldDir = FVector::ForwardVector;
		if (OrbitalSet && Orbital.OrbitalIndex >= 0 && Orbital.OrbitalIndex < OrbitalSet->Num())
		{
			const FPCGExValencyOrbitalEntry& Entry = OrbitalSet->Orbitals[Orbital.OrbitalIndex];
			FVector Direction;
			int64 Bitmask;
			if (Entry.GetDirectionAndBitmask(Direction, Bitmask))
			{
				WorldDir = CageTransform.TransformVectorNoScale(Direction);
				WorldDir.Normalize();
			}
		}

		// Get the display connection (auto or manual)
		const APCGExValencyCageBase* ConnectedCage = Orbital.GetDisplayConnection();

		if (ConnectedCage)
		{
			const FVector ConnectedLocation = ConnectedCage->GetActorLocation();
			const FVector ToConnected = (ConnectedLocation - CageLocation).GetSafeNormal();
			const float ConnectionDistance = FVector::Dist(CageLocation, ConnectedLocation);

			// Arrow starts offset from center
			const FVector ArrowStart = CageLocation + ToConnected * (ProbeRadius * Settings->ArrowStartOffsetPct);

			FLinearColor ArrowColor;
			bool bDashed = false;
			bool bDrawArrowhead = true;
			FVector ArrowEnd;

			if (ConnectedCage->IsNullCage())
			{
				// Connection to null cage (placeholder): color based on mode, dashed, no arrowhead
				if (const APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(ConnectedCage))
				{
					switch (NullCage->GetPlaceholderMode())
					{
					case EPCGExPlaceholderMode::Boundary:
						ArrowColor = Settings->BoundaryConnectionColor;
						break;
					case EPCGExPlaceholderMode::Wildcard:
						ArrowColor = Settings->WildcardConnectionColor;
						break;
					case EPCGExPlaceholderMode::Any:
						ArrowColor = Settings->AnyConnectionColor;
						break;
					}
				}
				else
				{
					ArrowColor = Settings->BoundaryConnectionColor; // Legacy fallback
				}
				bDashed = true;
				bDrawArrowhead = false;
				const float NullSphereRadius = ConnectedCage->GetEffectiveProbeRadius();
				const float DistanceToSurface = ConnectionDistance - NullSphereRadius;
				ArrowEnd = CageLocation + ToConnected * FMath::Max(DistanceToSurface, ProbeRadius * Settings->ArrowStartOffsetPct + 10.0f);
			}
			else
			{
				// Arrow ends at midpoint for pattern connections
				ArrowEnd = CageLocation + ToConnected * (ConnectionDistance * 0.5f);

				if (ConnectedCage->HasConnectionTo(PatternCage))
				{
					ArrowColor = Settings->BidirectionalColor;
				}
				else
				{
					ArrowColor = Settings->UnilateralColor;
				}
			}

			// Draw thin line along orbital direction
			const float ThinLineLength = ProbeRadius * Settings->ConnectedThinLinePct;
			DrawConnection(PDI, CageLocation, WorldDir, ThinLineLength, ArrowColor, false, false);

			// Draw the thick arrow toward connected cage
			DrawOrbitalArrow(PDI, ArrowStart, ArrowEnd, ArrowColor, bDashed, bDrawArrowhead);
		}
		else
		{
			// No connection: draw dashed thin line along orbital direction with arrowhead
			DrawConnection(PDI, CageLocation, WorldDir, ProbeRadius, Settings->NoConnectionColor, true, true);
		}
	}

	// Draw pattern root indicator (star shape around the cage)
	if (PatternCage->bIsPatternRoot)
	{
		constexpr float StarRadius = 35.0f;
		constexpr float InnerRadius = 15.0f;
		constexpr int32 NumPoints = 4;
		constexpr float ThinLineThickness = 1.5f;

		for (int32 i = 0; i < NumPoints; ++i)
		{
			const float Angle = (static_cast<float>(i) / static_cast<float>(NumPoints)) * UE_TWO_PI;
			const float AngleOffset = UE_PI / static_cast<float>(NumPoints);

			const FVector OuterPoint = CageLocation + FVector(
				FMath::Cos(Angle) * StarRadius,
				FMath::Sin(Angle) * StarRadius,
				0.0f
			);
			const FVector InnerPoint1 = CageLocation + FVector(
				FMath::Cos(Angle - AngleOffset) * InnerRadius,
				FMath::Sin(Angle - AngleOffset) * InnerRadius,
				0.0f
			);
			const FVector InnerPoint2 = CageLocation + FVector(
				FMath::Cos(Angle + AngleOffset) * InnerRadius,
				FMath::Sin(Angle + AngleOffset) * InnerRadius,
				0.0f
			);

			PDI->DrawLine(InnerPoint1, OuterPoint, Settings->PatternRootColor, SDPG_World, ThinLineThickness);
			PDI->DrawLine(OuterPoint, InnerPoint2, Settings->PatternRootColor, SDPG_World, ThinLineThickness);
		}
	}
}

void FPCGExValencyDrawHelper::DrawPatternCageLabels(FCanvas* Canvas, const FSceneView* View, const APCGExValencyCagePattern* PatternCage, bool bIsSelected)
{
	if (!Canvas || !View || !PatternCage)
	{
		return;
	}

	const UPCGExValencyEditorSettings* Settings = GetSettings();

	// Check if labels should be shown at all
	if (Settings->bOnlyShowSelectedLabels && !bIsSelected)
	{
		return;
	}

	if (!Settings->bShowCageLabels)
	{
		return;
	}

	// Determine label color based on role
	// Pattern cage is visually "wildcard" if ProxiedCages is empty (matches any module)
	const bool bIsVisualWildcard = PatternCage->ProxiedCages.IsEmpty();

	FLinearColor LabelColor;
	if (bIsSelected)
	{
		LabelColor = Settings->SelectedLabelColor;
	}
	else if (PatternCage->bIsPatternRoot)
	{
		LabelColor = Settings->PatternRootColor;
	}
	else if (bIsVisualWildcard)
	{
		LabelColor = Settings->PatternWildcardColor;
	}
	else if (!PatternCage->bIsActiveInPattern)
	{
		LabelColor = Settings->PatternConstraintColor;
	}
	else
	{
		LabelColor = Settings->PatternConnectionColor;
	}

	const FVector CageLocation = PatternCage->GetActorLocation();

	// Draw cage display name (already includes pattern prefix from GetCageDisplayName)
	const FString CageName = PatternCage->GetCageDisplayName();
	if (!CageName.IsEmpty())
	{
		DrawLabel(Canvas, View, CageLocation + FVector(0, 0, Settings->CageLabelVerticalOffset), CageName, LabelColor);
	}

	// Show proxied cage count if proxying regular cages (not a wildcard)
	if (!bIsVisualWildcard)
	{
		const FString ProxyInfo = FString::Printf(TEXT("Proxies: %d"), PatternCage->ProxiedCages.Num());
		DrawLabel(Canvas, View, CageLocation + FVector(0, 0, Settings->CageLabelVerticalOffset - 20.0f), ProxyInfo, LabelColor * 0.7f);
	}
}

// ========== Constraint Visualization ==========

void FPCGExValencyDrawHelper::DrawCageConstraints(
	FPrimitiveDrawInterface* PDI,
	const APCGExValencyCageBase* Cage,
	EPCGExConstraintDetailLevel DetailLevel,
	const UPCGExValencyCageConnectorComponent* SelectedConnector)
{
	if (!PDI || !Cage) { return; }

	TInlineComponentArray<UPCGExValencyCageConnectorComponent*> Connectors;
	Cage->GetComponents(Connectors);

	for (UPCGExValencyCageConnectorComponent* Connector : Connectors)
	{
		if (!Connector || !Connector->bEnabled) { continue; }

		const bool bIsSelected = (Connector == SelectedConnector);
		const EPCGExConstraintDetailLevel ConnectorDetailLevel =
			bIsSelected ? EPCGExConstraintDetailLevel::Detail : DetailLevel;

		DrawConnectorConstraints(PDI, Connector, ConnectorDetailLevel, bIsSelected);
	}
}

void FPCGExValencyDrawHelper::DrawConnectorConstraints(
	FPrimitiveDrawInterface* PDI,
	const UPCGExValencyCageConnectorComponent* Connector,
	EPCGExConstraintDetailLevel DetailLevel,
	bool bIsSelectedConnector)
{
	if (!PDI || !Connector) { return; }

	const UPCGExValencyEditorSettings* Settings = GetSettings();
	if (!Settings) { return; }

	const FLinearColor BaseColor = bIsSelectedConnector
		? Settings->ConstraintActiveColor
		: (DetailLevel == EPCGExConstraintDetailLevel::Indicator
			? Settings->ConstraintIndicatorColor
			: Settings->ConstraintZoneColor);

	DrawConnectorConstraintsAt(PDI, Connector, Connector->GetComponentTransform(), DetailLevel, BaseColor);
}

void FPCGExValencyDrawHelper::DrawConnectorConstraintsAt(
	FPrimitiveDrawInterface* PDI,
	const UPCGExValencyCageConnectorComponent* Connector,
	const FTransform& WorldTransform,
	EPCGExConstraintDetailLevel DetailLevel,
	const FLinearColor& Color)
{
	if (!PDI || !Connector) { return; }

	const APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(Connector->GetOwner());
	if (!Cage) { return; }

	const UPCGExValencyConnectorSet* ConnSet = nullptr;
	for (const TWeakObjectPtr<AValencyContextVolume>& VolRef : Cage->GetContainingVolumes())
	{
		if (AValencyContextVolume* Vol = VolRef.Get())
		{
			ConnSet = Vol->GetEffectiveConnectorSet();
			if (ConnSet) { break; }
		}
	}

	TArray<const FInstancedStruct*> ConstraintPtrs;
	if (ConnSet)
	{
		const int32 TypeIdx = ConnSet->FindConnectorTypeIndex(Connector->ConnectorType);
		if (ConnSet->ConnectorTypes.IsValidIndex(TypeIdx))
		{
			const TArray<FInstancedStruct>& Defaults = ConnSet->ConnectorTypes[TypeIdx].DefaultConstraints;
			for (const FInstancedStruct& Instance : Defaults)
			{
				ConstraintPtrs.Add(&Instance);
			}
		}
	}

	if (ConstraintPtrs.IsEmpty()) { return; }

	const FConstraintVisualizerRegistry& Registry = FConstraintVisualizerRegistry::Get();

	for (const FInstancedStruct* InstancePtr : ConstraintPtrs)
	{
		if (!InstancePtr || !InstancePtr->GetScriptStruct()) { continue; }

		const FPCGExConnectorConstraint* Constraint = InstancePtr->GetPtr<FPCGExConnectorConstraint>();
		if (!Constraint || !Constraint->bEnabled) { continue; }

		IConstraintVisualizer* Visualizer = Registry.Find(InstancePtr->GetScriptStruct());
		if (!Visualizer) { continue; }

		switch (DetailLevel)
		{
		case EPCGExConstraintDetailLevel::Indicator:
			Visualizer->DrawIndicator(PDI, WorldTransform, *Constraint, Color);
			break;
		case EPCGExConstraintDetailLevel::Zone:
			Visualizer->DrawZone(PDI, WorldTransform, *Constraint, Color);
			break;
		case EPCGExConstraintDetailLevel::Detail:
			Visualizer->DrawDetail(PDI, WorldTransform, *Constraint, Color, false);
			break;
		}
	}
}
