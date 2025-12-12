// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JSBSimSimulationState.h"
#include "JSBSimSimulationFSMComponent.generated.h"

// Forward declarations
class UJSBSimMovementComponent;

/**
 * Delegate fired when state changes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStateChanged, EJSBSimSimulationState, NewState, EJSBSimSimulationState, PreviousState);

/**
 * Delegate fired when entering a specific state
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStateEntered, EJSBSimSimulationState, State);

/**
 * Delegate fired when exiting a specific state
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStateExited, EJSBSimSimulationState, State);

/**
 * Component for managing JSBSim simulation finite state machine
 * Monitors JSBSimMovementComponent lifecycle and provides state management
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class JSBSIMSIMULATIONFSM_API UJSBSimSimulationFSMComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJSBSimSimulationFSMComponent(const FObjectInitializer& ObjectInitializer);

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Configuration
	/** Target JSBSim Movement Component to monitor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FSM Configuration")
	TObjectPtr<UJSBSimMovementComponent> TargetMovementComponent;

	/** Automatically monitor JSBSimMovementComponent lifecycle */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FSM Configuration")
	bool bAutoMonitorMovementComponent = true;

	/** Allow manual state transitions */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FSM Configuration")
	bool bAllowManualTransitions = true;

	/** Enable logging of state transitions */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FSM Configuration")
	bool bLogStateTransitions = true;

	/** Current simulation state */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "FSM State")
	EJSBSimSimulationState CurrentState = EJSBSimSimulationState::Uninitialized;

	/** State transition history */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "FSM State")
	TArray<FJSBSimStateTransition> StateHistory;

	/** Maximum number of state transitions to keep in history */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FSM Configuration", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MaxHistorySize = 100;

	// Events
	/** Event fired when state changes */
	UPROPERTY(BlueprintAssignable, Category = "FSM Events")
	FOnStateChanged OnStateChanged;

	/** Event fired when entering a specific state */
	UPROPERTY(BlueprintAssignable, Category = "FSM Events")
	FOnStateEntered OnStateEntered;

	/** Event fired when exiting a specific state */
	UPROPERTY(BlueprintAssignable, Category = "FSM Events")
	FOnStateExited OnStateExited;

	// Functions
	/** Get current simulation state */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	EJSBSimSimulationState GetCurrentState() const { return CurrentState; }

	/** Request a state transition (may be rejected if invalid) */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	bool RequestStateTransition(EJSBSimSimulationState NewState);

	/** Pause the simulation (transition to Paused state) */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	bool PauseSimulation();

	/** Resume the simulation (transition from Paused to Running) */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	bool ResumeSimulation();

	/** Reset simulation to Uninitialized state */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	bool ResetSimulation();

	/** Manually trigger state monitoring (useful if auto-monitoring is disabled) */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	void UpdateStateFromMovementComponent();

	/** Get the last state transition */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	FJSBSimStateTransition GetLastTransition() const;

	/** Check if a state transition is valid */
	UFUNCTION(BlueprintCallable, Category = "FSM")
	bool IsValidTransition(EJSBSimSimulationState FromState, EJSBSimSimulationState ToState) const;

protected:
	/** Internal state transition with validation */
	bool TransitionToState(EJSBSimSimulationState NewState, bool bForce = false);

	/** Monitor JSBSimMovementComponent and update FSM state accordingly */
	void MonitorMovementComponent();

	/** Check if JSBSimMovementComponent indicates an error condition */
	bool CheckForErrors() const;

	/** Get state name as string for logging */
	FString GetStateName(EJSBSimSimulationState State) const;

private:
	/** Previous state (for transition tracking) */
	EJSBSimSimulationState PreviousState = EJSBSimSimulationState::Uninitialized;

	/** Whether the component is currently paused */
	bool bIsPaused = false;

	/** Error message if in Error state */
	FString ErrorMessage;
};

