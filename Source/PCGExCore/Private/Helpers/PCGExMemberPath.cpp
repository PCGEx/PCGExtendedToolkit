// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExMemberPath.h"

#include "UObject/UnrealType.h"

namespace PCGExMemberPath
{
	FResolvedMember Resolve(const UStruct* Root, void* Container, FName MemberPath)
	{
		FResolvedMember Result;

		if (!Root || !Container || MemberPath.IsNone())
		{
			return Result;
		}

		TArray<FString> Segments;
		MemberPath.ToString().ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.IsEmpty())
		{
			return Result;
		}

		const UStruct* CurrentStruct = Root;
		void* CurrentContainer = Container;

		for (int32 i = 0; i < Segments.Num(); i++)
		{
			const FProperty* Property = FindFProperty<FProperty>(CurrentStruct, FName(*Segments[i]));
			if (!Property)
			{
				return Result;
			}

			if (i == Segments.Num() - 1)
			{
				Result.Property = Property;
				Result.Address = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
				return Result;
			}

			// Intermediate segments must be plain struct members so the walk can descend.
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty)
			{
				return Result;
			}

			CurrentContainer = StructProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProperty->Struct;
		}

		return Result;
	}
}
