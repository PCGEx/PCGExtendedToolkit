// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/Registry/PCGDataType.h"

///

class UPCGExPointFilterFactoryData;
class UPCGData;
struct FPCGExContext;
class UPCGExFactoryData;

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;
}

struct FPCGExFactoryProviderContext;

namespace PCGExFactories
{
	enum class EPreparationResult : uint8
	{
		None = 0,
		Success,
		MissingData,
		Fail
	};

}

namespace PCGExFactories
{
	PCGEXCORE_API
	bool GetInputFactories_Internal(FPCGExContext* InContext, const FName InLabel, TArray<TObjectPtr<const UPCGExFactoryData>>& OutFactories, const TSet<FPCGDataTypeBaseId>& Types, const bool bRequired);

	template <typename T_DEF>
	static bool GetInputFactories(FPCGExContext* InContext, const FName InLabel, TArray<TObjectPtr<const T_DEF>>& OutFactories, const TSet<FPCGDataTypeBaseId>& Types, const bool bRequired = true)
	{
		TArray<TObjectPtr<const UPCGExFactoryData>> BaseFactories;
		if (!GetInputFactories_Internal(InContext, InLabel, BaseFactories, Types, bRequired))
		{
			return false;
		}

		// Cast back to T_DEF
		for (const TObjectPtr<const UPCGExFactoryData>& Base : BaseFactories)
		{
			if (const T_DEF* Derived = Cast<T_DEF>(Base))
			{
				OutFactories.Add(Derived);
			}
		}

		return !OutFactories.IsEmpty();
	}

	PCGEXCORE_API
	void RegisterConsumableAttributesWithData_Internal(const TArray<TObjectPtr<const UPCGExFactoryData>>& InFactories, FPCGExContext* InContext, const UPCGData* InData);

	PCGEXCORE_API
	void RegisterConsumableAttributesWithFacade_Internal(const TArray<TObjectPtr<const UPCGExFactoryData>>& InFactories, const TSharedPtr<PCGExData::FFacade>& InFacade);

	template <typename T_DEF>
	static void RegisterConsumableAttributesWithData(const TArray<TObjectPtr<const T_DEF>>& InFactories, FPCGExContext* InContext, const UPCGData* InData)
	{
		TArray<TObjectPtr<const UPCGExFactoryData>> BaseFactories;
		BaseFactories.Reserve(InFactories.Num());
		for (const TObjectPtr<const T_DEF>& Factory : InFactories)
		{
			BaseFactories.Add(Factory);
		}
		RegisterConsumableAttributesWithData_Internal(BaseFactories, InContext, InData);
	}

	template <typename T_DEF>
	static void RegisterConsumableAttributesWithFacade(const TArray<TObjectPtr<const T_DEF>>& InFactories, const TSharedPtr<PCGExData::FFacade>& InFacade)
	{
		TArray<TObjectPtr<const UPCGExFactoryData>> BaseFactories;
		BaseFactories.Reserve(InFactories.Num());
		for (const TObjectPtr<const T_DEF>& Factory : InFactories)
		{
			BaseFactories.Add(Factory);
		}
		RegisterConsumableAttributesWithFacade_Internal(BaseFactories, InFacade);
	}
}
