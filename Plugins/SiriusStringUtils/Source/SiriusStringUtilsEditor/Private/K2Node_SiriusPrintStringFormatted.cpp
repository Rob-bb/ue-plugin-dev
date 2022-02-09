// Copyright 2022-2022 Jasper de Laat. All Rights Reserved.

#include "K2Node_SiriusPrintStringFormatted.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "K2Node_SiriusPrintStringFormatted"

UK2Node_SiriusPrintStringFormatted::UK2Node_SiriusPrintStringFormatted()
{
	NodeTooltip = LOCTEXT("NodeTooltip", "Prints a formatted string to the log, and optionally, to the screen.\n If Print To Log is true, it will be visible in the Output Log window. Otherwise it will be logged only as 'Verbose', so it generally won't show up.");

	// Show the development only banner to warn the user they're not going to get the benefits of this node in a shipping build
	SetEnabledState(ENodeEnabledState::DevelopmentOnly, false);
}

void UK2Node_SiriusPrintStringFormatted::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	if (AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	}

	const UEdGraphSchema_K2* DefaultSchema = GetDefault<UEdGraphSchema_K2>();

	// Execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Format pins
	CachedFormatPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, TEXT("In String"));
	DefaultSchema->SetPinAutogeneratedDefaultValue(CachedFormatPin, TEXT("Hello"));
	for (const FName& PinName : PinNames)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, PinName);
	}

	// Logging option pins
	UEdGraphPin* PrintScreenPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Print to Screen"));
	PrintScreenPin->bAdvancedView = true;
	DefaultSchema->SetPinAutogeneratedDefaultValue(PrintScreenPin, TEXT("true"));
	UEdGraphPin* PrintLogPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Print to Log"));
	PrintLogPin->bAdvancedView = true;
	DefaultSchema->SetPinAutogeneratedDefaultValue(PrintLogPin, TEXT("true"));

	UScriptStruct* LinearColorScriptStruct = FindObjectChecked<UScriptStruct>(ANY_PACKAGE, TEXT("LinearColor"));
	UEdGraphPin* TextColorPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, LinearColorScriptStruct, TEXT("Text Color"));
	TextColorPin->bAdvancedView = true;
	DefaultSchema->SetPinAutogeneratedDefaultValue(TextColorPin, FLinearColor(0.0f, 0.66f, 1.0f).ToString());

	UEdGraphPin* DurationPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Duration"));
	DurationPin->bAdvancedView = true;
	DefaultSchema->SetPinAutogeneratedDefaultValue(DurationPin, TEXT("2.0"));
}

FText UK2Node_SiriusPrintStringFormatted::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Print String Formatted (Sirius String Utils)");
}

FText UK2Node_SiriusPrintStringFormatted::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (Pin != FindPin(UEdGraphSchema_K2::PN_Execute) && Pin != FindPin(UEdGraphSchema_K2::PN_Then))
	{
		return FText::FromName(Pin->PinName);
	}

	return FText::GetEmpty();
}

FText UK2Node_SiriusPrintStringFormatted::GetTooltipText() const
{
	return NodeTooltip;
}

void UK2Node_SiriusPrintStringFormatted::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Modify();

	// Potentially update an argument pin type
	SynchronizeArgumentPinType(Pin);
}

void UK2Node_SiriusPrintStringFormatted::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	// Detect if the format pin has changed.
	const UEdGraphPin* FormatPin = GetFormatPin();
	if (Pin == FormatPin && FormatPin->LinkedTo.Num() == 0)
	{
		TArray<FString> ArgumentParams;
		FTextFormat::FromString(FormatPin->DefaultValue).GetFormatArgumentNames(ArgumentParams);

		PinNames.Reset();

		// Create argument pins if new arguments were created.
		for (const FString& Param : ArgumentParams)
		{
			const FName ParamName(*Param);
			if (!FindArgumentPin(ParamName))
			{
				CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, ParamName);
			}
			PinNames.Add(ParamName);
		}

		// Destroy argument pins whose arguments were destroyed.
		for (auto It = Pins.CreateIterator(); It; ++It)
		{
			UEdGraphPin* CheckPin = *It;
			if (CheckPin != FormatPin && CheckPin->Direction == EGPD_Input)
			{
				const bool bIsValidArgPin = ArgumentParams.ContainsByPredicate([&CheckPin](const FString& InPinName)
				{
					return InPinName.Equals(CheckPin->PinName.ToString(), ESearchCase::CaseSensitive);
				});

				if (!bIsValidArgPin)
				{
					CheckPin->MarkPendingKill();
					It.RemoveCurrent();
				}
			}
		}

		// Notify graph that something changed.
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_SiriusPrintStringFormatted::PinTypeChanged(UEdGraphPin* Pin)
{
	// Potentially update an argument pin type
	SynchronizeArgumentPinType(Pin);

	Super::PinTypeChanged(Pin);
}

UK2Node::ERedirectType UK2Node_SiriusPrintStringFormatted::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	ERedirectType RedirectType = ERedirectType_None;

	// if the pin names do match
	if (NewPin->PinName.ToString().Equals(OldPin->PinName.ToString(), ESearchCase::CaseSensitive))
	{
		// Make sure we're not dealing with a menu node
		if (const UEdGraphSchema* Schema = GetSchema())
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(Schema);
			if (!K2Schema || K2Schema->IsSelfPin(*NewPin) || K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType))
			{
				RedirectType = ERedirectType_Name;
			}
			else
			{
				RedirectType = ERedirectType_None;
			}
		}
	}
	else
	{
		// try looking for a redirect if it's a K2 node
		if (const UK2Node* Node = Cast<UK2Node>(NewPin->GetOwningNode()))
		{
			// if you don't have matching pin, now check if there is any redirect param set
			TArray<FString> OldPinNames;
			GetRedirectPinNames(*OldPin, OldPinNames);

			FName NewPinName;
			RedirectType = ShouldRedirectParam(OldPinNames, /*out*/ NewPinName, Node);

			// make sure they match
			if (RedirectType != ERedirectType_None && !NewPin->PinName.ToString().Equals(NewPinName.ToString(), ESearchCase::CaseSensitive))
			{
				RedirectType = ERedirectType_None;
			}
		}
	}

	return RedirectType;
}

void UK2Node_SiriusPrintStringFormatted::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	// TODO (Jasper): Implement node expansion logic.

	// Final step, break all links to this node as we've finished expanding it.
	BreakAllNodeLinks();
}

void UK2Node_SiriusPrintStringFormatted::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	const UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_SiriusPrintStringFormatted::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::String);
}

bool UK2Node_SiriusPrintStringFormatted::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	const UEdGraphPin* FormatPin = GetFormatPin();
	if (MyPin != FormatPin && MyPin->Direction == EGPD_Input)
	{
		const FName& OtherPinCategory = OtherPin->PinType.PinCategory;

		bool bIsValidType = false;
		if (OtherPinCategory == UEdGraphSchema_K2::PC_Int ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Int64 ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Float ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Text ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Byte ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Boolean ||
			OtherPinCategory == UEdGraphSchema_K2::PC_String ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Name ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Object ||
			OtherPinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			bIsValidType = true;
		}

		if (!bIsValidType)
		{
			OutReason = LOCTEXT("Error_InvalidArgumentType", "Format arguments may only be Byte, Enum, Integer, Float, Text, String, Name, Boolean, Object or Wildcard.").ToString();
			return true;
		}
	}

	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

void UK2Node_SiriusPrintStringFormatted::PostReconstructNode()
{
	Super::PostReconstructNode();

	if (!IsTemplate())
	{
		// Make sure we're not dealing with a menu node
		if (GetSchema())
		{
			for (UEdGraphPin* CurrentPin : Pins)
			{
				// Potentially update an argument pin type
				SynchronizeArgumentPinType(CurrentPin);
			}
		}
	}
}

UEdGraphPin* UK2Node_SiriusPrintStringFormatted::GetFormatPin() const
{
	if (!CachedFormatPin)
	{
		const_cast<UK2Node_SiriusPrintStringFormatted*>(this)->CachedFormatPin = FindPinChecked(TEXT("In String"));
	}
	return CachedFormatPin;
}

UEdGraphPin* UK2Node_SiriusPrintStringFormatted::FindArgumentPin(const FName InPinName) const
{
	const UEdGraphPin* FormatPin = GetFormatPin();
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin != FormatPin && Pin->Direction != EGPD_Output && Pin->PinName.ToString().Equals(InPinName.ToString(), ESearchCase::CaseSensitive))
		{
			return Pin;
		}
	}

	return nullptr;
}

void UK2Node_SiriusPrintStringFormatted::SynchronizeArgumentPinType(UEdGraphPin* Pin) const
{
	const UEdGraphPin* FormatPin = GetFormatPin();
	if (Pin != FormatPin && Pin->Direction == EGPD_Input)
	{
		bool bPinTypeChanged = false;
		if (Pin->LinkedTo.Num() == 0)
		{
			static const FEdGraphPinType WildcardPinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Wildcard, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());

			// Ensure wildcard
			if (Pin->PinType != WildcardPinType)
			{
				Pin->PinType = WildcardPinType;
				bPinTypeChanged = true;
			}
		}
		else
		{
			const UEdGraphPin* ArgumentSourcePin = Pin->LinkedTo[0];

			// Take the type of the connected pin
			if (Pin->PinType != ArgumentSourcePin->PinType)
			{
				Pin->PinType = ArgumentSourcePin->PinType;
				bPinTypeChanged = true;
			}
		}

		if (bPinTypeChanged)
		{
			// Let the graph know to refresh
			GetGraph()->NotifyGraphChanged();

			UBlueprint* Blueprint = GetBlueprint();
			if (!Blueprint->bBeingCompiled)
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
				Blueprint->BroadcastChanged();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
