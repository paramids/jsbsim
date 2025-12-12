// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JSBSimSimulationState.generated.h"

/**
 * Enumeration for simulation finite state machine states
 */
UENUM(BlueprintType)
enum class EJSBSimSimulationState : uint8
{
	Uninitialized	UMETA(DisplayName = "Uninitialized"),
	Initializing	UMETA(DisplayName = "Initializing"),
	Loading			UMETA(DisplayName = "Loading"),
	Preparing		UMETA(DisplayName = "Preparing"),
	Ready			UMETA(DisplayName = "Ready"),
	Running			UMETA(DisplayName = "Running"),
	Paused			UMETA(DisplayName = "Paused"),
	Stopping		UMETA(DisplayName = "Stopping"),
	Error			UMETA(DisplayName = "Error")
};

/**
 * Structure containing state transition information
 */
USTRUCT(BlueprintType)
struct JSBSIMSIMULATIONFSM_API FJSBSimStateTransition
{
	GENERATED_BODY()

	FJSBSimStateTransition()
		: FromState(EJSBSimSimulationState::Uninitialized)
		, ToState(EJSBSimSimulationState::Uninitialized)
		, Timestamp(0.0)
	{
	}

	/** Previous state */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	EJSBSimSimulationState FromState;

	/** New state */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	EJSBSimSimulationState ToState;

	/** Timestamp of transition (game time) */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	double Timestamp;
};

