// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExGenericAssetPickerCustomization.h"

#include "AssetRegistry/AssetData.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Collections/PCGExGenericCollectionEntry.h"
#include "Core/PCGExAssetCollection.h"
#include "UObject/Package.h"

namespace PCGExGenericAssetPicker
{
	const UPCGExAssetCollection* FindHostCollection(const TSharedRef<IPropertyHandle>& PropertyHandle)
	{
		TArray<UObject*> Outers;
		PropertyHandle->GetOuterObjects(Outers);
		for (UObject* Outer : Outers)
		{
			if (!Outer)
			{
				continue;
			}
			if (const UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Outer))
			{
				return Collection;
			}
			if (const UPCGExAssetCollection* Collection = Outer->GetTypedOuter<UPCGExAssetCollection>())
			{
				return Collection;
			}
		}

		// Struct-on-scope panels have no outer; the grid stamps the host's package on the scope.
		TArray<UPackage*> Packages;
		PropertyHandle->GetOuterPackages(Packages);
		for (const UPackage* Package : Packages)
		{
			if (const UPCGExAssetCollection* Collection = Package ? Cast<UPCGExAssetCollection>(Package->FindAssetInPackage()) : nullptr)
			{
				return Collection;
			}
		}

		return nullptr;
	}

	TSharedRef<SWidget> MakeFilteredAssetPicker(const TSharedRef<IPropertyHandle>& AssetHandle)
	{
		// Snapshot: the Content Browser runs the filter per asset while scrolling.
		const UPCGExAssetCollection* Host = FindHostCollection(AssetHandle);
		const UClass* AllowedClass = (Host && Host->GenericAllowedClass.Get()) ? Host->GenericAllowedClass.Get() : UObject::StaticClass();

		const FOnShouldFilterAsset OnShouldFilter = FOnShouldFilterAsset::CreateLambda([AllowedClass](const FAssetData& AssetData) -> bool
		{
			if (AllowedClass == UObject::StaticClass())
			{
				return false;
			}
			const UClass* AssetClass = AssetData.GetClass();
			return !AssetClass || !AssetClass->IsChildOf(AllowedClass);
		});

		return SNew(SObjectPropertyEntryBox)
			.PropertyHandle(AssetHandle)
			.AllowedClass(AllowedClass)
			.OnShouldFilterAsset(OnShouldFilter)
			.AllowClear(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(false)
			.DisplayUseSelected(true);
	}

	void RegisterOnDetailsView(IDetailsView& DetailsView)
	{
		// A soft-object property is keyed by its property class name (it is not an FObjectProperty, whose
		// key would be the pointee class) -- see FPropertyEditorModule::GetPropertyTypeCustomization.
		DetailsView.RegisterInstancedCustomPropertyTypeLayout(
			FSoftObjectProperty::StaticClass()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExGenericAssetPickerCustomization::MakeInstance),
			MakeShared<FPCGExGenericAssetPickerIdentifier>());
	}
}

#pragma region FPCGExGenericAssetPickerIdentifier

bool FPCGExGenericAssetPickerIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	static const FName AssetName = GET_MEMBER_NAME_CHECKED(FPCGExGenericCollectionEntry, Asset);
	const FProperty* Property = PropertyHandle.GetProperty();
	return Property && Property->GetFName() == AssetName && Property->GetOwnerStruct() == FPCGExGenericCollectionEntry::StaticStruct();
}

#pragma endregion

#pragma region FPCGExGenericAssetPickerCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExGenericAssetPickerCustomization::MakeInstance()
{
	return MakeShared<FPCGExGenericAssetPickerCustomization>();
}

void FPCGExGenericAssetPickerCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			PCGExGenericAssetPicker::MakeFilteredAssetPicker(PropertyHandle)
		];
}

#pragma endregion
