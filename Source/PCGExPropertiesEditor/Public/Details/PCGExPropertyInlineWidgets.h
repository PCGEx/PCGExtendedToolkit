// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

class IPropertyHandle;
class SWidget;

/**
 * Inline widget factories for property types that read editor-only sibling meta fields
 * on the same FPCGExProperty subclass (AllowedClass for soft paths; Range for numeric
 * types). Sibling values are snapshotted at factory time via offset-arithmetic against
 * the Value handle's raw memory -- IPropertyHandle navigation breaks under
 * AddExternalStructureProperty(Scope, "Value"), which is the path used by override-row
 * and read-only-schema customizations. Schema-edit edits to the meta field re-run the
 * factory via the standard PostEditChangeProperty panel refresh.
 */
namespace PCGExPropertyInlineWidgets
{
	/**
	 * Base address of the struct instance that declares ValueProperty, from the address of that field's value.
	 * The one place this arithmetic lives, for the built-in widgets and for foreign property types alike.
	 */
	inline uint8* AccessOwnerRaw(const FProperty* ValueProperty, void* RawValue)
	{
		return static_cast<uint8*>(RawValue) - ValueProperty->GetOffset_ForInternal();
	}

	/** AccessOwnerRaw as T. Null unless ValueProperty is declared by T or by a struct derived from it. */
	template <typename T>
	T* AccessOwner(const FProperty* ValueProperty, void* RawValue)
	{
		const UStruct* OwnerStruct = ValueProperty ? ValueProperty->GetOwnerStruct() : nullptr;
		if (!RawValue || !OwnerStruct || !OwnerStruct->IsChildOf(T::StaticStruct()))
		{
			return nullptr;
		}
		return reinterpret_cast<T*>(AccessOwnerRaw(ValueProperty, RawValue));
	}

	/** Soft object path picker filtered by the sibling AllowedClass field. */
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeSoftObjectPathWidget(const TSharedRef<IPropertyHandle>& ValueHandle);

	/** Soft class path picker constrained to descendants of the sibling AllowedClass field. */
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeSoftClassPathWidget(const TSharedRef<IPropertyHandle>& ValueHandle);

	/** Numeric entry box with ClampMin/ClampMax bound to the sibling Range field. */
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeClampedFloatWidget(const TSharedRef<IPropertyHandle>& ValueHandle);
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeClampedDoubleWidget(const TSharedRef<IPropertyHandle>& ValueHandle);
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeClampedInt32Widget(const TSharedRef<IPropertyHandle>& ValueHandle);
	PCGEXPROPERTIESEDITOR_API TSharedRef<SWidget> MakeClampedInt64Widget(const TSharedRef<IPropertyHandle>& ValueHandle);
}
