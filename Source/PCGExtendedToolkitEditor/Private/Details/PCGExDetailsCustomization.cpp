// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExDetailsCustomization.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "PCGExCoreEditor/Public/PCGExAssetTypesMacros.h"
#include "PCGExCoreEditor/Public/Details/Enums/PCGExGridEnumCustomization.h"
#include "PCGExCoreEditor/Public/Details/Enums/PCGExInlineEnumCustomization.h"
#include "Styling/SlateStyle.h"

namespace PCGExDetailsCustomization
{
	void RegisterDetailsCustomization(const TSharedPtr<FSlateStyleSet>& Style)
	{
#pragma region Editor UI Icon

		// Currently registers blindly all the icons available, whether used or not.
		// Ultimately should be moved per-module but it's not that big of a deal to store them here anyway; they're just IDs.

		FSlateStyleSet& AppStyle = const_cast<FSlateStyleSet&>(static_cast<const FSlateStyleSet&>(FAppStyle::Get()));

#define PCGEX_ADD_ACTION_ICON(_NAME, _SIZE) AppStyle.Set("PCGEx.ActionIcon." # _NAME, new FSlateVectorImageBrush(Style->RootToContentDir(TEXT( "PCGEx_Editor_" #_NAME), TEXT(".svg")), _SIZE));

		const FVector2D AIS_VerySmall = FVector2D(16.0f, 16.0f);
		const FVector2D AIS_Small = FVector2D(18.0f, 18.0f);
		const FVector2D AIS_Med = FVector2D(22.0f, 22.0f);
		const FVector2D AIS_Wide = FVector2D(44.0f, 22.0f);
		const FVector2D AIS_Big = FVector2D(44.0f, 44.0f);

		PCGEX_ADD_ACTION_ICON(Constant, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(Attribute, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(DataAttribute, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(Default, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(Enabled, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(Disabled, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(ScaledBounds, AIS_Med)
		PCGEX_ADD_ACTION_ICON(DensityBounds, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Bounds, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Center, AIS_Med)
		PCGEX_ADD_ACTION_ICON(X, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Y, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Z, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Dist_Center, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Dist_SphereBounds, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Dist_BoxBounds, AIS_Small)
		PCGEX_ADD_ACTION_ICON(All, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Include, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Exclude, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Vtx, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Edges, AIS_Med)

		PCGEX_ADD_ACTION_ICON(STF_None, AIS_Med)
		PCGEX_ADD_ACTION_ICON(STF_Uniform, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(STF_Individual, AIS_Wide)

		PCGEX_ADD_ACTION_ICON(MissingData_Error, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(MissingData_Pass, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(MissingData_Fail, AIS_VerySmall)

		PCGEX_ADD_ACTION_ICON(Fit_None, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Fit_Fill, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Fit_Min, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Fit_Max, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Fit_Average, AIS_Med)

		PCGEX_ADD_ACTION_ICON(From_Min, AIS_Med)
		PCGEX_ADD_ACTION_ICON(From_Center, AIS_Med)
		PCGEX_ADD_ACTION_ICON(From_Max, AIS_Med)
		PCGEX_ADD_ACTION_ICON(From_Pivot, AIS_Med)
		PCGEX_ADD_ACTION_ICON(From_Custom, AIS_Med)

		PCGEX_ADD_ACTION_ICON(To_Same, AIS_Med)
		PCGEX_ADD_ACTION_ICON(To_Min, AIS_Med)
		PCGEX_ADD_ACTION_ICON(To_Center, AIS_Med)
		PCGEX_ADD_ACTION_ICON(To_Max, AIS_Med)
		PCGEX_ADD_ACTION_ICON(To_Pivot, AIS_Med)
		PCGEX_ADD_ACTION_ICON(To_Custom, AIS_Med)

		PCGEX_ADD_ACTION_ICON(Numeric, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(Text, AIS_Wide)


		PCGEX_ADD_ACTION_ICON(RebuildStaging, AIS_Big)
		PCGEX_ADD_ACTION_ICON(RebuildStagingRecursive, AIS_Big)
		PCGEX_ADD_ACTION_ICON(RebuildStagingProject, AIS_Big)

		PCGEX_ADD_ACTION_ICON(AddContentBrowserSelection, AIS_Med)
		PCGEX_ADD_ACTION_ICON(NormalizeWeight, AIS_Med)

		PCGEX_ADD_ACTION_ICON(Entries, AIS_Med)
		PCGEX_ADD_ACTION_ICON(Settings, AIS_Med)

		PCGEX_ADD_ACTION_ICON(AxisOrder_XYZ, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AxisOrder_YZX, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AxisOrder_ZXY, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AxisOrder_YXZ, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AxisOrder_ZYX, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AxisOrder_XZY, AIS_Wide)

		PCGEX_ADD_ACTION_ICON(RotOrder_X, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_XY, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_XZ, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_Y, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_YX, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_YZ, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_Z, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_ZX, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_ZY, AIS_Med)
		PCGEX_ADD_ACTION_ICON(RotOrder_XYZ, AIS_Med)

		PCGEX_ADD_ACTION_ICON(EntryRule, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(CollectionRule, AIS_VerySmall)

		PCGEX_ADD_ACTION_ICON(SingleMat, AIS_Med)
		PCGEX_ADD_ACTION_ICON(MultiMat, AIS_Wide)

		PCGEX_ADD_ACTION_ICON(Unchanged, AIS_Small)
		PCGEX_ADD_ACTION_ICON(CW, AIS_Small)
		PCGEX_ADD_ACTION_ICON(CCW, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Ascending, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Descending, AIS_Small)

		PCGEX_ADD_ACTION_ICON(BeforeStaging, AIS_Wide)
		PCGEX_ADD_ACTION_ICON(AfterStaging, AIS_Wide)

		PCGEX_ADD_ACTION_ICON(NoSnapping, AIS_Small)
		PCGEX_ADD_ACTION_ICON(SnapOffset, AIS_Small)
		PCGEX_ADD_ACTION_ICON(SnapResult, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Snap, AIS_Small)

		PCGEX_ADD_ACTION_ICON(Round, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Floor, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Ceil, AIS_Small)

		PCGEX_ADD_ACTION_ICON(Fixed, AIS_Small)
		PCGEX_ADD_ACTION_ICON(Flexible, AIS_Small)

		PCGEX_ADD_ACTION_ICON(Bit_Direct, AIS_VerySmall)
		PCGEX_ADD_ACTION_ICON(Bit_Mutations, AIS_VerySmall)

		FButtonStyle ActionIconButton = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton");

		FSlateBrush Brush = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton").Pressed;
		Brush.Margin = FMargin(2, 2);

		Brush.TintColor = FLinearColor(0.1, 0.1, 0.1, 0.5f);
		ActionIconButton.SetNormal(Brush);

		Brush.TintColor = FLinearColor(0.1, 0.1, 0.1, 0.5f);
		ActionIconButton.SetHovered(Brush);

		Brush.TintColor = FLinearColor(0.1, 0.1, 0.1, 0.8f);
		ActionIconButton.SetPressed(Brush);

		AppStyle.Set("PCGEx.ActionIcon", ActionIconButton);

		FCheckBoxStyle SmallCheckStyle = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");

		FSlateBrush CheckBrush = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton").Pressed;
		CheckBrush.Margin = FMargin(2, 2);
		CheckBrush.TintColor = FLinearColor(0, 0.5, 1, 1);
		SmallCheckStyle.SetCheckedImage(CheckBrush);
		CheckBrush.TintColor = FLinearColor(1, 1, 1, 1);
		SmallCheckStyle.SetCheckedHoveredImage(CheckBrush);

		AppStyle.Set("PCGEx.Checkbox", SmallCheckStyle);

#undef PCGEX_ADD_ACTION_ICON
#undef PCGEX_ADD_ACTION_ICON_WIDE

#pragma endregion

#pragma region Inlined Enums registration

		// Same as icons, this should be moved to their owning modules
		// But no harm done if the the enum doesn't exists.

		PCGEX_REGISTER_CUSTO_START

#define PCGEX_FOREACH_INLINE_ENUM(MACRO)\
MACRO(EPCGExInputValueType)\
MACRO(EPCGExInputValueToggle)\
MACRO(EPCGExApplySampledComponentFlags)\
MACRO(EPCGExOptionState)\
MACRO(EPCGExFilterFallback)\
MACRO(EPCGExFilterNoDataFallback)\
MACRO(EPCGExPointBoundsSource)\
MACRO(EPCGExDistance)\
MACRO(EPCGExClusterElement)\
MACRO(EPCGExAttributeFilter)\
MACRO(EPCGExComparisonDataType)\
MACRO(EPCGExScaleToFit)\
MACRO(EPCGExJustifyFrom)\
MACRO(EPCGExJustifyTo)\
MACRO(EPCGExFitMode)\
MACRO(EPCGExMinimalAxis)\
MACRO(EPCGExMaterialVariantsMode)\
MACRO(EPCGExEntryVariationMode)\
MACRO(EPCGExGlobalVariationRule)\
MACRO(EPCGExWinding)\
MACRO(EPCGExWindingMutation)\
MACRO(EPCGExSortDirection)\
MACRO(EPCGExTruncateMode)\
MACRO(EPCGExVariationMode)\
MACRO(EPCGExVariationSnapping)\
MACRO(EPCGExGrammarScaleMode)\
MACRO(EPCGExSampleSource)\
MACRO(EPCGExBitmaskMode)\
MACRO(EPCGExCellTriageFlags)

#define PCGEX_FOREACH_GRID_ENUM(MACRO)\
MACRO(EPCGExAxisOrder, 3)\
MACRO(EPCGExMakeRotAxis, 3)

		// This is grotesque but it works （。＾▽＾）

#define PCGEX_DECL_INLINE_ENUM(_ENUM)\
class FPCGExInline##_ENUM final : public FPCGExInlineEnumCustomization{\
public:	explicit FPCGExInline##_ENUM(const FString& InEnumName) : FPCGExInlineEnumCustomization(InEnumName){}\
static TSharedRef<IPropertyTypeCustomization> MakeInstance(){ return MakeShareable(new FPCGExInline##_ENUM(#_ENUM));}};\
PropertyModule.RegisterCustomPropertyTypeLayout(#_ENUM,FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExInline##_ENUM::MakeInstance));

		PCGEX_FOREACH_INLINE_ENUM(PCGEX_DECL_INLINE_ENUM)

#undef PCGEX_DECL_INLINE_ENUM
#undef PCGEX_FOREACH_INLINE_ENUM

#define PCGEX_DECL_GRID_ENUM(_ENUM, _COL)\
class FPCGExGrid##_ENUM final : public FPCGExGridEnumCustomization{\
public:	explicit FPCGExGrid##_ENUM(const FString& InEnumName, const int32 InColumns = 3) : FPCGExGridEnumCustomization(InEnumName){}\
static TSharedRef<IPropertyTypeCustomization> MakeInstance(){ return MakeShareable(new FPCGExGrid##_ENUM(#_ENUM, _COL));}};\
PropertyModule.RegisterCustomPropertyTypeLayout(#_ENUM,FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExGrid##_ENUM::MakeInstance));

		PCGEX_FOREACH_GRID_ENUM(PCGEX_DECL_GRID_ENUM)

#undef PCGEX_DECL_GRID_ENUM
#undef PCGEX_FOREACH_GRID_ENUM

#pragma endregion
	}
}
