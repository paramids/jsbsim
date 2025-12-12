// Copyright Epic Games, Inc. All Rights Reserved.

#include "JSBSimSimulationFSMModule.h"
#include "Core.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FJSBSimSimulationFSMModule"

void FJSBSimSimulationFSMModule::StartupModule()
{
	
}

void FJSBSimSimulationFSMModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJSBSimSimulationFSMModule, JSBSimSimulationFSM)
DEFINE_LOG_CATEGORY(LogJSBSimSimulationFSM);

