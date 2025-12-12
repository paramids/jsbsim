// Fill out your copyright notice in the Description page of Project Settings.

#include "JSBSimUDPInputComponent.h"
#include "JSBSimMovementComponent.h"
#include "JSBSimUDPInputModule.h"
#include "FDMTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "Misc/DefaultValueHelper.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

UJSBSimUDPInputComponent::UJSBSimUDPInputComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	
	bIsListening = false;
	bIsSending = false;
	PacketsReceived = 0;
	PacketsSent = 0;
	SendTimer = 0.0f;
	UpdateSendInterval();
	
	ReceiveBuffer.SetNumUninitialized(4096);
}

void UJSBSimUDPInputComponent::BeginPlay()
{
	Super::BeginPlay();

	// Try to find TargetMovementComponent if not set
	if (!TargetMovementComponent)
	{
		TargetMovementComponent = GetOwner()->FindComponentByClass<UJSBSimMovementComponent>();
		if (!TargetMovementComponent)
		{
			UE_LOG(LogJSBSimUDPInput, Warning, TEXT("JSBSimUDPInputComponent: No TargetMovementComponent found. UDP input will not be processed."));
		}
	}

	// Auto-start if enabled
	if (bAutoStart)
	{
		StartUDPListening();
	}
}

void UJSBSimUDPInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopUDPListening();
	Super::EndPlay(EndPlayReason);
}

void UJSBSimUDPInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Receive data
	if (bEnableReceiving && bIsListening && ReceiveSocket)
	{
		uint32 DataSize = 0;
		if (ReceiveSocket->HasPendingData(DataSize))
		{
			TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
			int32 BytesRead = 0;
			
			if (ReceiveSocket->RecvFrom(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), BytesRead, *Sender))
			{
				ReceiveBuffer.SetNum(BytesRead);
				FString SenderAddress = Sender->ToString(false);
				
				PacketsReceived++;
				OnUDPDataReceived.Broadcast(ReceiveBuffer, SenderAddress);
				
				if (DataMappingConfig.bEnableInputMapping)
				{
					ProcessReceivedData(ReceiveBuffer, SenderAddress);
				}
			}
		}
	}

	// Send data
	if (bEnableSending && TargetMovementComponent)
	{
		SendTimer += DeltaTime;
		if (SendTimer >= SendInterval)
		{
			SendTimer = 0.0f;
			SendJSBSimState();
		}
	}

	// Debug visualization
	if (bEnableDebugVisualization)
	{
		DrawDebugVisualization();
	}
}

bool UJSBSimUDPInputComponent::StartUDPListening()
{
	if (bIsListening)
	{
		return true; // Already listening
	}

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogJSBSimUDPInput, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}

	// Create receive socket
	ReceiveSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("JSBSimUDPReceive"), true);
	if (ReceiveSocket)
	{
		ReceiveSocket->SetReuseAddr();
		ReceiveSocket->SetNonBlocking();
		
		// Bind to local port
		TSharedPtr<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr();
		LocalAddr->SetPort(LocalPort);
		LocalAddr->SetAnyAddress();
		
		if (ReceiveSocket->Bind(*LocalAddr))
		{
			bIsListening = true;
			UE_LOG(LogJSBSimUDPInput, Log, TEXT("UDP listening started on port %d"), LocalPort);
			OnUDPConnectionStatusChanged.Broadcast(true);
			return true;
		}
		else
		{
			UE_LOG(LogJSBSimUDPInput, Error, TEXT("Failed to bind receive socket to port %d"), LocalPort);
			SocketSubsystem->DestroySocket(ReceiveSocket);
			ReceiveSocket = nullptr;
		}
	}
	else
	{
		UE_LOG(LogJSBSimUDPInput, Error, TEXT("Failed to create receive socket"));
	}

	return false;
}

void UJSBSimUDPInputComponent::StopUDPListening()
{
	if (ReceiveSocket && SocketSubsystem)
	{
		SocketSubsystem->DestroySocket(ReceiveSocket);
		ReceiveSocket = nullptr;
	}

	if (SendSocket && SocketSubsystem)
	{
		SocketSubsystem->DestroySocket(SendSocket);
		SendSocket = nullptr;
	}

	if (bIsListening)
	{
		bIsListening = false;
		bIsSending = false;
		UE_LOG(LogJSBSimUDPInput, Log, TEXT("UDP listening stopped"));
		OnUDPConnectionStatusChanged.Broadcast(false);
	}
}

bool UJSBSimUDPInputComponent::SendUDPData(const TArray<uint8>& Data)
{
	if (!SocketSubsystem)
	{
		SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return false;
		}
	}

	// Create send socket if needed
	if (!SendSocket)
	{
		SendSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("JSBSimUDPSend"), true);
		if (SendSocket)
		{
			SendSocket->SetReuseAddr();
			SendSocket->SetNonBlocking();
		}
		else
		{
			UE_LOG(LogJSBSimUDPInput, Error, TEXT("Failed to create send socket"));
			return false;
		}
	}

	// Create remote address
	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	
	if (RemoteIP.IsEmpty())
	{
		// Broadcast
		RemoteAddr->SetBroadcastAddress();
		RemoteAddr->SetPort(RemotePort);
		bIsValid = true;
	}
	else
	{
		RemoteAddr->SetIp(*RemoteIP, bIsValid);
		RemoteAddr->SetPort(RemotePort);
	}

	if (!bIsValid)
	{
		UE_LOG(LogJSBSimUDPInput, Error, TEXT("Invalid remote IP address: %s"), *RemoteIP);
		return false;
	}

	// Send data
	int32 BytesSent = 0;
	bool bResult = SendSocket->SendTo(Data.GetData(), Data.Num(), BytesSent, *RemoteAddr);

	if (bResult && BytesSent == Data.Num())
	{
		PacketsSent++;
		FString TargetAddress = RemoteAddr->ToString(false);
		OnUDPDataSent.Broadcast(Data, TargetAddress);
		bIsSending = true;
		return true;
	}
	else
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Failed to send UDP data. Sent: %d/%d bytes"), BytesSent, Data.Num());
		return false;
	}
}

void UJSBSimUDPInputComponent::ProcessUDPData(const TArray<uint8>& Data)
{
	FString SenderAddress = TEXT("Unknown");
	ProcessReceivedData(Data, SenderAddress);
}

void UJSBSimUDPInputComponent::UpdateDataMappingConfig(const FUDPDataMappingConfig& NewConfig)
{
	DataMappingConfig = NewConfig;
	UpdateSendInterval();
}

void UJSBSimUDPInputComponent::ProcessReceivedData(const TArray<uint8>& Data, const FString& SenderAddress)
{
	if (!TargetMovementComponent)
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("ProcessReceivedData: No TargetMovementComponent set"));
		return;
	}

	if (Data.Num() == 0)
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("ProcessReceivedData: Received empty data from %s"), *SenderAddress);
		return;
	}

	// Parse received data
	TMap<FString, FString> ParsedData;
	if (!ParseReceivedData(Data, ParsedData))
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Failed to parse UDP data from %s (Format: %d, Size: %d bytes)"), 
			*SenderAddress, (int32)DataMappingConfig.DataFormat, Data.Num());
		return;
	}

	UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("Parsed %d fields from UDP data from %s"), ParsedData.Num(), *SenderAddress);

	// Apply input mappings
	ApplyInputMappings(ParsedData);
}

void UJSBSimUDPInputComponent::SendJSBSimState()
{
	if (!TargetMovementComponent)
	{
		return;
	}

	if (!DataMappingConfig.bEnableOutputMapping)
	{
		return;
	}

	// Collect output data
	TMap<FString, FString> OutputData = CollectOutputData();

	if (OutputData.Num() == 0)
	{
		UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("No output data to send (no mappings or all disabled)"));
		return;
	}

	// Serialize data
	TArray<uint8> SerializedData;
	if (SerializeOutputData(OutputData, SerializedData))
	{
		if (SerializedData.Num() > 0)
		{
			SendUDPData(SerializedData);
			UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("Sent %d bytes of JSBSim state data (%d fields)"), 
				SerializedData.Num(), OutputData.Num());
		}
		else
		{
			UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Serialization produced empty data"));
		}
	}
	else
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Failed to serialize output data (Format: %d)"), 
			(int32)DataMappingConfig.DataFormat);
	}
}

void UJSBSimUDPInputComponent::ApplyInputMappings(const TMap<FString, FString>& ParsedData)
{
	if (!TargetMovementComponent)
	{
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("ApplyInputMappings: No TargetMovementComponent set"));
		return;
	}

	// Process each input mapping
	for (const FUDPFieldMapping& Mapping : DataMappingConfig.InputMappings)
	{
		if (!Mapping.bEnabled)
		{
			continue;
		}

		// Find the value in parsed data
		const FString* ValuePtr = ParsedData.Find(Mapping.UDPFieldName);
		if (!ValuePtr)
		{
			continue; // Field not found in received data
		}

		FString Value = *ValuePtr;
		double NumericValue = 0.0;
		bool bIsNumeric = FDefaultValueHelper::ParseDouble(Value, NumericValue);

		// Apply based on mapping target
		switch (Mapping.MappingTarget)
		{
		case EUDPMappingTarget::Property:
		{
			// Set JSBSim property directly
			if (!Mapping.JSBSimPropertyPath.IsEmpty())
			{
				FString OutValue;
				TargetMovementComponent->CommandConsole(Mapping.JSBSimPropertyPath, Value, OutValue);
				UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("Set property %s = %s"), *Mapping.JSBSimPropertyPath, *Value);
			}
			break;
		}
		case EUDPMappingTarget::Command:
		{
			// Update command structure directly
			if (!Mapping.CommandMemberName.IsEmpty() && bIsNumeric)
			{
				FString MemberName = Mapping.CommandMemberName;
				
				// Map command member names to FFlightControlCommands structure
				if (MemberName.Equals(TEXT("Aileron"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Aileron = NumericValue;
				}
				else if (MemberName.Equals(TEXT("Elevator"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Elevator = NumericValue;
				}
				else if (MemberName.Equals(TEXT("Rudder"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Rudder = NumericValue;
				}
				else if (MemberName.Equals(TEXT("YawTrim"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.YawTrim = NumericValue;
				}
				else if (MemberName.Equals(TEXT("PitchTrim"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.PitchTrim = NumericValue;
				}
				else if (MemberName.Equals(TEXT("RollTrim"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.RollTrim = NumericValue;
				}
				else if (MemberName.Equals(TEXT("Steer"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Steer = NumericValue;
				}
				else if (MemberName.Equals(TEXT("LeftBrake"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.LeftBrake = NumericValue;
				}
				else if (MemberName.Equals(TEXT("RightBrake"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.RightBrake = NumericValue;
				}
				else if (MemberName.Equals(TEXT("CenterBrake"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.CenterBrake = NumericValue;
				}
				else if (MemberName.Equals(TEXT("ParkingBrake"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.ParkingBrake = NumericValue;
				}
				else if (MemberName.Equals(TEXT("GearDown"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.GearDown = NumericValue;
				}
				else if (MemberName.Equals(TEXT("Flap"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Flap = NumericValue;
				}
				else if (MemberName.Equals(TEXT("SpeedBrake"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.SpeedBrake = NumericValue;
				}
				else if (MemberName.Equals(TEXT("Spoiler"), ESearchCase::IgnoreCase))
				{
					TargetMovementComponent->Commands.Spoiler = NumericValue;
				}
				else
				{
					UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Unknown command member name: %s"), *MemberName);
				}
				
				UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("Set command %s = %f"), *MemberName, NumericValue);
			}
			else if (!bIsNumeric)
			{
				UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Command value is not numeric: %s = %s"), *Mapping.CommandMemberName, *Value);
			}
			break;
		}
		case EUDPMappingTarget::Custom:
		{
			// Custom mapping - could be extended for specific use cases
			UE_LOG(LogJSBSimUDPInput, Verbose, TEXT("Custom mapping for %s = %s"), *Mapping.UDPFieldName, *Value);
			break;
		}
		}
	}
}

TMap<FString, FString> UJSBSimUDPInputComponent::CollectOutputData()
{
	TMap<FString, FString> OutputData;

	if (!TargetMovementComponent)
	{
		return OutputData;
	}

	// Process each output mapping
	for (const FUDPOutputMapping& Mapping : DataMappingConfig.OutputMappings)
	{
		if (!Mapping.bEnabled)
		{
			continue;
		}

		FString Value;

		// Collect based on source type
		switch (Mapping.SourceType)
		{
		case EUDPMappingTarget::Property:
		{
			// Get JSBSim property value
			if (!Mapping.JSBSimPropertyPath.IsEmpty())
			{
				FString OutValue;
				TargetMovementComponent->CommandConsole(Mapping.JSBSimPropertyPath, TEXT(""), OutValue);
				Value = OutValue;
			}
			break;
		}
		case EUDPMappingTarget::Command:
		{
			// Get from aircraft state or command structure
			if (!Mapping.StateMemberName.IsEmpty())
			{
				FString MemberName = Mapping.StateMemberName;
				const FAircraftState& AircraftState = TargetMovementComponent->AircraftState;
				
				// Map state member names to FAircraftState structure
				if (MemberName.Equals(TEXT("CalibratedAirSpeedKts"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.CalibratedAirSpeedKts);
				}
				else if (MemberName.Equals(TEXT("GroundSpeedKts"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.GroundSpeedKts);
				}
				else if (MemberName.Equals(TEXT("TotalVelocityKts"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.TotalVelocityKts);
				}
				else if (MemberName.Equals(TEXT("AltitudeASLFt"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.AltitudeASLFt);
				}
				else if (MemberName.Equals(TEXT("AltitudeAGLFt"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.AltitudeAGLFt);
				}
				else if (MemberName.Equals(TEXT("AltitudeRateFtps"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.AltitudeRateFtps);
				}
				else if (MemberName.Equals(TEXT("Latitude"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.Latitude);
				}
				else if (MemberName.Equals(TEXT("Longitude"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.Longitude);
				}
				else if (MemberName.Equals(TEXT("ElevatorPosition"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.ElevatorPosition);
				}
				else if (MemberName.Equals(TEXT("AileronPosition"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.LeftAileronPosition);
				}
				else if (MemberName.Equals(TEXT("RudderPosition"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.RudderPosition);
				}
				else if (MemberName.Equals(TEXT("FlapPosition"), ESearchCase::IgnoreCase))
				{
					Value = FString::SanitizeFloat(AircraftState.FlapPosition);
				}
				// Try command structure if not found in state
				else
				{
					const FFlightControlCommands& Commands = TargetMovementComponent->Commands;
					if (MemberName.Equals(TEXT("Aileron"), ESearchCase::IgnoreCase))
					{
						Value = FString::SanitizeFloat(Commands.Aileron);
					}
					else if (MemberName.Equals(TEXT("Elevator"), ESearchCase::IgnoreCase))
					{
						Value = FString::SanitizeFloat(Commands.Elevator);
					}
					else if (MemberName.Equals(TEXT("Rudder"), ESearchCase::IgnoreCase))
					{
						Value = FString::SanitizeFloat(Commands.Rudder);
					}
					else if (MemberName.Equals(TEXT("GearDown"), ESearchCase::IgnoreCase))
					{
						Value = FString::SanitizeFloat(Commands.GearDown);
					}
					else if (MemberName.Equals(TEXT("Flap"), ESearchCase::IgnoreCase))
					{
						Value = FString::SanitizeFloat(Commands.Flap);
					}
					else
					{
						// Fallback to JSBSim property lookup
						FString PropertyPath;
						if (MemberName.Equals(TEXT("CalibratedAirSpeedKts"), ESearchCase::IgnoreCase))
						{
							PropertyPath = TEXT("velocities/vc-kts");
						}
						else if (MemberName.Equals(TEXT("AltitudeASLFt"), ESearchCase::IgnoreCase))
						{
							PropertyPath = TEXT("position/altitude-ft");
						}
						else if (MemberName.Equals(TEXT("AltitudeAGLFt"), ESearchCase::IgnoreCase))
						{
							PropertyPath = TEXT("position/agl-ft");
						}
						
						if (!PropertyPath.IsEmpty())
						{
							FString OutValue;
							TargetMovementComponent->CommandConsole(PropertyPath, TEXT(""), OutValue);
							Value = OutValue;
						}
					}
				}
			}
			break;
		}
		case EUDPMappingTarget::Custom:
		{
			// Custom collection - could be extended for specific use cases
			break;
		}
		}

		if (!Value.IsEmpty())
		{
			OutputData.Add(Mapping.UDPFieldName, Value);
		}
	}

	return OutputData;
}

bool UJSBSimUDPInputComponent::ParseReceivedData(const TArray<uint8>& Data, TMap<FString, FString>& OutParsedData)
{
	switch (DataMappingConfig.DataFormat)
	{
	case EUDPDataFormat::JSON:
	{
		// Parse JSON - convert byte array to UTF-8 string
		FString JsonString = FString(UTF8_TO_TCHAR((const char*)Data.GetData()));
		
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			// Extract all key-value pairs
			for (auto& Pair : JsonObject->Values)
			{
				FString ValueStr;
				if (Pair.Value->Type == EJson::String)
				{
					ValueStr = Pair.Value->AsString();
				}
				else if (Pair.Value->Type == EJson::Number)
				{
					ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
				}
				else if (Pair.Value->Type == EJson::Boolean)
				{
					ValueStr = Pair.Value->AsBool() ? TEXT("1") : TEXT("0");
				}
				else if (Pair.Value->Type == EJson::Array || Pair.Value->Type == EJson::Object)
				{
					// Skip complex types for now
					UE_LOG(LogJSBSimUDPInput, VeryVerbose, TEXT("Skipping complex JSON type for key: %s"), *Pair.Key);
					continue;
				}
				else
				{
					ValueStr = Pair.Value->AsString(); // Fallback
				}
				OutParsedData.Add(Pair.Key, ValueStr);
			}
			return OutParsedData.Num() > 0;
		}
		else
		{
			UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Failed to deserialize JSON: %s"), *JsonString.Left(100));
		}
		return false;
	}
	case EUDPDataFormat::Text:
	{
		// Parse text key-value pairs (e.g., "key1=value1\nkey2=value2")
		FString TextString = FString(UTF8_TO_TCHAR((const char*)Data.GetData()));
		
		TArray<FString> Lines;
		TextString.ParseIntoArrayLines(Lines);
		
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (Line.Split(TEXT("="), &Key, &Value))
			{
				Key.TrimStartAndEndInline();
				Value.TrimStartAndEndInline();
				OutParsedData.Add(Key, Value);
			}
		}
		return OutParsedData.Num() > 0;
	}
	case EUDPDataFormat::Binary:
	{
		// Binary format - for now, we'll assume a simple key-value structure
		// This is a placeholder - you may want to implement a specific binary protocol
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Binary format parsing not fully implemented"));
		return false;
	}
	default:
		return false;
	}
}

bool UJSBSimUDPInputComponent::SerializeOutputData(const TMap<FString, FString>& Data, TArray<uint8>& OutSerializedData)
{
	switch (DataMappingConfig.DataFormat)
	{
	case EUDPDataFormat::JSON:
	{
		// Create JSON object
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		
		for (const auto& Pair : Data)
		{
			// Try to parse as number, otherwise keep as string
			double NumberValue;
			if (FDefaultValueHelper::ParseDouble(Pair.Value, NumberValue))
			{
				JsonObject->SetNumberField(Pair.Key, NumberValue);
			}
			else
			{
				JsonObject->SetStringField(Pair.Key, Pair.Value);
			}
		}
		
		// Serialize to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
		
		// Convert to bytes
		FTCHARToUTF8 UTF8String(*OutputString);
		OutSerializedData.SetNumUninitialized(UTF8String.Length());
		FMemory::Memcpy(OutSerializedData.GetData(), UTF8String.Get(), UTF8String.Length());
		
		return true;
	}
	case EUDPDataFormat::Text:
	{
		// Serialize as text key-value pairs
		FString OutputString;
		for (const auto& Pair : Data)
		{
			OutputString += FString::Printf(TEXT("%s=%s\n"), *Pair.Key, *Pair.Value);
		}
		
		// Convert to bytes
		FTCHARToUTF8 UTF8String(*OutputString);
		OutSerializedData.SetNumUninitialized(UTF8String.Length());
		FMemory::Memcpy(OutSerializedData.GetData(), UTF8String.Get(), UTF8String.Length());
		
		return true;
	}
	case EUDPDataFormat::Binary:
	{
		// Binary format - placeholder implementation
		UE_LOG(LogJSBSimUDPInput, Warning, TEXT("Binary format serialization not fully implemented"));
		return false;
	}
	default:
		return false;
	}
}

void UJSBSimUDPInputComponent::UpdateSendInterval()
{
	if (SendRateHz > 0.0f)
	{
		SendInterval = 1.0f / SendRateHz;
	}
	else
	{
		SendInterval = 0.0f; // Send every tick
	}
}

void UJSBSimUDPInputComponent::DrawDebugVisualization()
{
	if (!GetWorld() || !GEngine)
	{
		return;
	}

	FString DebugText;
	DebugText += FString::Printf(TEXT("=== JSBSim UDP Input ===\n"));
	DebugText += FString::Printf(TEXT("Listening: %s (Port: %d)\n"), bIsListening ? TEXT("Yes") : TEXT("No"), LocalPort);
	DebugText += FString::Printf(TEXT("Sending: %s (To: %s:%d)\n"), bIsSending ? TEXT("Yes") : TEXT("No"), *RemoteIP, RemotePort);
	DebugText += FString::Printf(TEXT("Packets Received: %d\n"), PacketsReceived);
	DebugText += FString::Printf(TEXT("Packets Sent: %d\n"), PacketsSent);
	DebugText += FString::Printf(TEXT("Send Rate: %.1f Hz\n"), SendRateHz);
	DebugText += FString::Printf(TEXT("Data Format: %s\n"), 
		DataMappingConfig.DataFormat == EUDPDataFormat::JSON ? TEXT("JSON") :
		DataMappingConfig.DataFormat == EUDPDataFormat::Text ? TEXT("Text") : TEXT("Binary"));
	DebugText += FString::Printf(TEXT("Input Mappings: %d (Enabled: %d)\n"), 
		DataMappingConfig.InputMappings.Num(),
		DataMappingConfig.InputMappings.FilterByPredicate([](const FUDPFieldMapping& M) { return M.bEnabled; }).Num());
	DebugText += FString::Printf(TEXT("Output Mappings: %d (Enabled: %d)\n"), 
		DataMappingConfig.OutputMappings.Num(),
		DataMappingConfig.OutputMappings.FilterByPredicate([](const FUDPOutputMapping& M) { return M.bEnabled; }).Num());
	
	if (!TargetMovementComponent)
	{
		DebugText += TEXT("\nWARNING: No TargetMovementComponent set!\n");
	}

	GEngine->AddOnScreenDebugMessage(
		(uint64)this,
		0.0f,
		FColor::Cyan,
		DebugText,
		false
	);
}

