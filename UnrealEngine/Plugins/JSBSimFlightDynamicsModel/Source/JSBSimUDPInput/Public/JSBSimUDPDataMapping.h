// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JSBSimUDPDataMapping.generated.h"

/**
 * Enumeration for data format types supported by UDP input
 */
UENUM(BlueprintType)
enum class EUDPDataFormat : uint8
{
	Binary		UMETA(DisplayName = "Binary"),
	JSON		UMETA(DisplayName = "JSON"),
	Text		UMETA(DisplayName = "Text Key-Value")
};

/**
 * Enumeration for mapping target types
 */
UENUM(BlueprintType)
enum class EUDPMappingTarget : uint8
{
	Property		UMETA(DisplayName = "JSBSim Property"),
	Command		UMETA(DisplayName = "Command Structure"),
	Custom		UMETA(DisplayName = "Custom")
};

/**
 * Structure defining a single UDP field to JSBSim mapping
 */
USTRUCT(BlueprintType)
struct JSBSIMUDPINPUT_API FUDPFieldMapping
{
	GENERATED_BODY()

	FUDPFieldMapping()
		: UDPFieldName(TEXT(""))
		, MappingTarget(EUDPMappingTarget::Property)
		, JSBSimPropertyPath(TEXT(""))
		, CommandMemberName(TEXT(""))
		, bEnabled(true)
	{
	}

	/** Name of the field in the UDP packet */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mapping")
	FString UDPFieldName;

	/** Type of target for this mapping */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mapping")
	EUDPMappingTarget MappingTarget;

	/** JSBSim property path (e.g., "propulsion/engine/throttle") - used when MappingTarget is Property */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mapping", meta = (EditCondition = "MappingTarget == EUDPMappingTarget::Property"))
	FString JSBSimPropertyPath;

	/** Command structure member name (e.g., "Aileron", "Throttle") - used when MappingTarget is Command */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mapping", meta = (EditCondition = "MappingTarget == EUDPMappingTarget::Command"))
	FString CommandMemberName;

	/** Whether this mapping is currently enabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mapping")
	bool bEnabled;
};

/**
 * Structure defining output mapping (JSBSim property/state to UDP field)
 */
USTRUCT(BlueprintType)
struct JSBSIMUDPINPUT_API FUDPOutputMapping
{
	GENERATED_BODY()

	FUDPOutputMapping()
		: UDPFieldName(TEXT(""))
		, SourceType(EUDPMappingTarget::Property)
		, JSBSimPropertyPath(TEXT(""))
		, StateMemberName(TEXT(""))
		, bEnabled(true)
	{
	}

	/** Name of the field in the output UDP packet */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output Mapping")
	FString UDPFieldName;

	/** Type of source for this mapping */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output Mapping")
	EUDPMappingTarget SourceType;

	/** JSBSim property path (e.g., "velocities/vt-fps") - used when SourceType is Property */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output Mapping", meta = (EditCondition = "SourceType == EUDPMappingTarget::Property"))
	FString JSBSimPropertyPath;

	/** Aircraft state member name (e.g., "CalibratedAirSpeedKts", "AltitudeASLFt") - used when SourceType is Command */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output Mapping", meta = (EditCondition = "SourceType == EUDPMappingTarget::Command"))
	FString StateMemberName;

	/** Whether this mapping is currently enabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output Mapping")
	bool bEnabled;
};

/**
 * Configuration structure for UDP data mapping
 */
USTRUCT(BlueprintType)
struct JSBSIMUDPINPUT_API FUDPDataMappingConfig
{
	GENERATED_BODY()

	FUDPDataMappingConfig()
		: DataFormat(EUDPDataFormat::JSON)
		, bEnableInputMapping(true)
		, bEnableOutputMapping(true)
	{
	}

	/** Data format for UDP packets */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Configuration")
	EUDPDataFormat DataFormat;

	/** Input field mappings (UDP field → JSBSim) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Configuration")
	TArray<FUDPFieldMapping> InputMappings;

	/** Output field mappings (JSBSim → UDP field) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Configuration")
	TArray<FUDPOutputMapping> OutputMappings;

	/** Whether input mapping is enabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Configuration")
	bool bEnableInputMapping;

	/** Whether output mapping is enabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Configuration")
	bool bEnableOutputMapping;
};

