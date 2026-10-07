// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UStruct;

/**
 * Dot-separated member path resolution against reflected containers.
 *
 * Resolves paths like "Weight", "AssetGrammar.SizingX.FixedSize" or
 * "ISMDescriptor.InstanceStartCullDistance" to a terminal FProperty and its value address.
 * Intermediate segments must be struct-typed properties; the terminal may be any property,
 * including whole arrays and whole structs. No array-index addressing ("Foo[2].Bar").
 *
 * Roots may be UScriptStruct (entry structs) or UClass (UObject members) -- both walk their
 * Super chain through FindFProperty. Segment names match the authored property names
 * (FProperty::GetFName), which is also what reflection-driven pickers emit.
 */
namespace PCGExMemberPath
{
	struct PCGEXCORE_API FResolvedMember
	{
		const FProperty* Property = nullptr;
		/** Value address inside the container; only valid as long as the container is. */
		void* Address = nullptr;

		bool IsValid() const
		{
			return Property != nullptr && Address != nullptr;
		}
	};

	/** Resolve a member path against (Root layout, Container memory). Invalid result on
	 *  empty path, unknown segment, or non-struct intermediate. */
	PCGEXCORE_API FResolvedMember Resolve(const UStruct* Root, void* Container, FName MemberPath);
}
