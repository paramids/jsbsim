// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JSBSimSimulationFSM : ModuleRules
{
	public JSBSimSimulationFSM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"JSBSimFlightDynamicsModel",
				"JSBSimUDPInput"
			}
		);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}

