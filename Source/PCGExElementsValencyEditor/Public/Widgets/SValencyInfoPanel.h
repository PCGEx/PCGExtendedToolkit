// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Components/VerticalBox.h"
#include "Widgets/SCompoundWidget.h"

class UPCGExValencyCageEditorMode;
class APCGExValencyCageBase;
class APCGExValencyCage;
class APCGExValencyCageNull;
class APCGExValencyCagePattern;
class APCGExValencyAssetPalette;
class APCGExValencyAssetContainerBase;

/**
 * Base class for type-specific cage/palette info panels.
 * Provides shared UI building blocks used by all subclasses.
 */
class SValencyInfoPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencyInfoPanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

protected:
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Probe radius spinbox row (shared by all cage types) */
	void AddProbeRadiusRow(TSharedRef<SVerticalBox> Content, APCGExValencyCageBase* Cage);

	/** Orbital status line: "X/Y orbitals" or "X/Y orbitals · Z assets" */
	void AddOrbitalStatusLine(TSharedRef<SVerticalBox> Content, APCGExValencyCageBase* Cage, bool bShowAssets);

	/** Enabled toggle button */
	void AddEnabledToggle(TSharedRef<SHorizontalBox> Row, APCGExValencyCageBase* Cage);

	/** Module settings section: header + optional module name + W/Min/Max/Dead End + behavior flags */
	void AddModuleSettingsSection(TSharedRef<SVerticalBox> Content, APCGExValencyAssetContainerBase* Container, APCGExValencyCageBase* CageForRebuild);
};

/**
 * Panel for regular cages (APCGExValencyCage).
 * Shows: type header (blue), probe radius, orbital status, enabled/policy/template, module settings.
 */
class SValencyRegularCagePanel : public SValencyInfoPanel
{
public:
	SLATE_BEGIN_ARGS(SValencyRegularCagePanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
		SLATE_ARGUMENT(APCGExValencyCage*, Cage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/**
 * Panel for null/placeholder cages (APCGExValencyCageNull).
 * Shows: type header (red), probe radius, orbital status, enabled toggle, placeholder mode, description.
 */
class SValencyNullCagePanel : public SValencyInfoPanel
{
public:
	SLATE_BEGIN_ARGS(SValencyNullCagePanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
		SLATE_ARGUMENT(APCGExValencyCageNull*, Cage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/**
 * Panel for pattern cages (APCGExValencyCagePattern).
 * Shows: type header (green), probe radius, orbital status, Active/Root toggles,
 * proxied cages list, and pattern settings (when root).
 */
class SValencyPatternCagePanel : public SValencyInfoPanel
{
public:
	SLATE_BEGIN_ARGS(SValencyPatternCagePanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
		SLATE_ARGUMENT(APCGExValencyCagePattern*, Cage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/**
 * Panel for asset palettes (APCGExValencyAssetPalette).
 * Shows: type header (amber), asset count, module settings, mirroring cages list.
 */
class SValencyPalettePanel : public SValencyInfoPanel
{
public:
	SLATE_BEGIN_ARGS(SValencyPalettePanel) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
		SLATE_ARGUMENT(APCGExValencyAssetPalette*, Palette)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
