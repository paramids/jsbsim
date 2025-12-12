// Fill out your copyright notice in the Description page of Project Settings.

#include "JSBSimSimulationFSMComponent.h"
#include "JSBSimMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UJSBSimSimulationFSMComponent::UJSBSimSimulationFSMComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	
	CurrentState = EJSBSimSimulationState::Uninitialized;
	PreviousState = EJSBSimSimulationState::Uninitialized;
	bIsPaused = false;
}

void UJSBSimSimulationFSMComponent::BeginPlay()
{
	Super::BeginPlay();

	// Try to find TargetMovementComponent if not set
	if (!TargetMovementComponent)
	{
		TargetMovementComponent = GetOwner()->FindComponentByClass<UJSBSimMovementComponent>();
		if (!TargetMovementComponent)
		{
			UE_LOG(LogJSBSimSimulationFSM, Warning, TEXT("JSBSimSimulationFSMComponent: No TargetMovementComponent found. Auto-monitoring will be disabled."));
			bAutoMonitorMovementComponent = false;
		}
	}

	// Initialize state based on current movement component state
	if (bAutoMonitorMovementComponent && TargetMovementComponent)
	{
		UpdateStateFromMovementComponent();
	}
}

void UJSBSimSimulationFSMComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Transition to Stopping state on shutdown
	if (CurrentState != EJSBSimSimulationState::Stopping)
	{
		TransitionToState(EJSBSimSimulationState::Stopping, true);
	}

	Super::EndPlay(EndPlayReason);
}

void UJSBSimSimulationFSMComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoMonitorMovementComponent && TargetMovementComponent)
	{
		MonitorMovementComponent();
	}
}

bool UJSBSimSimulationFSMComponent::RequestStateTransition(EJSBSimSimulationState NewState)
{
	if (!bAllowManualTransitions)
	{
		UE_LOG(LogJSBSimSimulationFSM, Warning, TEXT("Manual state transitions are disabled."));
		return false;
	}

	return TransitionToState(NewState, false);
}

bool UJSBSimSimulationFSMComponent::PauseSimulation()
{
	if (CurrentState == EJSBSimSimulationState::Running)
	{
		bIsPaused = true;
		return TransitionToState(EJSBSimSimulationState::Paused, false);
	}
	return false;
}

bool UJSBSimSimulationFSMComponent::ResumeSimulation()
{
	if (CurrentState == EJSBSimSimulationState::Paused)
	{
		bIsPaused = false;
		return TransitionToState(EJSBSimSimulationState::Running, false);
	}
	return false;
}

bool UJSBSimSimulationFSMComponent::ResetSimulation()
{
	return TransitionToState(EJSBSimSimulationState::Uninitialized, true);
}

void UJSBSimSimulationFSMComponent::UpdateStateFromMovementComponent()
{
	MonitorMovementComponent();
}

FJSBSimStateTransition UJSBSimSimulationFSMComponent::GetLastTransition() const
{
	if (StateHistory.Num() > 0)
	{
		return StateHistory.Last();
	}
	return FJSBSimStateTransition();
}

bool UJSBSimSimulationFSMComponent::IsValidTransition(EJSBSimSimulationState FromState, EJSBSimSimulationState ToState) const
{
	// Same state is always valid (no-op)
	if (FromState == ToState)
	{
		return true;
	}

	// Stopping can be reached from any state
	if (ToState == EJSBSimSimulationState::Stopping)
	{
		return true;
	}

	// Error can be reached from any state
	if (ToState == EJSBSimSimulationState::Error)
	{
		return true;
	}

	// Valid transitions based on state machine definition
	switch (FromState)
	{
	case EJSBSimSimulationState::Uninitialized:
		return ToState == EJSBSimSimulationState::Initializing;

	case EJSBSimSimulationState::Initializing:
		return ToState == EJSBSimSimulationState::Loading || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Loading:
		return ToState == EJSBSimSimulationState::Preparing || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Preparing:
		return ToState == EJSBSimSimulationState::Ready || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Ready:
		return ToState == EJSBSimSimulationState::Running || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Running:
		return ToState == EJSBSimSimulationState::Paused || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Paused:
		return ToState == EJSBSimSimulationState::Running || 
		       ToState == EJSBSimSimulationState::Error;

	case EJSBSimSimulationState::Error:
		// From Error, can only go to Uninitialized (reset) or Stopping
		return ToState == EJSBSimSimulationState::Uninitialized || 
		       ToState == EJSBSimSimulationState::Stopping;

	case EJSBSimSimulationState::Stopping:
		// Terminal state - no transitions allowed
		return false;

	default:
		return false;
	}
}

bool UJSBSimSimulationFSMComponent::TransitionToState(EJSBSimSimulationState NewState, bool bForce)
{
	// Check if transition is valid
	if (!bForce && !IsValidTransition(CurrentState, NewState))
	{
		UE_LOG(LogJSBSimSimulationFSM, Warning, 
			TEXT("Invalid state transition from %s to %s"), 
			*GetStateName(CurrentState), 
			*GetStateName(NewState));
		return false;
	}

	// Same state - no transition needed
	if (CurrentState == NewState)
	{
		return true;
	}

	// Store previous state
	PreviousState = CurrentState;

	// Fire exit event for previous state
	OnStateExited.Broadcast(PreviousState);

	// Update current state
	CurrentState = NewState;

	// Record transition in history
	FJSBSimStateTransition Transition;
	Transition.FromState = PreviousState;
	Transition.ToState = CurrentState;
	Transition.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	StateHistory.Add(Transition);

	// Limit history size
	if (StateHistory.Num() > MaxHistorySize)
	{
		StateHistory.RemoveAt(0, StateHistory.Num() - MaxHistorySize);
	}

	// Fire enter event for new state
	OnStateEntered.Broadcast(CurrentState);

	// Fire state changed event
	OnStateChanged.Broadcast(CurrentState, PreviousState);

	// Log transition if enabled
	if (bLogStateTransitions)
	{
		UE_LOG(LogJSBSimSimulationFSM, Log, 
			TEXT("State transition: %s -> %s (Time: %.3f)"), 
			*GetStateName(PreviousState), 
			*GetStateName(CurrentState),
			Transition.Timestamp);
	}

	return true;
}

void UJSBSimSimulationFSMComponent::MonitorMovementComponent()
{
	if (!TargetMovementComponent)
	{
		return;
	}

	// Check for errors first
	if (CheckForErrors())
	{
		if (CurrentState != EJSBSimSimulationState::Error)
		{
			TransitionToState(EJSBSimSimulationState::Error, true);
		}
		return;
	}

	// If we're in Error state and no longer have errors, allow recovery
	if (CurrentState == EJSBSimSimulationState::Error && !CheckForErrors())
	{
		// Stay in error state - manual reset required
		return;
	}

	// Don't auto-transition if paused or in terminal states
	if (bIsPaused && CurrentState == EJSBSimSimulationState::Paused)
	{
		return;
	}

	if (CurrentState == EJSBSimSimulationState::Stopping)
	{
		return;
	}

	// Determine target state based on JSBSimMovementComponent flags
	// Note: We need to access protected members, so we'll use public methods or reflection
	// For now, we'll use a simplified approach based on what we can observe

	EJSBSimSimulationState TargetState = CurrentState;

	// Check if JSBSim is initialized
	// Since we can't directly access JSBSimInitialized, we'll infer from component state
	// We can check if the component has been set up by checking if it has an aircraft model
	bool bHasAircraftModel = !TargetMovementComponent->AircraftModel.IsEmpty();
	
	// Simplified state detection - in a real implementation, you might want to:
	// 1. Add public getter methods to JSBSimMovementComponent for these flags
	// 2. Use reflection to access protected members
	// 3. Add events/callbacks to JSBSimMovementComponent for lifecycle changes

	// For now, we'll use a basic heuristic:
	// - If no aircraft model, assume Uninitialized
	// - If aircraft model but component not ticking, assume Initializing/Loading
	// - If component is ticking, assume Running (or Ready if just started)

	if (!bHasAircraftModel)
	{
		TargetState = EJSBSimSimulationState::Uninitialized;
	}
	else
	{
		// Check if component is actively ticking (simplified check)
		bool bIsTicking = TargetMovementComponent->IsComponentTickEnabled() && 
		                  TargetMovementComponent->PrimaryComponentTick.IsTickFunctionEnabled();

		if (!bIsTicking)
		{
			// Component exists but not ticking - could be Initializing, Loading, or Preparing
			if (CurrentState == EJSBSimSimulationState::Uninitialized)
			{
				TargetState = EJSBSimSimulationState::Initializing;
			}
			else if (CurrentState == EJSBSimSimulationState::Initializing)
			{
				TargetState = EJSBSimSimulationState::Loading;
			}
			else if (CurrentState == EJSBSimSimulationState::Loading)
			{
				TargetState = EJSBSimSimulationState::Preparing;
			}
			else if (CurrentState == EJSBSimSimulationState::Preparing)
			{
				TargetState = EJSBSimSimulationState::Ready;
			}
		}
		else
		{
			// Component is ticking - should be Running (unless paused)
			if (CurrentState == EJSBSimSimulationState::Ready || 
			    CurrentState == EJSBSimSimulationState::Preparing)
			{
				TargetState = EJSBSimSimulationState::Running;
			}
			else if (CurrentState == EJSBSimSimulationState::Running && !bIsPaused)
			{
				TargetState = EJSBSimSimulationState::Running;
			}
		}
	}

	// Transition to target state if different
	if (TargetState != CurrentState)
	{
		TransitionToState(TargetState, false);
	}
}

bool UJSBSimSimulationFSMComponent::CheckForErrors() const
{
	if (!TargetMovementComponent)
	{
		return false;
	}

	// Check for error conditions
	// In a real implementation, you might:
	// 1. Check if JSBSimMovementComponent has crashed
	// 2. Check if aircraft failed to load
	// 3. Check for invalid configurations
	// 4. Listen to error events from JSBSimMovementComponent

	// For now, we'll check if the component has an aircraft model but something seems wrong
	// This is a simplified check - you may want to add more sophisticated error detection

	return false; // No errors detected by default
}

FString UJSBSimSimulationFSMComponent::GetStateName(EJSBSimSimulationState State) const
{
	switch (State)
	{
	case EJSBSimSimulationState::Uninitialized:
		return TEXT("Uninitialized");
	case EJSBSimSimulationState::Initializing:
		return TEXT("Initializing");
	case EJSBSimSimulationState::Loading:
		return TEXT("Loading");
	case EJSBSimSimulationState::Preparing:
		return TEXT("Preparing");
	case EJSBSimSimulationState::Ready:
		return TEXT("Ready");
	case EJSBSimSimulationState::Running:
		return TEXT("Running");
	case EJSBSimSimulationState::Paused:
		return TEXT("Paused");
	case EJSBSimSimulationState::Stopping:
		return TEXT("Stopping");
	case EJSBSimSimulationState::Error:
		return TEXT("Error");
	default:
		return TEXT("Unknown");
	}
}

