// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBitmaskCommon.h"
#include "PCGExBitmaskDetails.h"

class UPCGExBitmaskCollection;

namespace PCGExBitmask
{
	class PCGEXCORE_API FBitmaskData : public TSharedFromThis<FBitmaskData>
	{
	public:
		TArray<FVector> Directions;
		TArray<FPCGExSimpleBitmask> Bitmasks;
		TArray<double> Dots;

		FBitmaskData() = default;

		void Append(const UPCGExBitmaskCollection* InCollection, const double InAngle, EPCGExBitOp Op = EPCGExBitOp::OR);
		void Append(const FPCGExBitmaskRef& InBitmaskRef, const double InAngle);
		void Append(const TArray<FPCGExBitmaskRef>& InBitmaskRef, const double InAngle);

		void MutateMatch(const FVector& InDirection, int64& Flags) const;
		void MutateUnmatch(const FVector& InDirection, int64& Flags) const;

		static TSharedPtr<FBitmaskData> Make(const TMap<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR>& InCollections, const TArray<FPCGExBitmaskRef>& InReferences, const double Angle);
	};
}
