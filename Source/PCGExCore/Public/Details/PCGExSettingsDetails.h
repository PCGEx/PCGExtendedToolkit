// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGExSettingsMacros.h" // Boilerplate dependency
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"

namespace PCGExMT
{
	struct FScope;
}

enum class EPCGExInputValueType : uint8;
struct FPCGExContext;
class UPCGData;

namespace PCGExData
{
	struct FProxyPoint;
	struct FConstPoint;
	class FFacade;
	class FPointIO;

	template <typename T>
	class TBuffer;
}

namespace PCGExDetails
{
#pragma region Settings

	template <typename T>
	class TSettingValue : public TSharedFromThis<TSettingValue<T>>
	{
	public:
		virtual ~TSettingValue() = default;

		/**
		 * Template method: runs the subclass' InitInternal, then registers this value's consumable
		 * attribute (if any) with the facade's context -- one registration site for the whole
		 * hierarchy, subclasses only declare what they consume via GetConsumableName.
		 */
		bool Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped = true, const bool bCaptureMinMax = false);

		FORCEINLINE virtual void SetConstant(T InConstant)
		{
		}

		bool bQuiet = false;

		/**
		 * Whether Init auto-registers the consumed attribute with the context. A veto chain:
		 * shorthand getters seed this from their per-operand bCleanupAttribute toggle, factory-driven
		 * consumers (filters, fill controls, blend ops...) AND-in their factory's own toggle
		 * (bRegisterConsumable &= Factory->bCleanupConsumableAttributes), and Init checks the node's
		 * context toggle last. Factory Register* overrides remain only for operands read through raw
		 * FNames/broadcasters that never flow through a TSettingValue.
		 */
		bool bRegisterConsumable = true;

		FORCEINLINE virtual bool IsConstant()
		{
			return false;
		}

		FORCEINLINE virtual T Read(const int32 Index) = 0;
		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) = 0;

		FORCEINLINE virtual T Min() = 0;
		FORCEINLINE virtual T Max() = 0;
		FORCEINLINE virtual uint32 ReadValueHash(const int32 Index) = 0;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) = 0;

		/** Name of the attribute this value consumes (domain-qualified when selector-driven), or NAME_None. */
		virtual FName GetConsumableName(const UPCGData* InData) const
		{
			return NAME_None;
		}
	};

	template <typename T>
	class TSettingValueBuffer : public TSettingValue<T>
	{
	protected:
		TSharedPtr<PCGExData::TBuffer<T>> Buffer = nullptr;
		FName Name = NAME_None;

	public:
		explicit TSettingValueBuffer(const FName InName)
			: Name(InName)
		{
		}

		virtual T Read(const int32 Index) override;
		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) override;

		virtual T Min() override;
		virtual T Max() override;
		virtual uint32 ReadValueHash(const int32 Index) override;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;

		virtual FName GetConsumableName(const UPCGData* InData) const override
		{
			return Name;
		}
	};

	template <typename T>
	class TSettingValueSelector final : public TSettingValueBuffer<T>
	{
		using TSettingValueBuffer<T>::Buffer;

	protected:
		FPCGAttributePropertyInputSelector Selector;

	public:
		explicit TSettingValueSelector(const FPCGAttributePropertyInputSelector& InSelector)
			: TSettingValueBuffer<T>(InSelector.GetName())
			  , Selector(InSelector)
		{
		}

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
		virtual FName GetConsumableName(const UPCGData* InData) const override;
	};

	template <typename T>
	class TSettingValueConstant : public TSettingValue<T>
	{
	protected:
		T Constant = T{};

	public:
		explicit TSettingValueConstant(const T InConstant)
			: Constant(InConstant)
		{
		}

		FORCEINLINE virtual bool IsConstant() override
		{
			return true;
		}

		FORCEINLINE virtual void SetConstant(T InConstant) override
		{
			Constant = InConstant;
		};

		FORCEINLINE virtual T Read(const int32 Index) override
		{
			return Constant;
		}

		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) override;

		FORCEINLINE virtual T Min() override
		{
			return Constant;
		}

		FORCEINLINE virtual T Max() override
		{
			return Constant;
		}

		virtual uint32 ReadValueHash(const int32 Index) override;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
	};

	template <typename T>
	class TSettingValueSelectorConstant final : public TSettingValueConstant<T>
	{
	protected:
		FPCGAttributePropertyInputSelector Selector;

	public:
		explicit TSettingValueSelectorConstant(const FPCGAttributePropertyInputSelector& InSelector)
			: TSettingValueConstant<T>(T{})
			  , Selector(InSelector)
		{
		}

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
		virtual FName GetConsumableName(const UPCGData* InData) const override;
	};

	template <typename T>
	class TSettingValueBufferConstant final : public TSettingValueConstant<T>
	{
	protected:
		FName Name = NAME_None;

	public:
		explicit TSettingValueBufferConstant(const FName InName)
			: TSettingValueConstant<T>(T{})
			  , Name(InName)
		{
		}

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;

		virtual FName GetConsumableName(const UPCGData* InData) const override
		{
			return Name;
		}
	};

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const EPCGExInputValueType InInput, const FName InName, const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FName InName, const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FName InName, const T InConstant);

	template <typename T>
	TSharedPtr<TSettingValue<T>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const T InConstant);

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValue<_TYPE>; \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValueBuffer<_TYPE>; \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValueSelector<_TYPE>; \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValueConstant<_TYPE>; \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValueSelectorConstant<_TYPE>; \
extern template class PCGEX_TPL_EXPORT(PCGEXCORE_API) TSettingValueBufferConstant<_TYPE>; \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const TSharedPtr<PCGExData::FPointIO> InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

#pragma endregion
}
