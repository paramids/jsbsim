// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "JSBSimUDPDataMapping.h"
#include "JSBSimUDPInputComponent.generated.h"

// Forward declarations
class UJSBSimMovementComponent;

/**
 * Delegate fired when UDP data is received
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUDPDataReceived, const TArray<uint8>&, Data, const FString&, SenderAddress);

/**
 * Delegate fired when UDP data is sent
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUDPDataSent, const TArray<uint8>&, Data, const FString&, TargetAddress);

/**
 * Delegate fired when UDP connection status changes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUDPConnectionStatusChanged, bool, bIsConnected);

/**
 * Component for bidirectional UDP communication with JSBSim
 * Receives UDP data and forwards to JSBSim, sends JSBSim state via UDP
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class JSBSIMUDPINPUT_API UJSBSimUDPInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJSBSimUDPInputComponent(const FObjectInitializer& ObjectInitializer);

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// UDP Configuration
	/** Target JSBSim Movement Component to send data to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	TObjectPtr<UJSBSimMovementComponent> TargetMovementComponent;

	/** Local port to receive UDP packets on */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 LocalPort = 8889;

	/** Remote IP address to send UDP packets to (empty = broadcast) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	FString RemoteIP = TEXT("127.0.0.1");

	/** Remote port to send UDP packets to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 RemotePort = 8890;

	/** Automatically start listening on BeginPlay */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	bool bAutoStart = true;

	/** Enable receiving UDP data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	bool bEnableReceiving = true;

	/** Enable sending UDP data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	bool bEnableSending = true;

	/** Send rate in Hz (0 = every tick) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration", meta = (ClampMin = "0", ClampMax = "1000"))
	float SendRateHz = 60.0f;

	/** Enable debug visualization (on-screen text) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	bool bEnableDebugVisualization = false;

	/** Data mapping configuration */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UDP Configuration")
	FUDPDataMappingConfig DataMappingConfig;

	// Status
	/** Whether UDP is currently listening */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "UDP Status")
	bool bIsListening = false;

	/** Whether UDP is currently sending */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "UDP Status")
	bool bIsSending = false;

	/** Number of packets received */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "UDP Status")
	int32 PacketsReceived = 0;

	/** Number of packets sent */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "UDP Status")
	int32 PacketsSent = 0;

	// Events
	/** Event fired when UDP data is received */
	UPROPERTY(BlueprintAssignable, Category = "UDP Events")
	FOnUDPDataReceived OnUDPDataReceived;

	/** Event fired when UDP data is sent */
	UPROPERTY(BlueprintAssignable, Category = "UDP Events")
	FOnUDPDataSent OnUDPDataSent;

	/** Event fired when connection status changes */
	UPROPERTY(BlueprintAssignable, Category = "UDP Events")
	FOnUDPConnectionStatusChanged OnUDPConnectionStatusChanged;

	// Functions
	/** Start UDP listening */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	bool StartUDPListening();

	/** Stop UDP listening */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	void StopUDPListening();

	/** Send data via UDP */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	bool SendUDPData(const TArray<uint8>& Data);

	/** Process received UDP data and forward to JSBSim */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	void ProcessUDPData(const TArray<uint8>& Data);

	/** Update data mapping configuration */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	void UpdateDataMappingConfig(const FUDPDataMappingConfig& NewConfig);

	/** Get current connection status */
	UFUNCTION(BlueprintCallable, Category = "UDP")
	bool GetIsConnected() const { return bIsListening; }

protected:
	/** UDP Socket for receiving */
	FSocket* ReceiveSocket = nullptr;

	/** UDP Socket for sending */
	FSocket* SendSocket = nullptr;

	/** Socket subsystem */
	ISocketSubsystem* SocketSubsystem = nullptr;

	/** Timer for periodic sending */
	float SendTimer = 0.0f;
	float SendInterval = 0.0f;

	/** Buffer for receiving data */
	TArray<uint8> ReceiveBuffer;

	/** Process received data and apply mappings */
	void ProcessReceivedData(const TArray<uint8>& Data, const FString& SenderAddress);

	/** Collect JSBSim state and send via UDP */
	void SendJSBSimState();

	/** Apply input mappings to JSBSim */
	void ApplyInputMappings(const TMap<FString, FString>& ParsedData);

	/** Collect output data from JSBSim */
	TMap<FString, FString> CollectOutputData();

	/** Parse received data based on format */
	bool ParseReceivedData(const TArray<uint8>& Data, TMap<FString, FString>& OutParsedData);

	/** Serialize output data based on format */
	bool SerializeOutputData(const TMap<FString, FString>& Data, TArray<uint8>& OutSerializedData);

	/** Update send interval based on rate */
	void UpdateSendInterval();

	/** Draw debug visualization on screen */
	void DrawDebugVisualization();
};

