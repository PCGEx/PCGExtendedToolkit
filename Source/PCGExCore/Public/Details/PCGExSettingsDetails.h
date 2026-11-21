// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
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
	class PCGEXCORE_API TSettingValue : public TSharedFromThis<TSettingValue<T>>
	{
	protected:
		// While bConstant is set, Read() returns Constant without a virtual call. Subclasses own both.
		bool bConstant = false;
		T Constant = T{};

	public:
		virtual ~TSettingValue() = default;

		/**
		 * Template method: runs the subclass' InitInternal, then registers this value's consumable
		 * attribute (if any) with the facade's context -- one registration site for the whole
		 * hierarchy, subclasses only declare what they consume via GetConsumableName.
		 */
		[[nodiscard]] bool Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped = true, const bool bCaptureMinMax = false);

		bool bQuiet = false;

		/**
		 * Whether Init auto-registers the consumed attribute with the context. A veto chain:
		 * shorthand getters seed this from their per-operand bCleanupAttribute toggle, factory-driven
		 * consumers (filters, factory-made operations) AND-in their factory's own toggle through
		 * InitSettingValueGated (see IFilter / FPCGExOperation::InitSettingValue), and Init checks the node's
		 * context toggle last. Factory Register* overrides remain only for operands read through raw
		 * FNames/broadcasters that never flow through a TSettingValue.
		 */
		bool bRegisterConsumable = true;

		FORCEINLINE bool IsConstant() const
		{
			return bConstant;
		}

		FORCEINLINE T Read(const int32 Index)
		{
			return bConstant ? Constant : ReadInternal(Index);
		}

		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) = 0;

		FORCEINLINE virtual T Min() = 0;
		FORCEINLINE virtual T Max() = 0;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) = 0;

		/** Per-index read behind Read(). Not reached while bConstant is set. */
		virtual T ReadInternal(const int32 Index)
		{
			return Constant;
		}

		/** Name of the attribute this value consumes (domain-qualified when selector-driven), or NAME_None. */
		virtual FName GetConsumableName(const UPCGData* InData) const
		{
			return NAME_None;
		}
	};

	template <typename T>
	class PCGEXCORE_API TSettingValueBuffer : public TSettingValue<T>
	{
	protected:
		TSharedPtr<PCGExData::TBuffer<T>> Buffer = nullptr;
		FName Name = NAME_None;

	public:
		explicit TSettingValueBuffer(const FName InName)
			: Name(InName)
		{
		}

		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) override;

		virtual T Min() override;
		virtual T Max() override;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
		virtual T ReadInternal(const int32 Index) override;

		virtual FName GetConsumableName(const UPCGData* InData) const override
		{
			return Name;
		}
	};

	template <typename T>
	class PCGEXCORE_API TSettingValueSelector final : public TSettingValueBuffer<T>
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
	class PCGEXCORE_API TSettingValueConstant : public TSettingValue<T>
	{
	public:
		explicit TSettingValueConstant(const T InConstant)
		{
			this->bConstant = true;
			this->Constant = InConstant;
		}

		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) override;

		FORCEINLINE virtual T Min() override
		{
			return this->Constant;
		}

		FORCEINLINE virtual T Max() override
		{
			return this->Constant;
		}

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
	};

	template <typename T>
	class PCGEXCORE_API TSettingValueSelectorConstant final : public TSettingValueConstant<T>
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
	class PCGEXCORE_API TSettingValueBufferConstant final : public TSettingValueConstant<T>
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

	/**
	 * Negates every value read from a wrapped setting value (direction shorthands' bFlip).
	 * Init hands the caller's bQuiet / bRegisterConsumable to the wrapped value, which owns the consumable registration.
	 * The wrapped value must only be initialised through this wrapper: its constant is mirrored here.
	 * Only instantiated for FVector.
	 */
	template <typename T>
	class PCGEXCORE_API TSettingValueNegated final : public TSettingValue<T>
	{
	protected:
		TSharedPtr<TSettingValue<T>> Inner;

	public:
		explicit TSettingValueNegated(const TSharedPtr<TSettingValue<T>>& InInner);

		virtual void ReadScope(const int32 Start, TArrayView<T> OutResults) override;

		virtual T Min() override;
		virtual T Max() override;

	protected:
		virtual bool InitInternal(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped, const bool bCaptureMinMax) override;
		virtual T ReadInternal(const int32 Index) override;

		// Mirrors the wrapped value's constant state, negated, into this value's own fast path.
		void SyncConstant();
	};

	/**
	 * The only way to construct a TSettingValueNegated. The class is dllexport: instantiating it in any other
	 * TU exports its TSettingValue<T> base there too, which needs Init's definition from the .cpp
	 * (C4661 in non-unity builds).
	 */
	PCGEXCORE_API TSharedPtr<TSettingValue<FVector>> MakeSettingValueNegated(const TSharedPtr<TSettingValue<FVector>>& InInner);

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

	/** Assigns InValue to OutValue, ANDs bAllowConsumable into its bRegisterConsumable (factory stage of the veto chain), then inits it. OutValue is assigned even when Init fails. */
	template <typename T>
	[[nodiscard]] bool InitSettingValueGated(TSharedPtr<TSettingValue<T>>& OutValue, TSharedPtr<TSettingValue<T>> InValue, const bool bAllowConsumable, const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool bSupportScoped = true, const bool bCaptureMinMax = false)
	{
		OutValue = MoveTemp(InValue);
		OutValue->bRegisterConsumable &= bAllowConsumable;
		return OutValue->Init(InDataFacade, bSupportScoped, bCaptureMinMax);
	}

#pragma region externalization

// NOTE: the TSettingValue* classes are dllexport (PCGEXCORE_API on the class definition), so they
// intentionally have NO `extern template class` declarations -- an extern explicit instantiation of a
// dllexport class is C4910 on MSVC. Export comes from the class-def macro + the .cpp instantiation
// (same pattern as TBuffer / TDataValue). The MakeSettingValue *function* instantiations below are
// free functions, not dllexport classes, so they can be externalized normally.
#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FName InName, const _TYPE InConstant); \
extern template TSharedPtr<TSettingValue<_TYPE>> MakeSettingValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType InInput, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE InConstant);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

#pragma endregion
}
