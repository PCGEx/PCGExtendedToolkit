// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"

namespace PCGExActorClassificationRules
{
	/** The default classificators' rule: any exclude match rejects, else any include match accepts. Unloaded classes never match. */
	inline bool Matches(
		const AActor* Actor,
		const TConstArrayView<TSoftClassPtr<AActor>> IncludeClasses, const TConstArrayView<FName> IncludeTags,
		const TConstArrayView<TSoftClassPtr<AActor>> ExcludeClasses, const TConstArrayView<FName> ExcludeTags)
	{
		if (!Actor)
		{
			return false;
		}

		auto IsAnyOf = [Actor](const TConstArrayView<TSoftClassPtr<AActor>> Classes)
		{
			for (const TSoftClassPtr<AActor>& ClassPtr : Classes)
			{
				if (const UClass* Class = ClassPtr.Get(); Class && Actor->IsA(Class))
				{
					return true;
				}
			}
			return false;
		};

		auto HasAnyOf = [Actor](const TConstArrayView<FName> Tags)
		{
			for (const FName& Tag : Tags)
			{
				if (Actor->Tags.Contains(Tag))
				{
					return true;
				}
			}
			return false;
		};

		if (IsAnyOf(ExcludeClasses) || HasAnyOf(ExcludeTags))
		{
			return false;
		}

		return IsAnyOf(IncludeClasses) || HasAnyOf(IncludeTags);
	}
}
