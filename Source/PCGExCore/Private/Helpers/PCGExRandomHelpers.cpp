// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExRandomHelpers.h"

#include "PCGComponent.h"
#include "PCGSettings.h"
#include "Helpers/PCGHelpers.h"

namespace PCGExRandomHelpers
{
	int32 GetSeed(const int32 BaseSeed, const uint8 Flags, const int32 Local, const UPCGSettings* Settings, const UPCGComponent* Component)
	{
		int32 Seed = BaseSeed;

		const bool bHasLocalFlag = (Flags & static_cast<uint8>(EPCGExSeedComponents::Local)) != 0;
		const bool bHasSettingsFlag = (Flags & static_cast<uint8>(EPCGExSeedComponents::Settings)) != 0;
		const bool bHasComponentFlag = (Flags & static_cast<uint8>(EPCGExSeedComponents::Component)) != 0;

		if (bHasLocalFlag)
		{
			Seed = PCGHelpers::ComputeSeed(Seed, Local);
		}

		if (bHasSettingsFlag || bHasComponentFlag)
		{
			if (Settings && Component)
			{
				Seed = PCGHelpers::ComputeSeed(Seed, Settings->Seed, Component->Seed);
			}
			else if (Settings)
			{
				Seed = PCGHelpers::ComputeSeed(Seed, Settings->Seed);
			}
			else if (Component)
			{
				Seed = PCGHelpers::ComputeSeed(Seed, Component->Seed);
			}
		}

		return Seed;
	}

	int32 GetSeed(const int32 BaseSeed, const int32 Local, const UPCGSettings* Settings, const UPCGComponent* Component)
	{
		int32 Seed = PCGHelpers::ComputeSeed(BaseSeed, Local);

		if (Settings && Component)
		{
			Seed = PCGHelpers::ComputeSeed(Seed, Settings->Seed, Component->Seed);
		}
		else if (Settings)
		{
			Seed = PCGHelpers::ComputeSeed(Seed, Settings->Seed);
		}
		else if (Component)
		{
			Seed = PCGHelpers::ComputeSeed(Seed, Component->Seed);
		}

		return Seed;
	}

	void FSeedResolver::Init(const uint8 Flags, const int32 InLocal, const UPCGSettings* Settings, const UPCGComponent* Component)
	{
		bLocal = (Flags & static_cast<uint8>(EPCGExSeedComponents::Local)) != 0;
		Local = InLocal;

		Stage = EStage::None;
		SettingsSeed = 0;
		ComponentSeed = 0;

		const uint8 StageFlags = static_cast<uint8>(EPCGExSeedComponents::Settings) | static_cast<uint8>(EPCGExSeedComponents::Component);
		if ((Flags & StageFlags) == 0)
		{
			return;
		}

		if (Settings)
		{
			SettingsSeed = Settings->Seed;
		}
		if (Component)
		{
			ComponentSeed = Component->Seed;
		}

		if (Settings && Component)
		{
			Stage = EStage::Both;
		}
		else if (Settings)
		{
			Stage = EStage::Settings;
		}
		else if (Component)
		{
			Stage = EStage::Component;
		}
	}

	int32 FSeedResolver::Resolve(const int32 BaseSeed) const
	{
		const int32 Seed = bLocal ? PCGHelpers::ComputeSeed(BaseSeed, Local) : BaseSeed;

		switch (Stage)
		{
		case EStage::Both:
			return PCGHelpers::ComputeSeed(Seed, SettingsSeed, ComponentSeed);
		case EStage::Settings:
			return PCGHelpers::ComputeSeed(Seed, SettingsSeed);
		case EStage::Component:
			return PCGHelpers::ComputeSeed(Seed, ComponentSeed);
		default:
			return Seed;
		}
	}

	FRandomStream GetRandomStreamFromPoint(const int32 BaseSeed, const int32 Offset, const UPCGSettings* Settings, const UPCGComponent* Component)
	{
		return FRandomStream(GetSeed(BaseSeed, Offset, Settings, Component));
	}

	int ComputeSpatialSeed(const FVector& Origin, const FVector& Offset)
	{
		return PCGHelpers::ComputeSeed(PCGHelpers::ComputeSeedFromPosition(Origin), PCGHelpers::ComputeSeedFromPosition(Offset));
	}
}
