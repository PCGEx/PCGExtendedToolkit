// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"

class IDetailsView;
class IPropertyHandle;
class SWidget;
class UPCGExAssetCollection;

/**
 * The Generic entry's Asset picker, narrowed by the host collection's editor-only GenericAllowedClass.
 *
 * Two hosting contexts need it: asset-hosted details views (Entries tab), where the entry's own
 * customization builds the header picker, and struct-on-scope panels (grid detail panes), where the
 * entry's customization never runs -- a structure view expands the root struct's members directly --
 * so the Asset property itself is customized, registered per panel via RegisterOnDetailsView.
 */
namespace PCGExGenericAssetPicker
{
	/** Host of the entry a handle belongs to: an outer object, else the package a struct-on-scope panel stamps. */
	PCGEXCOLLECTIONSEDITOR_API const UPCGExAssetCollection* FindHostCollection(const TSharedRef<IPropertyHandle>& PropertyHandle);

	/** Picker for the Asset handle. Class snapshotted at build (UObject = everything); the editor rebuilds on change. */
	PCGEXCOLLECTIONSEDITOR_API TSharedRef<SWidget> MakeFilteredAssetPicker(const TSharedRef<IPropertyHandle>& AssetHandle);

	/** Register the Asset-property customization on a struct-on-scope panel's inner details view. */
	PCGEXCOLLECTIONSEDITOR_API void RegisterOnDetailsView(IDetailsView& DetailsView);
}

/** Matches FPCGExGenericCollectionEntry::Asset and nothing else. */
class PCGEXCOLLECTIONSEDITOR_API FPCGExGenericAssetPickerIdentifier : public IPropertyTypeIdentifier
{
public:
	virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override;
};

class PCGEXCOLLECTIONSEDITOR_API FPCGExGenericAssetPickerCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
	}
};
