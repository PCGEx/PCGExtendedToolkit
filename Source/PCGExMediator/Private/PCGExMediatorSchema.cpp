// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedRef<FJsonObject> PCGExMediator::Schema::Typed(const TCHAR* Type, const FString& Description)
{
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	if (Type) { S->SetStringField(TEXT("type"), Type); }
	if (!Description.IsEmpty()) { S->SetStringField(TEXT("description"), Description); }
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::String(const FString& Description)
{
	return Typed(TEXT("string"), Description);
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Number(const FString& Description)
{
	return Typed(TEXT("number"), Description);
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Integer(const FString& Description)
{
	return Typed(TEXT("integer"), Description);
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Boolean(const FString& Description)
{
	return Typed(TEXT("boolean"), Description);
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Array(const TSharedPtr<FJsonObject>& Items, const FString& Description, const int32 MinItems, const int32 MaxItems)
{
	TSharedRef<FJsonObject> S = Typed(TEXT("array"), Description);
	if (Items.IsValid()) { S->SetObjectField(TEXT("items"), Items.ToSharedRef()); }
	if (MinItems >= 0) { S->SetNumberField(TEXT("minItems"), MinItems); }
	if (MaxItems >= 0) { S->SetNumberField(TEXT("maxItems"), MaxItems); }
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Object(const TSharedRef<FJsonObject>& Properties, const FString& Description, const TConstArrayView<FString> Required)
{
	TSharedRef<FJsonObject> S = Typed(TEXT("object"), Description);
	S->SetObjectField(TEXT("properties"), Properties);
	if (!Required.IsEmpty()) { S->SetArrayField(TEXT("required"), Strings(TArray<FString>(Required))); }
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Const(const FString& Value)
{
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetStringField(TEXT("const"), Value);
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Const(const int32 Value)
{
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetNumberField(TEXT("const"), Value);
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Enum(const TArray<FString>& Names, const FString& Description)
{
	TSharedRef<FJsonObject> S = String(Description);
	S->SetArrayField(TEXT("enum"), Strings(Names));
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::OneOf(const TArray<TSharedPtr<FJsonObject>>& Options, const FString& Description)
{
	TSharedRef<FJsonObject> S = Typed(nullptr, Description);
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Options.Num());
	for (const TSharedPtr<FJsonObject>& Option : Options)
	{
		if (Option.IsValid()) { Values.Add(MakeShared<FJsonValueObject>(Option)); }
	}
	S->SetArrayField(TEXT("oneOf"), Values);
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Describe(const TSharedRef<FJsonObject>& S, const FString& Description)
{
	S->SetStringField(TEXT("description"), Description);
	return S;
}

TSharedRef<FJsonObject> PCGExMediator::Schema::Comment(const TSharedRef<FJsonObject>& S, const FString& Comment)
{
	S->SetStringField(TEXT("$comment"), Comment);
	return S;
}

TArray<TSharedPtr<FJsonValue>> PCGExMediator::Schema::Strings(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Out.Add(MakeShared<FJsonValueString>(Value));
	}
	return Out;
}
