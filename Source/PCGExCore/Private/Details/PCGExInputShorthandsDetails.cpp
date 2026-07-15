// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExInputShorthandsDetails.h"

#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
#include "PCGExLog.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#endif

#define PCGEX_FOREACH_INPUT_SHORTHAND(MACRO, ...) \
MACRO(bool, Boolean, __VA_ARGS__)       \
MACRO(int32, Integer32, __VA_ARGS__)      \
MACRO(int32, Integer32Abs, __VA_ARGS__)      \
MACRO(int32, Integer3201, __VA_ARGS__)      \
MACRO(int64, Integer64, __VA_ARGS__)      \
MACRO(float, Float, __VA_ARGS__)      \
MACRO(double, Double, __VA_ARGS__)     \
MACRO(double, DoubleAbs, __VA_ARGS__)     \
MACRO(double, Double01, __VA_ARGS__)     \
MACRO(double, Double11, __VA_ARGS__)     \
MACRO(FVector2D, Vector2, __VA_ARGS__)  \
MACRO(FVector, Vector, __VA_ARGS__)    \
MACRO(FVector, Direction, __VA_ARGS__)    \
MACRO(FVector4, Vector4, __VA_ARGS__)   \
MACRO(FRotator, Rotator, __VA_ARGS__)   \
MACRO(FTransform, Transform, __VA_ARGS__) \
MACRO(FString, String, __VA_ARGS__)    \
MACRO(FName, Name, __VA_ARGS__) \
MACRO(FSoftObjectPath, SoftObjectPath, __VA_ARGS__)

#define PCGEX_SHORTHAND_UPDATE__NAME_IMPL(_TYPE, _NAME)\
void FPCGExInputShorthandName##_NAME::Update(EPCGExInputValueType InInputType, const FPCGAttributePropertyInputSelector& InSelector, _TYPE InConstant){Input = InInputType; Constant = InConstant;	Attribute = InSelector.GetName();}\
void FPCGExInputShorthandName##_NAME::Update(EPCGExInputValueType InInputType, FName InSelector, _TYPE InConstant){Input = InInputType; Constant = InConstant;	Attribute = InSelector;}\
bool FPCGExInputShorthandName##_NAME::CanSupportDataOnly() const { return Input == EPCGExInputValueType::Constant ? true : PCGExMetaHelpers::IsDataDomainAttribute(Attribute); }

#define PCGEX_SHORTHAND_UPDATE__SELECTOR_IMPL(_TYPE, _NAME)\
void FPCGExInputShorthandSelector##_NAME::Update(EPCGExInputValueType InInputType, const FPCGAttributePropertyInputSelector& InSelector, _TYPE InConstant){Input = InInputType; Constant = InConstant;	Attribute = InSelector;}\
void FPCGExInputShorthandSelector##_NAME::Update(EPCGExInputValueType InInputType, FName InSelector, _TYPE InConstant){Input = InInputType; Constant = InConstant;	Attribute.Update(InSelector.ToString());}\
bool FPCGExInputShorthandSelector##_NAME::CanSupportDataOnly() const { return Input == EPCGExInputValueType::Constant ? true : PCGExMetaHelpers::IsDataDomainAttribute(Attribute); }

#define PCGEX_TPL_SHORTHAND_NAME(_TYPE, _NAME, ...)\
PCGEX_SETTING_VALUE_IMPL_SHORTHAND(FPCGExInputShorthandName##_NAME, , _TYPE, Input, Attribute, Constant)\
PCGEX_SETTING_DATA_VALUE_IMPL_SHORTHAND(FPCGExInputShorthandName##_NAME, , _TYPE, Input, Attribute, Constant)\
bool FPCGExInputShorthandName##_NAME::TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, _TYPE& OutValue, const bool bQuiet) const{return PCGExData::Helpers::TryGetSettingDataValue(IO, Input, Attribute, Constant, OutValue, bQuiet);}\
bool FPCGExInputShorthandName##_NAME::TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, _TYPE& OutValue, const bool bQuiet) const{return PCGExData::Helpers::TryGetSettingDataValue(InContext, InData, Input, Attribute, Constant, OutValue, bQuiet);}\
void FPCGExInputShorthandName##_NAME::RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const { if (Input == EPCGExInputValueType::Attribute) { FacadePreloader.Register<_TYPE>(InContext, Attribute); } }\
PCGEX_SHORTHAND_UPDATE__NAME_IMPL(_TYPE, _NAME)

#define PCGEX_TPL_SHORTHAND_SELECTOR(_TYPE, _NAME, ...)\
PCGEX_SETTING_VALUE_IMPL_SHORTHAND(FPCGExInputShorthandSelector##_NAME, , _TYPE, Input, Attribute, Constant)\
PCGEX_SETTING_DATA_VALUE_IMPL_SHORTHAND(FPCGExInputShorthandSelector##_NAME, , _TYPE, Input, Attribute, Constant)\
bool FPCGExInputShorthandSelector##_NAME::TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, _TYPE& OutValue, const bool bQuiet) const{return PCGExData::Helpers::TryGetSettingDataValue(IO, Input, Attribute, Constant, OutValue, bQuiet);}\
bool FPCGExInputShorthandSelector##_NAME::TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, _TYPE& OutValue, const bool bQuiet) const{return PCGExData::Helpers::TryGetSettingDataValue(InContext, InData, Input, Attribute, Constant, OutValue, bQuiet);}\
void FPCGExInputShorthandSelector##_NAME::RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const { if (Input == EPCGExInputValueType::Attribute) { FacadePreloader.Register<_TYPE>(InContext, Attribute); } }\
PCGEX_SHORTHAND_UPDATE__SELECTOR_IMPL(_TYPE, _NAME)

PCGEX_FOREACH_INPUT_SHORTHAND(PCGEX_TPL_SHORTHAND_NAME)
PCGEX_FOREACH_INPUT_SHORTHAND(PCGEX_TPL_SHORTHAND_SELECTOR)

#undef PCGEX_TPL_SHORTHAND_NAME
#undef PCGEX_TPL_SHORTHAND_SELECTOR
#undef PCGEX_FOREACH_INPUT_SHORTHAND

#if WITH_EDITOR

#pragma region PCGExDeprecation

namespace PCGExDeprecation
{
	void RenameShorthandOverridePin(const UPCGSettings* InSettings, UPCGNode* InOutNode, const FName InOldName, const FName InMemberName, const FName InLeafName, const FName InOldDisplayName)
	{
		const FName PathSuffix[] = {InMemberName, InLeafName};
		RenameShorthandOverridePin(InSettings, InOutNode, InOldName, PathSuffix, InOldDisplayName);
	}

	void RenameShorthandOverridePin(const UPCGSettings* InSettings, UPCGNode* InOutNode, const FName InOldName, const TArrayView<const FName> InNewPathSuffix, const FName InOldDisplayName)
	{
		if (!InSettings || !InOutNode || InOldName.IsNone() || InNewPathSuffix.IsEmpty()) { return; }

		// Resolve the new label from the freshly gathered params — the exact source UpdatePins
		// builds pins from. Labels can be bare, member-qualified or root-path-qualified depending
		// on clash disambiguation, so they must never be predicted statically.
		const FPCGSettingsOverridableParam* NewParam = nullptr;
		for (const FPCGSettingsOverridableParam& Param : InSettings->OverridableParams())
		{
			const int32 Offset = Param.PropertiesNames.Num() - InNewPathSuffix.Num();
			if (Offset < 0) { continue; }

			bool bMatchesSuffix = true;
			for (int32 i = 0; i < InNewPathSuffix.Num(); i++)
			{
				if (Param.PropertiesNames[Offset + i] != InNewPathSuffix[i])
				{
					bMatchesSuffix = false;
					break;
				}
			}

			if (!bMatchesSuffix) { continue; }

			if (NewParam)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[%s] Ambiguous override param suffix '%s/%s' while deprecating pin '%s' — pass a longer path suffix."),
				       *InSettings->GetName(), *InNewPathSuffix[0].ToString(), *InNewPathSuffix.Last().ToString(), *InOldName.ToString());
				return;
			}

			NewParam = &Param;
		}

		// Param absent (not overridable / renamed since), or the target pin already exists.
		if (!NewParam || InOutNode->GetInputPin(NewParam->Label)) { return; }

		// Resolve the old serialized pin: exact label first, else the unique segment-qualified
		// ".../OldName" (the old property itself may have been clash-disambiguated).
		FName OldLabel = NAME_None;
		if (InOutNode->GetInputPin(InOldName)) { OldLabel = InOldName; }
		else
		{
			const FString OldNameSuffix = FString::Printf(TEXT("/%s"), *InOldName.ToString());
			for (const UPCGPin* Pin : InOutNode->GetInputPins())
			{
				if (!Pin || !Pin->Properties.Label.ToString().EndsWith(OldNameSuffix)) { continue; }

				if (!OldLabel.IsNone())
				{
					UE_LOG(LogPCGEx, Warning, TEXT("[%s] Ambiguous old override pin '%s' during deprecation — pins '%s' and '%s' both match."),
					       *InSettings->GetName(), *InOldName.ToString(), *OldLabel.ToString(), *Pin->Properties.Label.ToString());
					return;
				}

				OldLabel = Pin->Properties.Label;
			}
		}

		// Display-name safety net: very old assets carry pins labeled from GetDisplayNameText (pre-authored-name
		// engine labeling). Only consulted when no authored-name pin matched; one-time deprecation cost, and
		// preserving user connections outweighs the (label-unique-per-node) collision risk.
		if (OldLabel.IsNone() && !InOldDisplayName.IsNone() && InOutNode->GetInputPin(InOldDisplayName))
		{
			OldLabel = InOldDisplayName;
		}

		if (OldLabel.IsNone())
		{
			const FString OldNameStr = InOldName.ToString();
			const bool bLooksLikeBool = OldNameStr.Len() > 1 && OldNameStr[0] == TEXT('b') && FChar::IsUpper(OldNameStr[1]);
			const FName DefaultDisplayLabel = FName(FName::NameToDisplayString(OldNameStr, bLooksLikeBool));
			if (InOutNode->GetInputPin(DefaultDisplayLabel)) { OldLabel = DefaultDisplayLabel; }
		}

		if (OldLabel.IsNone()) { return; }

		InOutNode->RenameInputPin(OldLabel, NewParam->Label);
	}
}

#pragma endregion

#endif
