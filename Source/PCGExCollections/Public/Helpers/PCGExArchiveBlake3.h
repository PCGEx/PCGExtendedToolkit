// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Hash/Blake3.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Object.h"

/**
 * Process-stable Blake3 hash archive.
 *
 * Mirrors FArchiveCrc32's stringification semantics for FName and UObject paths so the
 * hashed bytes -- and therefore the resulting digest -- are identical across editor
 * sessions, cooker runs, and machines for identical inputs. Produces a 256-bit Blake3
 * digest (collision probability ~2^-128) instead of FArchiveCrc32's 32-bit CRC.
 *
 * MUST inherit from FArchiveUObject (not FArchive directly): USTRUCT walks via
 * UScriptStruct::SerializeBin reach FSoftObjectPtr / FObjectPtr / FSoftObjectPath
 * fields through their respective operator<< overloads. FArchive's defaults are
 * no-op stubs that crash when descriptor structs are serialized through them;
 * FArchiveUObject routes them through SerializeSoftObjectPath / SerializeSoftObjectPtr
 * / etc., which in turn call back into our process-stable FName operator.
 *
 * Usage:
 *   FArchiveBlake3 Hasher;
 *   FMyStruct::StaticStruct()->SerializeBin(Hasher, &MyValue);
 *   const FBlake3Hash Digest = Hasher.Finalize();
 *
 * Header-only -- all methods inline so the wrapper imposes no link-time cost.
 */
class FArchiveBlake3 : public FArchiveUObject
{
public:
	FArchiveBlake3()
	{
		this->SetIsSaving(true);
		this->SetIsPersistent(false);
	}

	//~ Begin FArchive Interface
	virtual void Serialize(void* Data, int64 Length) override
	{
		if (Length > 0)
		{
			Hasher.Update(Data, static_cast<uint64>(Length));
		}
	}

	virtual FArchive& operator<<(FName& Name) override
	{
		FString NameAsString = Name.ToString();
		NameAsString.ToLowerInline();
		Serialize(GetData(NameAsString), sizeof(TCHAR) * NameAsString.Len());
		return *this;
	}

	virtual FArchive& operator<<(UObject*& Object) override
	{
		FString Path = Object ? Object->GetPathName() : FString();
		Serialize(GetData(Path), sizeof(TCHAR) * Path.Len());
		return *this;
	}

	virtual FString GetArchiveName() const override
	{
		return TEXT("FArchiveBlake3");
	}

	using FArchiveUObject::operator<<; // bring in FSoftObjectPtr / FObjectPtr / etc. overloads
	//~ End FArchive Interface

	/** Finalize and return the 256-bit Blake3 digest. May be called repeatedly; more input may follow. */
	FBlake3Hash Finalize() const
	{
		return Hasher.Finalize();
	}

private:
	FBlake3 Hasher;
};
