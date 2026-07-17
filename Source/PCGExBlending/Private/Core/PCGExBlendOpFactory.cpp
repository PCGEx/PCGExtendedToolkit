// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlendOpFactory.h"

#include "PCGExBlendingCommon.h"
#include "Core/PCGExBlendOpFactoryProvider.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "GeometryCollection/Facades/CollectionPositionTargetFacade.h"

#define LOCTEXT_NAMESPACE "PCGExCreateAttributeBlend"
#define PCGEX_NAMESPACE CreateAttributeBlend

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoBlendOp, UPCGExBlendOpFactory)

void FPCGExAttributeBlendWeight::Init()
{
	ScoreLUT = WeightCurveLookup.MakeLookup(
		bUseLocalCurve, LocalWeightCurve, WeightCurve,
		[](FRichCurve& CurveData)
		{
			CurveData.AddKey(0, 0);
			CurveData.AddKey(1, 1);
		});
}

#if WITH_EDITOR
void FPCGExAttributeBlendWeight::ApplyDeprecation()
{
	WeightValue.Update(WeightInput_DEPRECATED, WeightAttribute_DEPRECATED, Weight_DEPRECATED);
}

void FPCGExAttributeBlendWeight::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Weight")), FName(TEXT("WeightValue")), FName(TEXT("Constant")), FName(TEXT("Weight")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("WeightAttribute")), FName(TEXT("WeightValue")), FName(TEXT("Attribute")), FName(TEXT("Weight (Attr)")));
}
#endif

void FPCGExAttributeBlendConfig::Init()
{
	bRequiresWeight =
		BlendMode == EPCGExABBlendingType::Lerp
		|| BlendMode == EPCGExABBlendingType::Weight
		|| BlendMode == EPCGExABBlendingType::WeightedSubtract
		|| BlendMode == EPCGExABBlendingType::WeightedAdd;

	Weighting.Init();
}

bool FPCGExBlendOperation::PrepareForData(FPCGExContext* InContext)
{
	Weight = Config.Weighting.WeightValue.GetValueSetting();
	Weight->bRegisterConsumable &= bCleanupConsumableAttributes;
	if (!Weight->Init(WeightFacade))
	{
		return false;
	}

	// Fix @Selectors based on siblings 
	if (!CopyAndFixSiblingSelector(InContext, Config.OperandA))
	{
		return false;
	}
	if (Config.bUseOperandB)
	{
		if (!CopyAndFixSiblingSelector(InContext, Config.OperandB))
		{
			return false;
		}
	}
	else
	{
		Config.OperandB = Config.OperandA;
	}

	switch (Config.OutputMode)
	{
	case EPCGExBlendOpOutputMode::SameAsA:
		Config.OutputTo = Config.OperandA;
		break;
	case EPCGExBlendOpOutputMode::SameAsB:
		Config.OutputTo = Config.OperandB;
		break;
	case EPCGExBlendOpOutputMode::New:
	case EPCGExBlendOpOutputMode::Transient:
		if (!CopyAndFixSiblingSelector(InContext, Config.OutputTo))
		{
			return false;
		}
		break;
	}

	// Build output descriptor
	PCGExData::FProxyDescriptor C = PCGExData::FProxyDescriptor(TargetFacade, PCGExData::EProxyRole::Write);
	C.Role = PCGExData::EProxyRole::Write;
	C.Side = PCGExData::EIOSide::Out;

	Config.OutputTo = C.Selector = Config.OutputTo.CopyAndFixLast(TargetFacade->Source->GetOut());
	C.UpdateSubSelection();

	// Build main source descriptor
	PCGExData::FProxyDescriptor A = PCGExData::FProxyDescriptor(ConstantA ? ConstantA : Source_A_Facade, PCGExData::EProxyRole::Read);
	if (A.DataFacade.Pin() != Source_A_Facade)
	{
		A.AddFlags(PCGExData::EProxyFlags::Constant);
		if (!A.Capture(InContext, Config.OperandA, PCGExData::EIOSide::In))
		{
			return false;
		}
	}
	else
	{
		if (!A.Capture(InContext, Config.OperandA, SideA))
		{
			return false;
		}
	}

	// Build secondary source descriptor
	bool bSkipSourceB = false;
	PCGExData::FProxyDescriptor B;

	if (bUsedForMultiBlendOnly || Config.BlendMode == EPCGExABBlendingType::CopySource)
	{
		bSkipSourceB = true;
		B = C;
	}
	else
	{
		B = PCGExData::FProxyDescriptor(ConstantB ? ConstantB : Source_B_Facade, PCGExData::EProxyRole::Read);
		if (B.DataFacade.Pin() != Source_B_Facade)
		{
			B.AddFlags(PCGExData::EProxyFlags::Constant);
			if (!B.Capture(InContext, Config.OperandB, PCGExData::EIOSide::In))
			{
				return false;
			}
		}
		else
		{
			if (!B.Capture(InContext, Config.OperandB, SideB))
			{
				return false;
			}
		}
	}

	Config.OperandA = A.Selector;
	Config.OperandB = B.Selector;

	PCGExData::FSubSelection OutputSubselection(Config.OutputTo);
	EPCGMetadataTypes RealTypeC = EPCGMetadataTypes::Unknown;
	EPCGMetadataTypes WorkingTypeC = EPCGMetadataTypes::Unknown;

	if (Config.OutputTo.GetSelection() == EPCGAttributePropertySelection::ExtraProperty)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Only attributes and point properties are supported as outputs; it's not possible to write to extras."));
		return false;
	}
	if (Config.OutputTo.GetSelection() == EPCGAttributePropertySelection::Attribute)
	{
		if (const FPCGMetadataAttributeBase* OutAttribute = TargetFacade->GetOut()->Metadata->GetConstAttribute(PCGExMetaHelpers::GetAttributeIdentifier(Config.OutputTo, TargetFacade->GetOut())))
		{
			RealTypeC = PCGExMetaHelpers::GetAttributeType(OutAttribute);

			if ((Config.OutputType == EPCGExOperandAuthority::A && RealTypeC != A.RealType)
				|| (Config.OutputType == EPCGExOperandAuthority::B && RealTypeC != B.RealType)
				|| (Config.OutputType == EPCGExOperandAuthority::Custom && RealTypeC != Config.CustomType))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("An output attribute existing type will differ from its desired type."));
			}
		}
		else
		{
			if (Config.OutputType == EPCGExOperandAuthority::A)
			{
				RealTypeC = A.RealType;
			}
			else if (Config.OutputType == EPCGExOperandAuthority::B)
			{
				RealTypeC = B.RealType;
			}
			else if (Config.OutputType == EPCGExOperandAuthority::Custom)
			{
				RealTypeC = Config.CustomType;
			}
			else if (Config.OutputType == EPCGExOperandAuthority::Auto)
			{
				if (OutAttribute)
				{
					// First, check for an existing attribute
					RealTypeC = PCGExMetaHelpers::GetAttributeType(OutAttribute);
					// TODO : Account for possible desired cast to a different type in the blend stack
				}

				if (OutputSubselection.HasSelection() && RealTypeC == EPCGMetadataTypes::Unknown)
				{
					// Take a wild guess based on subselection, if any
					RealTypeC = OutputSubselection.GetHintedSourceType();
				}

				if (RealTypeC == EPCGMetadataTypes::Unknown)
				{
					// Ok we really have little to work with,
					// take a guess based on other attribute types and pick the broader type

					EPCGMetadataTypes TypeA = A.SubSelection.IsFieldSelection() ? EPCGMetadataTypes::Double : A.RealType;
					EPCGMetadataTypes TypeB = B.SubSelection.IsFieldSelection() ? EPCGMetadataTypes::Double : B.RealType;

					auto GetMetadataRating = [](const EPCGMetadataTypes InType)
					{
						switch (InType)
						{
						default:
							return 0;
						case EPCGMetadataTypes::Boolean:
						case EPCGMetadataTypes::Float:
						case EPCGMetadataTypes::Double:
						case EPCGMetadataTypes::Integer32:
						case EPCGMetadataTypes::Integer64:
							return 1;
						case EPCGMetadataTypes::Vector2:
							return 2;
						case EPCGMetadataTypes::Vector:
						case EPCGMetadataTypes::Rotator:
							return 3;
						case EPCGMetadataTypes::Vector4:
						case EPCGMetadataTypes::Quaternion:
							return 4;
						case EPCGMetadataTypes::Transform:
							return 5;
						case EPCGMetadataTypes::String:
						case EPCGMetadataTypes::Name:
							return 6;
						case EPCGMetadataTypes::Unknown:
							return -1;
						}
					};

					int32 RatingA = GetMetadataRating(TypeA);
					int32 RatingB = GetMetadataRating(TypeB);

					RealTypeC = RatingA > RatingB ? TypeA : TypeB;
				}
			}
		}
	}
	else // Point property
	{
		// Derived properties ($LocalCenter, $LocalSize, $ScaledLocalSize) resolve to NAME_None: PCG
		// can't write them, so reject them here.
		if (UPCGExBlendOpFactory::GetOutputTargetName(Config).IsNone())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("This point property is not a supported blend output (PCG does not allow writing it). Blend its underlying components instead (e.g. $BoundsMin/$BoundsMax, $Scale)."));
			return false;
		}
		RealTypeC = PCGExMetaHelpers::GetPropertyType(Config.OutputTo.GetPointProperty());
	}

	if (RealTypeC == EPCGMetadataTypes::Unknown)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Could not infer output type."));
		return false;
	}

	WorkingTypeC = C.SubSelection.GetSubType(RealTypeC);

	A.WorkingType = WorkingTypeC;
	B.WorkingType = WorkingTypeC;

	C.RealType = RealTypeC;
	C.WorkingType = WorkingTypeC;

	// Property-backed participation gate. Extended-type scalars (Struct/Object/Class/
	// Soft*/Byte/Text/Enum) and containers (TArray/TSet/TMap) have no FPCGMetadataAttribute<T>
	// and no field-wise conversion table -- only memcpy / CopyCompleteValue. Restrict to
	// Copy modes, reject subselection and cross-type conversion, and propagate the source
	// template Desc onto C (constructed manually, so GetProxyBuffer's write fallback can
	// create the output attribute for Output Mode: New).
	{
		auto IsPropertyBacked = [](const PCGExData::FProxyDescriptor& D) -> bool
		{
			if (!PCGExMetaHelpers::IsLegacyScalarType(D.RealType))
			{
				return true;
			}
			if (D.SourceDesc.IsValid() && !D.SourceDesc.IsSingleValue())
			{
				return true;
			}
			return false;
		};

		const bool bAIsPropertyBacked = IsPropertyBacked(A);
		const bool bBIsPropertyBacked = !bSkipSourceB && IsPropertyBacked(B);
		const bool bCIsPropertyBacked = !PCGExMetaHelpers::IsLegacyScalarType(RealTypeC);

		if (bAIsPropertyBacked || bBIsPropertyBacked || bCIsPropertyBacked)
		{
			const bool bIsCopyOp =
				Config.BlendMode == EPCGExABBlendingType::CopySource ||
				Config.BlendMode == EPCGExABBlendingType::CopyTarget ||
				Config.BlendMode == EPCGExABBlendingType::None;

			if (!bIsCopyOp)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Only Copy blend modes are supported on extended-type / container attributes (Struct, Object, Soft*, Byte, Text, Enum, TArray/TSet/TMap)."));
				return false;
			}

			if (A.SubSelection.HasSelection() ||
				(!bSkipSourceB && B.SubSelection.HasSelection()) ||
				C.SubSelection.HasSelection())
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Subselection (e.g. .X, .Yaw) is not supported on extended-type / container attributes."));
				return false;
			}

			const bool bMatchesA = (RealTypeC == A.RealType);
			const bool bMatchesB = !bSkipSourceB && (RealTypeC == B.RealType);
			if (!bMatchesA && !bMatchesB)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Cannot convert between extended-type / container attributes and other types via blend ops. Output type must match the source type exactly."));
				return false;
			}

			// Propagate template Desc from whichever source matches RealTypeC,
			// so GetProxyBuffer can create the output attribute on a fresh write.
			if (!C.SourceDesc.IsValid())
			{
				const PCGExData::FProxyDescriptor& Template = bMatchesA ? A : B;
				if (Template.SourceDesc.IsValid())
				{
					C.SourceDesc = Template.SourceDesc;
				}
			}
		}
	}

	if (bSkipSourceB)
	{
		Blender = PCGExBlending::CreateProxyBlender(InContext, Config.BlendMode, A, C, Config.bResetValueBeforeMultiSourceBlend);
	}
	else
	{
		Blender = PCGExBlending::CreateProxyBlender(InContext, Config.BlendMode, A, B, C, Config.bResetValueBeforeMultiSourceBlend);
	}


	if (!Blender)
	{
		return false;
	}
	return true;
}

void FPCGExBlendOperation::BlendAutoWeight(const int32 SourceIndex, const int32 TargetIndex)
{
	Blender->Blend(SourceIndex, TargetIndex, Config.Weighting.ScoreLUT->Eval(Weight->Read(SourceIndex)));
}

void FPCGExBlendOperation::Blend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight)
{
	Blender->Blend(SourceIndex, TargetIndex, Config.Weighting.ScoreLUT->Eval(InWeight));
}

void FPCGExBlendOperation::Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double InWeight)
{
	Blender->Blend(SourceIndexA, SourceIndexB, TargetIndex, Config.Weighting.ScoreLUT->Eval(InWeight));
}

void FPCGExBlendOperation::BlendScope(const PCGExMT::FScope& Scope)
{
	TArray<double> Weights;
	Weights.SetNumUninitialized(Scope.Count);
	Weight->ReadScope(Scope.Start, Weights);
	Config.Weighting.ScoreLUT->EvalInPlace(Weights);

	Blender->BlendScope(Scope, Weights);
}

void FPCGExBlendOperation::BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask)
{
	TArray<double> Weights;
	Weights.SetNumUninitialized(Scope.Count);
	Weight->ReadScope(Scope.Start, Weights);
	Config.Weighting.ScoreLUT->EvalInPlace(Weights);

	Blender->BlendScope(Scope, Mask, Weights);
}

void FPCGExBlendOperation::BlendScope(const PCGExMT::FScope& Scope, const double InWeight)
{
	Blender->BlendScope(Scope, Config.Weighting.ScoreLUT->Eval(InWeight));
}

PCGEx::FOpStats FPCGExBlendOperation::BeginMultiBlend(const int32 TargetIndex)
{
	return Blender->BeginMultiBlend(TargetIndex);
}

void FPCGExBlendOperation::MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight, PCGEx::FOpStats& Tracker)
{
	Blender->MultiBlend(SourceIndex, TargetIndex, Config.Weighting.ScoreLUT->Eval(InWeight), Tracker);
}

void FPCGExBlendOperation::EndMultiBlend(const int32 TargetIndex, PCGEx::FOpStats& Tracker)
{
	Blender->EndMultiBlend(TargetIndex, Tracker);
}

void FPCGExBlendOperation::CompleteWork(TSet<TSharedPtr<PCGExData::IBuffer>>& OutDisabledBuffers)
{
	if (Blender)
	{
		if (TSharedPtr<PCGExData::IBuffer> OutputBuffer = Blender->GetOutputBuffer())
		{
			if (Config.OutputMode == EPCGExBlendOpOutputMode::Transient)
			{
				OutputBuffer->Disable();
				OutDisabledBuffers.Add(OutputBuffer);
			}
			else
			{
				OutputBuffer->Enable();
				OutDisabledBuffers.Remove(OutputBuffer);
			}
		}
	}
}

bool FPCGExBlendOperation::CopyAndFixSiblingSelector(FPCGExContext* InContext, FPCGAttributePropertyInputSelector& Selector) const
{
	// TODO : Support index shortcuts like @0 @1 @2 etc

	if (Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
	{
		if (Selector.GetAttributeName() == PCGExBlending::PreviousAttributeName)
		{
			const TSharedPtr<FPCGExBlendOperation> PreviousOperation = SiblingOperations && SiblingOperations->IsValidIndex(OpIdx - 1) ? (*SiblingOperations.Get())[OpIdx - 1] : nullptr;

			if (!PreviousOperation)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("There is no valid #Previous attribute. Check priority order!"));
				return false;
			}

			Selector = PreviousOperation->Config.OutputTo;
			return true;
		}

		{
			FString Shortcut = Selector.GetAttributeName().ToString();
			if (Shortcut.RemoveFromStart(TEXT("#")))
			{
				if (Shortcut.IsNumeric())
				{
					int32 Idx = FCString::Atoi(*Shortcut);
					const TSharedPtr<FPCGExBlendOperation> TargetOperation = SiblingOperations && SiblingOperations->IsValidIndex(Idx) ? (*SiblingOperations.Get())[Idx] : nullptr;

					if (TargetOperation.Get() == this)
					{
						PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attempting to reference self using #INDEX, this is not allowed -- you can only reference previous operations."));
						return false;
					}

					if (!TargetOperation)
					{
						PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("There is no valid operation at the specified #INDEX. Check priority order -- you can only reference previous operations."));
						return false;
					}

					Selector = TargetOperation->Config.OutputTo;
					return true;
				}
			}
		}
	}

	return true;
}

TSharedPtr<FPCGExBlendOperation> UPCGExBlendOpFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(BlendOperation)
	NewOperation->Config = Config;
	NewOperation->ConstantA = ConstantA;
	NewOperation->ConstantB = ConstantB;
	NewOperation->bCleanupConsumableAttributes = bCleanupConsumableAttributes;
	return NewOperation;
}

bool UPCGExBlendOpFactory::ResolveConfigs(
	FPCGExContext* InContext,
	const TSharedPtr<PCGExData::FFacade>& InSourceAFacade,
	TArray<FPCGExAttributeBlendConfig>& OutConfigs,
	const TSet<FName>* InSupersedeNames) const
{
	OutConfigs.Add(Config);
	return true;
}

bool UPCGExBlendOpFactory::CreateOperations(
	FPCGExContext* InContext,
	const TSharedPtr<PCGExData::FFacade>& InSourceAFacade,
	const TSharedPtr<PCGExData::FFacade>& InTargetFacade,
	TArray<TSharedPtr<FPCGExBlendOperation>>& OutOperations,
	const TSet<FName>* InSupersedeNames) const
{
	TArray<FPCGExAttributeBlendConfig> Configs;
	if (!ResolveConfigs(InContext, InSourceAFacade, Configs, InSupersedeNames))
	{
		return false;
	}

	OutOperations.Reserve(OutOperations.Num() + Configs.Num());
	for (FPCGExAttributeBlendConfig& ResolvedConfig : Configs)
	{
		TSharedPtr<FPCGExBlendOperation> Op = CreateOperation(InContext);
		if (!Op)
		{
			return false;
		}
		Op->Config = MoveTemp(ResolvedConfig);
		OutOperations.Add(Op);
	}
	return true;
}

FName UPCGExBlendOpFactory::GetOutputTargetName(const FPCGExAttributeBlendConfig& InConfig)
{
	const FPCGAttributePropertyInputSelector* RelevantSelector = nullptr;

	switch (InConfig.OutputMode)
	{
	case EPCGExBlendOpOutputMode::SameAsA:
		RelevantSelector = &InConfig.OperandA;
		break;
	case EPCGExBlendOpOutputMode::SameAsB:
		RelevantSelector = InConfig.bUseOperandB ? &InConfig.OperandB : &InConfig.OperandA;
		break;
	case EPCGExBlendOpOutputMode::New:
	case EPCGExBlendOpOutputMode::Transient:
		RelevantSelector = &InConfig.OutputTo;
		break;
	}

	if (!RelevantSelector)
	{
		return NAME_None;
	}

	const EPCGAttributePropertySelection Selection = RelevantSelector->GetSelection();

	// Point property: map to $PropertyName using the same macro as GetPointPropertyBlendingParams
	if (Selection != EPCGAttributePropertySelection::Attribute && Selection != EPCGAttributePropertySelection::ExtraProperty)
	{
		switch (RelevantSelector->GetPointProperty())
		{
#define PCGEX_MAP_PP(_NAME, ...) case EPCGPointProperties::_NAME: return FName(TEXT("$" #_NAME));
		PCGEX_FOREACH_BLEND_POINTPROPERTY(PCGEX_MAP_PP)
#undef PCGEX_MAP_PP
		// Composite outputs (not part of the blend macro). $LocalCenter/$LocalSize/$ScaledLocalSize are
		// intentionally absent -- PCG can't write them, so they fall through to NAME_None.
		case EPCGPointProperties::Transform: return FName(TEXT("$Transform"));
		case EPCGPointProperties::Extents: return FName(TEXT("$Extents"));
		default:
			return NAME_None;
		}
	}

	return RelevantSelector->GetAttributeName();
}

bool UPCGExBlendOpFactory::WantsPreparation(FPCGExContext* InContext)
{
	return InContext->InputData.GetInputCountByPin(PCGExBlending::Labels::SourceConstantA)
		|| InContext->InputData.GetInputCountByPin(PCGExBlending::Labels::SourceConstantB);
}

PCGExFactories::EPreparationResult UPCGExBlendOpFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	ConstantA = PCGExData::TryGetSingleFacade(InContext, PCGExBlending::Labels::SourceConstantA, true, false);
	if (Config.bUseOperandB)
	{
		ConstantB = PCGExData::TryGetSingleFacade(InContext, PCGExBlending::Labels::SourceConstantB, true, false);
	}

	if (ConstantA)
	{
		InContext->ManagedObjects->Remove(const_cast<UPCGBasePointData*>(ConstantA->Source->GetIn()));
		AddDataDependency(ConstantA->Source->GetIn());
	}

	if (ConstantB)
	{
		InContext->ManagedObjects->Remove(const_cast<UPCGBasePointData*>(ConstantB->Source->GetIn()));
		AddDataDependency(ConstantB->Source->GetIn());
	}

	return Result;
}

void UPCGExBlendOpFactory::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const
{
	Super::RegisterAssetDependencies(InDependencies);
	if (!Config.Weighting.bUseLocalCurve)
	{
		InDependencies.Add(Config.Weighting.WeightCurve.ToSoftObjectPath());
	}
}

bool UPCGExBlendOpFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandB, Consumable)

	return true;
}

void UPCGExBlendOpFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	RegisterBuffersDependenciesForSourceA(InContext, FacadePreloader);
	RegisterBuffersDependenciesForSourceB(InContext, FacadePreloader);
}

void UPCGExBlendOpFactory::RegisterBuffersDependenciesForSourceA(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	FacadePreloader.TryRegister(InContext, Config.OperandA);
}

void UPCGExBlendOpFactory::RegisterBuffersDependenciesForSourceB(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (Config.bUseOperandB)
	{
		FacadePreloader.TryRegister(InContext, Config.OperandB);
	}
	else
	{
		FacadePreloader.TryRegister(InContext, Config.OperandA);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
