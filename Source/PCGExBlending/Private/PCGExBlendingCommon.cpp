// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExBlendingCommon.h"

#include "PCGData.h"

namespace PCGExBlending
{
	void ConvertBlending(const EPCGExBlendingType From, EPCGExABBlendingType& OutTo)
	{
		switch (From)
		{
		case EPCGExBlendingType::None:
			OutTo = EPCGExABBlendingType::None;
			break;
		case EPCGExBlendingType::Average:
			OutTo = EPCGExABBlendingType::Average;
			break;
		case EPCGExBlendingType::Weight:
			OutTo = EPCGExABBlendingType::Weight;
			break;
		case EPCGExBlendingType::Min:
			OutTo = EPCGExABBlendingType::Min;
			break;
		case EPCGExBlendingType::Max:
			OutTo = EPCGExABBlendingType::Max;
			break;
		case EPCGExBlendingType::Copy:
			OutTo = EPCGExABBlendingType::CopySource;
			break;
		case EPCGExBlendingType::Sum:
			OutTo = EPCGExABBlendingType::Add;
			break;
		case EPCGExBlendingType::WeightedSum:
			OutTo = EPCGExABBlendingType::WeightedAdd;
			break;
		case EPCGExBlendingType::Lerp:
			OutTo = EPCGExABBlendingType::Lerp;
			break;
		case EPCGExBlendingType::Subtract:
			OutTo = EPCGExABBlendingType::Subtract;
			break;
		case EPCGExBlendingType::UnsignedMin:
			OutTo = EPCGExABBlendingType::UnsignedMin;
			break;
		case EPCGExBlendingType::UnsignedMax:
			OutTo = EPCGExABBlendingType::UnsignedMax;
			break;
		case EPCGExBlendingType::AbsoluteMin:
			OutTo = EPCGExABBlendingType::AbsoluteMin;
			break;
		case EPCGExBlendingType::AbsoluteMax:
			OutTo = EPCGExABBlendingType::AbsoluteMax;
			break;
		case EPCGExBlendingType::WeightedSubtract:
			OutTo = EPCGExABBlendingType::WeightedSubtract;
			break;
		case EPCGExBlendingType::CopyOther:
			OutTo = EPCGExABBlendingType::CopyTarget;
			break;
		case EPCGExBlendingType::Hash:
			OutTo = EPCGExABBlendingType::Hash;
			break;
		case EPCGExBlendingType::UnsignedHash:
			OutTo = EPCGExABBlendingType::UnsignedHash;
			break;
		case EPCGExBlendingType::WeightNormalize:
			OutTo = EPCGExABBlendingType::WeightNormalize;
			break;
		default:
		case EPCGExBlendingType::Unset:
			OutTo = EPCGExABBlendingType::None;
			break;
		}
	}

	bool RequiresWeight(const EPCGExABBlendingType Mode)
	{
		switch (Mode)
		{
		case EPCGExABBlendingType::Weight:
		case EPCGExABBlendingType::WeightNormalize:
		case EPCGExABBlendingType::WeightedAdd:
		case EPCGExABBlendingType::WeightedSubtract:
		case EPCGExABBlendingType::Lerp:
		case EPCGExABBlendingType::GeometricMean:
		case EPCGExABBlendingType::HarmonicMean:
		case EPCGExABBlendingType::RMS:
		case EPCGExABBlendingType::Step:
			return true;
		case EPCGExABBlendingType::None:
		case EPCGExABBlendingType::Average:
		case EPCGExABBlendingType::Multiply:
		case EPCGExABBlendingType::Divide:
		case EPCGExABBlendingType::Min:
		case EPCGExABBlendingType::Max:
		case EPCGExABBlendingType::CopyTarget:
		case EPCGExABBlendingType::CopySource:
		case EPCGExABBlendingType::Add:
		case EPCGExABBlendingType::Subtract:
		case EPCGExABBlendingType::UnsignedMin:
		case EPCGExABBlendingType::UnsignedMax:
		case EPCGExABBlendingType::AbsoluteMin:
		case EPCGExABBlendingType::AbsoluteMax:
		case EPCGExABBlendingType::Hash:
		case EPCGExABBlendingType::UnsignedHash:
		case EPCGExABBlendingType::Mod:
		case EPCGExABBlendingType::ModCW:
			return false;
		default:
			checkNoEntry();
			return false;
		}
	}

	void FBlendingParam::SelectFromString(const FString& Selection)
	{
		Identifier = FName(*Selection);
		Selector.Update(Selection);
	}

	void FBlendingParam::Select(const FPCGAttributeIdentifier& InIdentifier)
	{
		Identifier = InIdentifier;
		Selector.Update(InIdentifier.Name.ToString());

		if (InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
		{
			Selector.SetDomainName(PCGDataConstants::DataDomainName);
		}
		else
		{
			Selector.SetDomainName(PCGDataConstants::DefaultDomainName);
		}
	}

	void FBlendingParam::SetBlending(const EPCGExBlendingType InBlending)
	{
		ConvertBlending(InBlending, Blending);
	}
}
