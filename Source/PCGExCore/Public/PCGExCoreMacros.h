// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExVersion.h"

#ifndef PCGEX_MACROS
#define PCGEX_MACROS


/// BASIC UTILITIES
/// General-purpose helper macros used throughout the codebase


// No-op macro for disabling functionality via macro arguments
#define PCGEX_MACRO_NONE(...)

// Convenience macro for creating a named TSharedPtr in one line
#define PCGEX_MAKE_SHARED(_NAME, _CLASS, ...) const TSharedPtr<_CLASS> _NAME = MakeShared<_CLASS>(__VA_ARGS__);

// Shorthand for creating FText from string literals
#define FTEXT(_TEXT) FText::FromString(FString(_TEXT))


/// NODE EXECUTION CONTROL
/// Macros for overriding node execution behavior


// Override to control whether a node can only execute on the main thread
#define PCGEX_CAN_ONLY_EXECUTE_ON_MAIN_THREAD(_BOOL) virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return _BOOL; }

// Override to control whether a node supports base point data inputs
#define PCGEX_SUPPORT_BASE_POINT_DATA(_BOOL) virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return _BOOL; }


/// SHARED CONTEXT ACCESS
/// Macros for safely accessing shared PCG contexts with automatic null checks


// Typed shared context access - returns void if context is invalid
#define PCGEX_SHARED_TCONTEXT_VOID(_TYPE, _HANDLE) \
FPCGContext::FSharedContext<FPCGEx##_TYPE##Context> SharedContext(_HANDLE); \
if(!SharedContext.Get()){ return; }

// Typed shared context access - returns false if context is invalid
#define PCGEX_SHARED_TCONTEXT(_TYPE, _HANDLE) \
FPCGContext::FSharedContext<FPCGEx##_TYPE##Context> SharedContext(_HANDLE); \
if(!SharedContext.Get()){ return false; }

// Default (non-typed) shared context access variants
#define PCGEX_SHARED_CONTEXT_VOID(_HANDLE) PCGEX_SHARED_TCONTEXT_VOID(, _HANDLE)
#define PCGEX_SHARED_CONTEXT(_HANDLE) PCGEX_SHARED_TCONTEXT(, _HANDLE)

// Shared context access with custom return value on failure
#define PCGEX_SHARED_CONTEXT_RET(_HANDLE, _RET) \
FPCGContext::FSharedContext<FPCGExContext> SharedContext(_HANDLE); \
if(!SharedContext.Get()){ return _RET; }


/// LOGGING & ERROR HANDLING
/// Macros for consistent error/warning logging with quiet mode support


// --- Basic Input Logging ---

// Log error for missing required inputs (respects quiet mode)
#define PCGEX_LOG_MISSING_INPUT(_CTX, _MESSAGE) if (_CTX && !_CTX->bQuietMissingInputError) { PCGE_LOG_C(Error, GraphAndLog, _CTX, _MESSAGE); }

// Log warning for invalid inputs (respects quiet mode)
#define PCGEX_LOG_INVALID_INPUT(_CTX, _MESSAGE) if (_CTX && !_CTX->bQuietInvalidInputWarning) { PCGE_LOG_C(Warning, GraphAndLog, _CTX, _MESSAGE); }

// --- Attribute/Selector Error Logging ---

// Log error when a selector (attribute or property) doesn't exist
#define PCGEX_LOG_INVALID_SELECTOR_C(_CTX, _NAME, _SELECTOR) if(_CTX && !_CTX->bQuietMissingAttributeError){ PCGE_LOG_C(Error, GraphAndLog, _CTX, FText::Format(FTEXT("Attribute or property \"{0}\" doesn't exist. (See "#_NAME")"), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(_SELECTOR)))); }

// Log error when an attribute doesn't exist
#define PCGEX_LOG_INVALID_ATTR_C(_CTX, _NAME, _ATTR) if(_CTX && !_CTX->bQuietMissingAttributeError){ PCGE_LOG_C(Error, GraphAndLog, _CTX, FText::Format(FTEXT("Attribute \"{0}\" doesn't exist. (See "#_NAME")"), FText::FromName(_ATTR))); }

// --- Quiet Handling for Filters ---

// Return value based on filter's initialization failure policy
#define PCGEX_QUIET_HANDLING_RET return Factory->InitializationFailurePolicy == EPCGExFilterNoDataFallback::Pass;

// Check if quiet handling is enabled for the filter
#define PCGEX_QUIET_HANDLING (Factory->InitializationFailurePolicy != EPCGExFilterNoDataFallback::Error)

// Conditional error logging based on quiet handling state
#define PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(_CTX, _NAME, _SELECTOR) if(!PCGEX_QUIET_HANDLING){ PCGEX_LOG_INVALID_SELECTOR_C(_CTX, _NAME, _SELECTOR) }
#define PCGEX_LOG_INVALID_ATTR_HANDLED_C(_CTX, _NAME, _ATTR) if(!PCGEX_QUIET_HANDLING){ PCGEX_LOG_INVALID_ATTR_C(_CTX, _NAME, _ATTR) }

// --- Attribute/Selector Warning Logging ---

// Log warning (instead of error) when selector/attribute doesn't exist
#define PCGEX_LOG_WARN_SELECTOR_C(_CTX, _NAME, _SELECTOR) if(_CTX && !_CTX->bQuietInvalidInputWarning){ PCGE_LOG_C(Warning, GraphAndLog, _CTX, FText::Format(FTEXT("Attribute or property \"{0}\" doesn't exist. (See "#_NAME")"), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(_SELECTOR)))); }
#define PCGEX_LOG_WARN_ATTR_C(_CTX, _NAME, _ATTR) if(_CTX && !_CTX->bQuietInvalidInputWarning){ PCGE_LOG_C(Warning, GraphAndLog, _CTX, FText::Format(FTEXT("Attribute \"{0}\" doesn't exist. (See "#_NAME")"), FText::FromName(_ATTR))); }

// --- Scope-Based Invalid Input Tracking ---

// Track and log invalid inputs at scope exit
#define PCGEX_ON_INVALILD_INPUTS(_MSG) bool bHasInvalidInputs = false; ON_SCOPE_EXIT{ if (bHasInvalidInputs){ PCGEX_LOG_INVALID_INPUT(Context, _MSG); } };
#define PCGEX_ON_INVALILD_INPUTS_C(_CTX, _MSG) bool bHasInvalidInputs = false; ON_SCOPE_EXIT{ if (bHasInvalidInputs){ PCGEX_LOG_INVALID_INPUT(_CTX, _MSG); } };


/// DEBUG & DEVELOPMENT
/// Macros for marking unimplemented code paths


// Fatal error for methods that should be overridden but weren't
#define PCGEX_NOT_IMPLEMENTED(_NAME){ LowLevelFatalError(TEXT("Method not implemented: (%s)"), TEXT(#_NAME));}
#define PCGEX_NOT_IMPLEMENTED_RET(_NAME, _RETURN){ LowLevelFatalError(TEXT("Method not implemented: (%s)"), TEXT(#_NAME)); return _RETURN;}


/// CONSUMABLE ATTRIBUTE REGISTRATION
/// Macros for registering attributes that can be consumed/cleaned up


// Register a selector as consumable if it resolves to a valid attribute
#define PCGEX_CONSUMABLE_SELECTOR(_SELECTOR, _NAME) if (PCGExMetaHelpers::TryGetAttributeName(_SELECTOR, InData, _NAME)) { InContext->AddConsumableAttributeName(_NAME); }

// Same as above but with explicit context parameter
#define PCGEX_CONSUMABLE_SELECTOR_C(_CONTEXT, _SELECTOR, _NAME) if (PCGExMetaHelpers::TryGetAttributeName(_SELECTOR, InData, _NAME)) { _CONTEXT->AddConsumableAttributeName(_NAME); }

// Conditional consumable registration based on a boolean condition
#define PCGEX_CONSUMABLE_CONDITIONAL(_CONDITION, _SELECTOR, _NAME) if (_CONDITION && PCGExMetaHelpers::TryGetAttributeName(_SELECTOR, InData, _NAME)) { InContext->AddConsumableAttributeName(_NAME); }


/// NODE METADATA & INFO
/// Macros for defining PCG node names, titles, and tooltips

#if WITH_EDITOR
// Standard node info definition with name, title, and tooltip
#define PCGEX_NODE_INFOS(_SHORTNAME, _NAME, _TOOLTIP)\
virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGEx"#_SHORTNAME)); } \
virtual FName AdditionalTaskName() const override{ FString A = TEXT(""); return FName(A + GetDefaultNodeTitle().ToString()); }\
virtual FText GetDefaultNodeTitle() const override { FString A = TEXT(""); A += TEXT("PCGEx | "); A += (bCleanupConsumableAttributes ? TEXT("🗑️ ") : TEXT("")); A += TEXT(_NAME); return FTEXT(A);} \
virtual FText GetNodeTooltipText() const override{ return FTEXT(_TOOLTIP); }

// Extended node info with custom subtitle support
#define PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(_SHORTNAME, _NAME, _TOOLTIP, _TASK_NAME)\
virtual FName GetDefaultNodeName() const override { return FName(TEXT(#_SHORTNAME)); } \
virtual FName AdditionalTaskName() const override{ FString A = TEXT(""); return FName(A + GetDefaultNodeTitle().ToString()); }\
virtual FString GetAdditionalTitleInformation() const override{ FName N = _TASK_NAME; return N.IsNone() ? FString() : N.ToString(); }\
virtual bool HasFlippedTitleLines() const { FName N = _TASK_NAME; return !N.IsNone(); }\
virtual FText GetDefaultNodeTitle() const override { FString A = TEXT(""); A += TEXT("PCGEx | ");  A += (bCleanupConsumableAttributes ? TEXT("🗑️ ") : TEXT("")); A += TEXT(_NAME); return FTEXT(A);} \
virtual FText GetNodeTooltipText() const override{ return FTEXT(_TOOLTIP); }

#else
#define PCGEX_NODE_INFOS(_SHORTNAME, _NAME, _TOOLTIP)
#define PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(_SHORTNAME, _NAME, _TOOLTIP, _TASK_NAME)
#endif


// Define point filter pin configuration for a node
#define PCGEX_NODE_POINT_FILTER(_LABEL, _TOOLTIP, _TYPE, _REQUIRED) \
virtual FName GetPointFilterPin() const override { return _LABEL; } \
virtual FString GetPointFilterTooltip() const override { return TEXT(_TOOLTIP); } \
virtual TSet<PCGExFactories::EType> GetPointFilterTypes() const override { return _TYPE; } \
virtual bool RequiresPointFilters() const override { return _REQUIRED; }


/// CONTEXT & SETTINGS ACCESS
/// Macros for retrieving and validating PCG context and settings objects


// Create the PCG element for a node
#define PCGEX_INITIALIZE_ELEMENT(_NAME)\
FPCGElementPtr UPCGEx##_NAME##Settings::CreateElement() const{	return MakeShared<FPCGEx##_NAME##Element>();}

// Cast and validate context from InContext
#define PCGEX_CONTEXT(_NAME) check(InContext) FPCGEx##_NAME##Context* Context = static_cast<FPCGEx##_NAME##Context*>(InContext); check(Context);

// Retrieve and validate settings from context
#define PCGEX_SETTINGS(_NAME) const UPCGEx##_NAME##Settings* Settings = Context->GetInputSettings<UPCGEx##_NAME##Settings>();	check(Settings);

// Retrieve settings from explicit context parameter
#define PCGEX_SETTINGS_C(_CTX, _NAME) const UPCGEx##_NAME##Settings* Settings = _CTX->GetInputSettings<UPCGEx##_NAME##Settings>();	check(Settings);

// Retrieve settings from 'this' (for use inside element classes)
#define PCGEX_SETTINGS_LOCAL(_NAME) const UPCGEx##_NAME##Settings* Settings = GetInputSettings<UPCGEx##_NAME##Settings>();	check(Settings);

// Combined context and settings retrieval
#define PCGEX_CONTEXT_AND_SETTINGS(_NAME) PCGEX_CONTEXT(_NAME) PCGEX_SETTINGS(_NAME)

// Typed context and settings from ExecutionContext (for async tasks)
#define PCGEX_TYPED_CONTEXT_AND_SETTINGS(_NAME) \
	FPCGEx##_NAME##Context* Context = static_cast<FPCGEx##_NAME##Context*>(ExecutionContext); \
	const UPCGEx##_NAME##Settings* Settings = Context->GetInputSettings<UPCGEx##_NAME##Settings>(); \
	check(Settings);


/// OPERATION MANAGEMENT
/// Macros for validating, registering, and binding PCGEx operations


// Validate that an operation has been selected in settings
#define PCGEX_VALIDATE_INSTANCED_FACTORY(_NAME) if(!Settings->_NAME){PCGE_LOG(Error, GraphAndLog, FTEXT("No operation selected for : "#_NAME)); return false;}

// Register an operation with the context and cast to expected type
#define PCGEX_REGISTER_INSTANCED_FACTORY_C(_CTX, _TYPE, _OP, _OVERRIDES_PIN) Cast<_TYPE>(_CTX->RegisterInstancedFactory(_OP, _OVERRIDES_PIN))

// Complete operation binding: validate, register, and store in context
#define PCGEX_BIND_INSTANCED_FACTORY(_NAME, _TYPE, _OVERRIDES_PIN) PCGEX_VALIDATE_INSTANCED_FACTORY(_NAME) Context->_NAME = PCGEX_REGISTER_INSTANCED_FACTORY_C(Context, _TYPE, Settings->_NAME, _OVERRIDES_PIN); if(!Context->_NAME){return false;}


/// ASYNC & WORK HANDLE UTILITIES
/// Macros for async task management and work handle validation


// Forward a setting value from Settings to Context
#define PCGEX_FWD(_NAME) Context->_NAME = Settings->_NAME;

// Cancel async operations and reset work handle
#define PCGEX_TERMINATE_ASYNC WorkHandle.Reset(); if (TaskManager) { TaskManager->Cancel(); }

// --- Work Handle Validity Checks ---

// Early return if work handle is invalid (void return)
#define PCGEX_CHECK_WORK_HANDLE_VOID if(!WorkHandle.IsValid()){return;}

// Early return if work handle is invalid OR additional condition (void return)
#define PCGEX_CHECK_WORK_HANDLE_OR_VOID(_OR) if(!WorkHandle.IsValid() || _OR){return;}

// Early return if work handle is invalid (with return value)
#define PCGEX_CHECK_WORK_HANDLE(_RET) if(!WorkHandle.IsValid()){return _RET;}

// Early return if work handle is invalid OR additional condition (with return value)
#define PCGEX_CHECK_WORK_HANDLE_OR(_OR, _RET) if(!WorkHandle.IsValid() || _OR){return _RET;}


/// OPTION STATE UTILITIES
/// Macros for handling tri-state options (Default/Enabled/Disabled)


// Resolve an EPCGExOptionState to a boolean, using core settings default
#define PCGEX_GET_OPTION_STATE(_OPTION, _DEFAULT)\
switch (_OPTION){ \
	default: \
	case EPCGExOptionState::Default: return PCGEX_CORE_SETTINGS._DEFAULT; \
	case EPCGExOptionState::Enabled: return true; \
	case EPCGExOptionState::Disabled: return false; \
}


/// PIN DEFINITION MACROS
/// Macros for defining PCG node input/output pins with proper type info


// --- Pin Metadata Helpers ---

// Set pin tooltip (editor-only)
#if WITH_EDITOR
#define PCGEX_PIN_TOOLTIP(_TOOLTIP) Pin.Tooltip = FTEXT(_TOOLTIP);
#else
#define PCGEX_PIN_TOOLTIP(_TOOLTIP) {}
#endif

// Set pin status (Required/Normal/Advanced)
#define PCGEX_PIN_STATUS(_STATUS) Pin.PinStatus = EPCGPinStatus::_STATUS;

// --- Multi-Connection Pins (allow multiple inputs) ---
// Engine version branching: 5.7+ uses new type info system

#if PCGEX_ENGINE_VERSION < 507

// Pre-5.7 pin definitions using EPCGDataType enum
#define PCGEX_PIN_ANY(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Any); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POINTS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Point); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_SPATIALS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Spatial); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POLYLINES(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::PolyLine); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_MESH(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::DynamicMesh); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_PARAMS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Param); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS)  }
#define PCGEX_PIN_FILTERS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Param); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_FACTORIES(_LABEL, _TOOLTIP, _STATUS, _FACTORY_TYPEID) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Param); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_TEXTURES(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::BaseTexture); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }

// --- Single-Connection Pins (only one input allowed) ---
#define PCGEX_PIN_ANY_SINGLE(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Any, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POINT(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Point, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_SPATIAL(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Spatial, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_PARAM(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Param, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_FACTORY(_LABEL, _TOOLTIP, _STATUS, _FACTORY_TYPEID) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::Param, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_TEXTURE(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, EPCGDataType::BaseTexture, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }

#else

// 5.7+ pin definitions using FPCGDataTypeInfo system
#define PCGEX_PIN_ANY(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfo::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POINTS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoPoint::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_SPATIALS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoSpatial::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POLYLINES(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoPolyline::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_MESH(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoDynamicMesh::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_PARAMS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoParam::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS)  }
#define PCGEX_PIN_FILTERS(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGExDataTypeInfoFilter::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_FACTORIES(_LABEL, _TOOLTIP, _STATUS, _FACTORY_TYPEID) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, _FACTORY_TYPEID); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_TEXTURES(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoBaseTexture2D::AsId()); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }

// --- Single-Connection Pins (only one input allowed) ---
#define PCGEX_PIN_ANY_SINGLE(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfo::AsId(), false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_POINT(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoPoint::AsId(), false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_SPATIAL(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoSpatial::AsId(), false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_PARAM(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoParam::AsId(), false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_FACTORY(_LABEL, _TOOLTIP, _STATUS, _FACTORY_TYPEID) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, _FACTORY_TYPEID, false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }
#define PCGEX_PIN_TEXTURE(_LABEL, _TOOLTIP, _STATUS) { FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(_LABEL, FPCGDataTypeInfoBaseTexture2D::AsId(), false, false); PCGEX_PIN_TOOLTIP(_TOOLTIP) PCGEX_PIN_STATUS(_STATUS) }

#endif

// --- Special Purpose Pins ---

// Pin for operation property overrides
#define PCGEX_PIN_OPERATION_OVERRIDES(_LABEL) PCGEX_PIN_PARAMS(_LABEL, "Property overrides to be forwarded & processed by the module. Name must match the property you're targeting 1:1, type mismatch will be broadcasted at your own risk.", Advanced)


/// GEOMETRY UTILITIES
/// Macros for creating bounding boxes with tolerance


// Create a named FBox from two points with tolerance padding
#define PCGEX_BOX_TOLERANCE(_NAME, A, B, Tolerance)\
const FVector _NAME##_A = A;\
const FVector _NAME##_B = B;\
FBox _NAME(_NAME##_A.ComponentMin(_NAME##_B) - Tolerance, _NAME##_A.ComponentMax(_NAME##_B) + Tolerance);

// Inline FBox creation (for use in expressions)
#define PCGEX_BOX_TOLERANCE_INLINE(A, B, Tolerance) FBox(A.ComponentMin(B) - Tolerance, A.ComponentMax(B) + Tolerance)

// Set an existing FBox variable from two points with tolerance
#define PCGEX_SET_BOX_TOLERANCE(_NAME, A, B, Tolerance)\
const FVector _NAME##_A = A;\
const FVector _NAME##_B = B;\
_NAME = FBox(_NAME##_A.ComponentMin(_NAME##_B) - Tolerance, _NAME##_A.ComponentMax(_NAME##_B) + Tolerance);

#endif
