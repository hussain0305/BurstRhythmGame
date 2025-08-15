// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class BurstRhythmGame : ModuleRules
{
	public BurstRhythmGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"DesktopPlatform", "SignalProcessing"
		});
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/AudioDecoders"));
	}
}
