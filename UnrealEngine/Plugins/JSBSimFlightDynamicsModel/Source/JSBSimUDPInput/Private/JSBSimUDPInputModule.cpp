// Copyright Epic Games, Inc. All Rights Reserved.

#include "JSBSimUDPInputModule.h"
#include "Core.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FJSBSimUDPInputModule"

void FJSBSimUDPInputModule::StartupModule()
{
	
}

void FJSBSimUDPInputModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJSBSimUDPInputModule, JSBSimUDPInput)
DEFINE_LOG_CATEGORY(LogJSBSimUDPInput);

