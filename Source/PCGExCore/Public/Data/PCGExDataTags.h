// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataValue.h"
#include "UObject/Object.h"

#include "Types/PCGExTypeTraits.h"

#include "PCGExDataTags.generated.h"

#define PCGEX_FOREACH_SUPPORTEDTAGTYPE(MACRO) \
MACRO(Integer, int64) \
MACRO(FloatingPoint, double) \
MACRO(String, FString) \
MACRO(Vector2, FVector2D) \
MACRO(Vector, FVector) \
MACRO(Vector4, FVector4)

UENUM()
enum class EPCGExSupportedTagValue : uint8
{
	Integer       = 0 UMETA(DisplayName = "Integer", ToolTip="Int32 / Int64"),
	FloatingPoint = 1 UMETA(DisplayName = "Floating Point", ToolTip="Double / float"),
	String        = 2 UMETA(DisplayName = "String", ToolTip="String"),
	Vector2       = 3 UMETA(DisplayName = "Vector 2D", ToolTip="Vector 2D"),
	Vector        = 4 UMETA(DisplayName = "Vector", ToolTip="Vector"),
	Vector4       = 5 UMETA(DisplayName = "Vector 4", ToolTip="Vector 4"),
};

namespace PCGExData
{
	const FString TagSeparator = TEXT(":");

	class PCGEXCORE_API FTags : public TSharedFromThis<FTags>
	{
		mutable FRWLock TagsLock;

	public:
		TSet<FString> RawTags;                           // Contains all data tag
		TMap<FString, TSharedPtr<IDataValue>> ValueTags; // Prefix:ValueTag

		int32 Num() const;
		bool IsEmpty() const;

		FTags();
		explicit FTags(const TSet<FString>& InTags);
		explicit FTags(const TSharedPtr<FTags>& InTags);

		void Append(const TSharedRef<FTags>& InTags);
		void Append(const TArray<FString>& InTags);
		void Append(const TSet<FString>& InTags);

		void Reset();
		void Reset(const TSharedPtr<FTags>& InTags);

		void DumpTo(TSet<FString>& InTags, const bool bFlatten = true) const;
		void DumpTo(TArray<FName>& InTags, const bool bFlatten = true) const;


		TSet<FString> Flatten() const;
		TArray<FString> FlattenToArray(const bool bIncludeValue = true) const;
		TArray<FName> FlattenToArrayOfNames(const bool bIncludeValue = true) const;

		~FTags() = default;

		/* Simple add to raw tags */
		void AddRaw(const FString& Key);

		template <typename T>
		TSharedPtr<TDataValue<T>> GetOrSet(const FString& Key, const T& Value)
		{
			{
				FReadScopeLock ReadScopeLock(TagsLock);

				TArray<T> ValuesForKey;
				if (const TSharedPtr<IDataValue>* ValueTagPtr = ValueTags.Find(Key))
				{
					if ((*ValueTagPtr)->GetTypeId() == PCGExTypes::TTraits<T>::Type)
					{
						return StaticCastSharedPtr<TDataValue<T>>(*ValueTagPtr);
					}
				}
			}

			{
				FWriteScopeLock WriteScopeLock(TagsLock);

				const TSharedPtr<TDataValue<T>> ValueTag = MakeShared<TDataValue<T>>(Value);
				ValueTags.Add(Key, ValueTag);
				return ValueTag;
			}
		}

		template <typename T>
		TSharedPtr<TDataValue<T>> Set(const FString& Key, const T& Value)
		{
			const TSharedPtr<TDataValue<T>> ValueTag = GetOrSet(Key, Value);
			ValueTag->Value = Value;
			return ValueTag;
		}

		template <typename T>
		TSharedPtr<TDataValue<T>> Set(const FString& Key, const TSharedPtr<TDataValue<T>>& Value)
		{
			return Set<T>(Key, Value->Value);
		}

		void Remove(const FString& Key);
		void Remove(const TSet<FString>& InSet);
		void Remove(const TSet<FName>& InSet);

		template <typename T>
		TSharedPtr<TDataValue<T>> GetTypedValue(const FString& Key) const
		{
			FReadScopeLock ReadScopeLock(TagsLock);

			if (const TSharedPtr<IDataValue>* ValueTagPtr = ValueTags.Find(Key))
			{
				if ((*ValueTagPtr)->GetTypeId() == PCGExTypes::TTraits<T>::Type)
				{
					return StaticCastSharedPtr<TDataValue<T>>(*ValueTagPtr);
				}
			}

			return nullptr;
		}

		TSharedPtr<IDataValue> GetValue(const FString& Key) const;

		template <typename T>
		T GetValue(const FString& Key, const T FallbackValue) const
		{
			if (const TSharedPtr<IDataValue> Value = GetValue(Key))
			{
				return Value->GetValue<T>();
			}
			return FallbackValue;
		}

		bool IsTagged(const FString& Key) const;
		bool IsTagged(const FString& Key, const bool bInvert) const;

	protected:
		void ParseAndAdd(const FString& InTag);

		// NAME:VALUE
		static bool GetTagFromString(const FString& Input, FString& OutKey, FString& OutValue);
	};
}
