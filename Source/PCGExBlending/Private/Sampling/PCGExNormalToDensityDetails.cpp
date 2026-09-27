// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/PCGExNormalToDensityDetails.h"

#include "PCGElement.h"
#include "PCGModule.h"
#include "PCGExCoreMacros.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"

namespace PCGExSampling
{
	bool FNormalToDensity::Init(const FPCGExNormalToDensityDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		Mode = InDetails.Mode;

		switch (Mode)
		{
		case EPCGExNormalToDensityMode::Set:
		case EPCGExNormalToDensityMode::Minimum:
		case EPCGExNormalToDensityMode::Maximum:
		case EPCGExNormalToDensityMode::Add:
		case EPCGExNormalToDensityMode::Subtract:
		case EPCGExNormalToDensityMode::Multiply:
		case EPCGExNormalToDensityMode::Divide:
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, InDataFacade->GetContext(), FTEXT("Normal to Density : unknown density mode."));
			return false;
		}

		Normal = InDetails.Normal.GetValueSetting();
		if (!Normal->Init(InDataFacade))
		{
			return false;
		}

		Offset = InDetails.Offset.GetValueSetting();
		if (!Offset->Init(InDataFacade))
		{
			return false;
		}

		Strength = InDetails.Strength.GetValueSetting();
		if (!Strength->Init(InDataFacade))
		{
			return false;
		}

		bConstant = Normal->IsConstant() && Offset->IsConstant() && Strength->IsConstant();
		if (bConstant)
		{
			ConstantNormal = Normal->Read(0).GetSafeNormal();
			ConstantOffset = Offset->Read(0);
			ConstantInvStrength = 1.0 / FMath::Max(MinStrength, Strength->Read(0));
		}

		return true;
	}

	void FNormalToDensity::PrepareScope(const PCGExMT::FScope& Scope, FScopeView& OutView) const
	{
		if (bConstant)
		{
			return;
		}

		OutView.Normals.SetNumUninitialized(Scope.Count);
		OutView.Offsets.SetNumUninitialized(Scope.Count);
		OutView.InvStrengths.SetNumUninitialized(Scope.Count);

		Normal->ReadScope(Scope.Start, OutView.Normals);
		Offset->ReadScope(Scope.Start, OutView.Offsets);
		Strength->ReadScope(Scope.Start, OutView.InvStrengths);

		for (FVector& N : OutView.Normals)
		{
			N = N.GetSafeNormal();
		}

		for (double& S : OutView.InvStrengths)
		{
			S = 1.0 / FMath::Max(MinStrength, S);
		}
	}
}
