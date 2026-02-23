// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"


class SWidget;
class UPCGExAssetCollection;

class FPCGExAssetEntryCustomization : public IPropertyTypeCustomization
{
public:
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

protected:
	TSet<FName> CustomizedTopLevelProperties;

	virtual void FillCustomizedTopLevelPropertiesNames();

	virtual TSharedRef<SWidget> GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle) =0;
};

class FPCGExEntryHeaderCustomizationBase : public FPCGExAssetEntryCustomization
{
protected:
	virtual FName GetAssetName() { return FName("Asset"); }
	virtual void FillCustomizedTopLevelPropertiesNames() override;
	virtual TSharedRef<SWidget> GetAssetPicker(TSharedRef<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> IsSubCollectionHandle) override;
};

#define PCGEX_FOREACH_ENTRY_TYPE(MACRO)\
MACRO(Mesh, "StaticMesh")\
MACRO(Actor, "Actor")\
MACRO(PCGDataAsset, "DataAsset")\
MACRO(Level, "Level")


#define PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_DECL(_CLASS, _NAME) \
class FPCGEx##_CLASS##EntryCustomization : public FPCGExEntryHeaderCustomizationBase \
{ \
public: \
virtual FName GetAssetName(){ return FName(_NAME); } \
static TSharedRef<IPropertyTypeCustomization> MakeInstance(); \
};

PCGEX_FOREACH_ENTRY_TYPE(PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_DECL)

#undef PCGEX_SUBCOLLECTION_ENTRY_BOILERPLATE_DECL
