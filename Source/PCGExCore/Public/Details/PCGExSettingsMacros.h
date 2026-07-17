// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

// Boilerplate forward declaration, almost always required when using settings.

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

#ifndef PCGEX_SETTINGS_MACROS
#define PCGEX_SETTINGS_MACROS

#define PCGEX_SETTING_VALUE_DECL(_NAME, _TYPE) TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> GetValueSetting##_NAME(const bool bQuiet = false) const;

#define PCGEX_SETTING_VALUE_IMPL(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> _CLASS::GetValueSetting##_NAME(const bool bQuiet) const{ \
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(_INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
return V; }

// Shorthand variant: seeds the value's consumable auto-registration from the shorthand's own
// per-operand bCleanupAttribute toggle (see FPCGExInputShorthandBase). Call sites may still veto
// further via &= (factory gate); the node toggle is checked at Init time.
#define PCGEX_SETTING_VALUE_IMPL_SHORTHAND(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> _CLASS::GetValueSetting##_NAME(const bool bQuiet) const{ \
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(_INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
V->bRegisterConsumable = bCleanupAttribute;\
return V; }

#define PCGEX_SETTING_VALUE_IMPL_BOOL(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT) PCGEX_SETTING_VALUE_IMPL(_CLASS, _NAME, _TYPE, ((_INPUT) ? EPCGExInputValueType::Attribute : EPCGExInputValueType::Constant), _SOURCE, _CONSTANT);
#define PCGEX_SETTING_VALUE_IMPL_TOGGLE(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT, _DISABLED) PCGEX_SETTING_VALUE_IMPL(_CLASS, _NAME, _TYPE, ((_INPUT != EPCGExInputValueToggle::Disabled) ? static_cast<EPCGExInputValueType>(_INPUT) : EPCGExInputValueType::Constant), _SOURCE, (_INPUT == EPCGExInputValueToggle::Disabled ? _DISABLED : _CONSTANT));

#define PCGEX_SETTING_VALUE_INLINE(_NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> GetValueSetting##_NAME(const bool bQuiet = false) const{ \
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(_INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
return V; }


#define PCGEX_SETTING_DATA_VALUE_DECL(_NAME, _TYPE) TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> GetValueSetting##_NAME(FPCGExContext* InContext, const UPCGData* InData, const bool bQuiet = false) const;

#define PCGEX_SETTING_DATA_VALUE_IMPL(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> _CLASS::GetValueSetting##_NAME(FPCGExContext* InContext, const UPCGData* InData, const bool bQuiet) const{\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(InContext, InData, _INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
return V; }

// Shorthand variant: mirrors PCGEX_SETTING_VALUE_IMPL_SHORTHAND — the shorthand's per-operand
// bCleanupAttribute toggle seeds the value's consumable auto-registration.
#define PCGEX_SETTING_DATA_VALUE_IMPL_SHORTHAND(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> _CLASS::GetValueSetting##_NAME(FPCGExContext* InContext, const UPCGData* InData, const bool bQuiet) const{\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(InContext, InData, _INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
V->bRegisterConsumable = bCleanupAttribute;\
return V; }

#define PCGEX_SETTING_DATA_VALUE_IMPL_BOOL(_CLASS, _NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT) PCGEX_SETTING_DATA_VALUE_IMPL(_CLASS, _NAME, _TYPE, ((_INPUT) ? EPCGExInputValueType::Attribute : EPCGExInputValueType::Constant), _SOURCE, _CONSTANT);

#define PCGEX_SETTING_DATA_VALUE_INLINE(_NAME, _TYPE, _INPUT, _SOURCE, _CONSTANT)\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> GetValueSetting##_NAME(FPCGExContext* InContext, const UPCGData* InDataconst bool bQuiet = false) const{\
TSharedPtr<PCGExDetails::TSettingValue<_TYPE>> V = PCGExDetails::MakeSettingValue<_TYPE>(InContext, InData, _INPUT, _SOURCE, _CONSTANT);\
V->bQuiet = bQuiet;\
return V; }

#endif
