// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildTool;

public class PCGExtendedToolkit : ModuleRules
{
	private const string PluginName = "PCGExtendedToolkit";
	private const string ModulePrefix = "PCGEx";
	private const string EditorSuffix = "Editor";
	private const string UncookedSuffix = "Uncooked";

	private static readonly string[] BaseDependencies = { "PCGExCore", "PCGExBlending" };
	private static readonly string[] BaseEditorDependencies = { "PCGExCoreEditor" };

	private readonly Dictionary<string, List<string>> _moduleDependencies = new();

	public PCGExtendedToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH")); 
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;

		ConfigureBaseDependencies();

		FileReference upluginFile = new FileReference(Path.Combine(PluginDirectory, $"{PluginName}.uplugin"));
		ExternalDependencies.Add(upluginFile.FullName);

		string pluginsDepsPath = Path.Combine(PluginDirectory, "Config", "PluginsDeps.ini");
		if (File.Exists(pluginsDepsPath))
		{
			ExternalDependencies.Add(pluginsDepsPath);
		}

		PluginDescriptor descriptor = PluginDescriptor.FromFile(upluginFile);
		var declaredModules = new HashSet<string>(descriptor.Modules.Select(m => m.Name));
		var enabledPlugins = new HashSet<string>(
			descriptor.Plugins
				.Where(p => p.bEnabled)
				.Select(p => p.Name)
		);

		var modulePluginRequirements = LoadPluginsDeps(pluginsDepsPath);
		ValidatePluginRequirements(declaredModules, enabledPlugins, modulePluginRequirements);

		foreach (ModuleDescriptor module in descriptor.Modules)
		{
			RegisterModule(module, declaredModules);
		}

		ValidateConfiguration(declaredModules);
		GenerateSubModulesHeader();
		
		PublicIncludePaths.AddRange(new string[] { Path.Combine(ModuleDirectory, "../../Intermediate/Generated") });
	}

	private Dictionary<string, List<string>> LoadPluginsDeps(string filePath)
	{
		var deps = new Dictionary<string, List<string>>();

		if (!File.Exists(filePath))
		{
			Logger.LogInformation("[{Plugin}] PluginsDeps.ini not found - skipping plugin requirement validation", PluginName);
			return deps;
		}

		string currentModule = null;

		foreach (string line in File.ReadAllLines(filePath))
		{
			string trimmed = line.Trim();
			if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith("#") || trimmed.StartsWith(";"))
				continue;

			// Section header: [ModuleName]
			if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
			{
				currentModule = trimmed.Substring(1, trimmed.Length - 2);
				if (!deps.ContainsKey(currentModule))
				{
					deps[currentModule] = new List<string>();
				}
				continue;
			}

			// Plugin name under current section
			if (currentModule != null && !string.IsNullOrEmpty(trimmed))
			{
				deps[currentModule].Add(trimmed);
			}
		}

		return deps;
	}

	private void ValidatePluginRequirements(
		HashSet<string> declaredModules,
		HashSet<string> enabledPlugins,
		Dictionary<string, List<string>> modulePluginRequirements)
	{
		var errors = new List<string>();

		foreach (var (moduleName, requiredPlugins) in modulePluginRequirements)
		{
			if (!declaredModules.Contains(moduleName)) continue;

			foreach (string plugin in requiredPlugins)
			{
				if (!enabledPlugins.Contains(plugin))
				{
					errors.Add($"Module '{moduleName}' requires plugin '{plugin}' to be listed and enabled in .uplugin");
				}
			}
		}

		if (errors.Count > 0)
		{
			string message = $"[{PluginName}] Plugin requirements not met:\n" + string.Join("\n", errors.Select(e => $"  - {e}"));
			throw new BuildException(message);
		}
	}

	private void ConfigureBaseDependencies()
	{
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PCG" });
		PublicDependencyModuleNames.AddRange(BaseDependencies);
		PrivateDependencyModuleNames.Add("DeveloperSettings");

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Settings" });
			PrivateDependencyModuleNames.AddRange(BaseEditorDependencies);
		}
	}

	private void RegisterModule(ModuleDescriptor module, HashSet<string> declaredModules)
	{
		string moduleName = module.Name;
		if (IsUmbrellaModule(moduleName)) return;
		if (!IsPCGExModule(moduleName)) return;

		// Classify by the DECLARED module type, not the name suffix: a direct module
		// dependency bypasses the .uplugin type gating, so referencing an Editor or
		// UncookedOnly module (K2 node hosts) from a cooked game target would drag
		// UnrealEd into UnrealGame builds.
		bool isEditorOnlyHost =
			module.Type == ModuleHostType.Editor ||
			module.Type == ModuleHostType.EditorNoCommandlet ||
			module.Type == ModuleHostType.UncookedOnly;

		if (isEditorOnlyHost && !Target.bBuildEditor) return;

		if (isEditorOnlyHost)
		{
			PrivateDependencyModuleNames.Add(moduleName);
		}
		else
		{
			PublicDependencyModuleNames.Add(moduleName);
		}

		string buildFile = GetBuildFilePath(moduleName);
		if (File.Exists(buildFile))
		{
			ExternalDependencies.Add(buildFile);
			_moduleDependencies[moduleName] = ScanDependencies(buildFile, moduleName);
		}
	}

	private List<string> ScanDependencies(string buildFilePath, string selfName)
	{
		var deps = new List<string>();
		string content = File.ReadAllText(buildFilePath);

		var blockPattern = new Regex(
			@"(?:Public|Private)DependencyModuleNames\s*\.\s*AddRange\s*\(\s*new\s*(?:string)?\s*\[\s*\]\s*\{([^}]*)\}",
			RegexOptions.Singleline
		);

		foreach (Match block in blockPattern.Matches(content))
		{
			var namePattern = new Regex(@"""(PCGEx\w+)""");
			foreach (Match name in namePattern.Matches(block.Groups[1].Value))
			{
				string dep = name.Groups[1].Value;
				if (dep != selfName && !deps.Contains(dep))
				{
					deps.Add(dep);
				}
			}
		}

		return deps;
	}

	private void ValidateConfiguration(HashSet<string> declaredModules)
	{
		var missingDeps = new Dictionary<string, List<string>>();
		var missingCompanions = new List<string>();

		foreach (var (module, deps) in _moduleDependencies)
		{
			foreach (string dep in deps)
			{
				if (!declaredModules.Contains(dep) && !IsBaseDependency(dep))
				{
					if (!missingDeps.ContainsKey(dep))
						missingDeps[dep] = new List<string>();
					missingDeps[dep].Add(module);
				}
			}
		}

		foreach (string moduleName in declaredModules)
		{
			if (IsUmbrellaModule(moduleName)) continue;

			// Companion modules follow the name convention Foo -> FooEditor / FooUncooked;
			// warn when one exists on disk but is not declared in the .uplugin.
			foreach (string suffix in new[] { EditorSuffix, UncookedSuffix })
			{
				if (moduleName.EndsWith(suffix)) continue;

				string companionName = moduleName + suffix;
				if (Directory.Exists(Path.Combine(ModuleDirectory, "..", companionName)) && !declaredModules.Contains(companionName))
				{
					missingCompanions.Add(companionName);
				}
			}
		}

		foreach (var (dep, referencedBy) in missingDeps.OrderBy(kv => kv.Key))
		{
			Logger.LogWarning(
				"[{Plugin}] Dependency '{Dep}' required by [{Refs}] is not declared in .uplugin. Add:\n{Entry}",
				PluginName, dep, string.Join(", ", referencedBy), FormatModuleEntry(dep)
			);
		}

		foreach (string companion in missingCompanions.OrderBy(e => e))
		{
			Logger.LogWarning(
				"[{Plugin}] Companion module '{Companion}' exists but is not declared in .uplugin. Add:\n{Entry}",
				PluginName, companion, FormatModuleEntry(companion)
			);
		}
	}

	private void GenerateSubModulesHeader()
	{
		// Editor and client/server targets generate different content (editor-only modules
		// are skipped in RegisterModule), so each flavor writes to its own file - otherwise
		// switching targets rewrites a shared header and dirties every TU that includes it.
		string headerPath = Path.Combine(ModuleDirectory, "..", "..", "Intermediate", "Generated", Target.bBuildEditor ? "Editor" : "", "PCGExSubModules.generated.h");
		Directory.CreateDirectory(Path.GetDirectoryName(headerPath)!);

		var modules = _moduleDependencies.Keys.OrderBy(m => m).ToList();
		var sb = new StringBuilder();

		sb.AppendLine("// Auto-generated by PCGExtendedToolkit.Build.cs - DO NOT EDIT");
		sb.AppendLine("#pragma once");
		sb.AppendLine("#include \"CoreMinimal.h\"");
		sb.AppendLine();
		sb.AppendLine("namespace PCGExSubModules");
		sb.AppendLine("{");

		sb.AppendLine("\tinline const TArray<FString>& GetEnabledModules()");
		sb.AppendLine("\t{");
		sb.AppendLine("\t\tstatic TArray<FString> Modules = {");
		for (int i = 0; i < modules.Count; i++)
		{
			sb.AppendLine($"\t\t\tTEXT(\"{modules[i]}\"){(i < modules.Count - 1 ? "," : "")}");
		}
		sb.AppendLine("\t\t};");
		sb.AppendLine("\t\treturn Modules;");
		sb.AppendLine("\t}");
		sb.AppendLine();

		sb.AppendLine("\tinline const TMap<FString, TArray<FString>>& GetModuleDependencies()");
		sb.AppendLine("\t{");
		sb.AppendLine("\t\tstatic TMap<FString, TArray<FString>> Dependencies = {");
		var sortedDeps = _moduleDependencies.OrderBy(kv => kv.Key).ToList();
		for (int i = 0; i < sortedDeps.Count; i++)
		{
			var (mod, deps) = sortedDeps[i];
			string depsStr = deps.Count > 0
				? string.Join(", ", deps.Select(d => $"TEXT(\"{d}\")"))
				: "";
			sb.AppendLine($"\t\t\t{{ TEXT(\"{mod}\"), {{ {depsStr} }} }}{(i < sortedDeps.Count - 1 ? "," : "")}");
		}
		sb.AppendLine("\t\t};");
		sb.AppendLine("\t\treturn Dependencies;");
		sb.AppendLine("\t}");
		sb.AppendLine("}");

		string content = sb.ToString();
		if (!File.Exists(headerPath) || File.ReadAllText(headerPath) != content)
		{
			File.WriteAllText(headerPath, content);
		}
	}

	private string GetBuildFilePath(string moduleName) =>
		Path.Combine(ModuleDirectory, "..", moduleName, $"{moduleName}.Build.cs");

	private string FormatModuleEntry(string name)
	{
		// Suggested entry only -- "Type" is inferred from the name suffix and may need
		// manual adjustment (e.g. a K2-node-hosting module must be "UncookedOnly" even
		// when named *Editor, like PCGExPropertiesEditor). Scripts/generate-uplugin.js/.py
		// preserve already-declared entries verbatim, so a hand-corrected type sticks.
		string type = name.EndsWith(UncookedSuffix) ? "UncookedOnly"
			: name.EndsWith(EditorSuffix) ? "Editor"
			: "Runtime";

		// Editor and UncookedOnly modules never ship in cooked builds: desktop platforms only
		string platforms = type == "Runtime"
			? "\"Win64\", \"Mac\", \"IOS\", \"Android\", \"Linux\", \"LinuxArm64\""
			: "\"Win64\", \"Mac\", \"Linux\"";

		return $@"		{{
			""Name"": ""{name}"",
			""Type"": ""{type}"",
			""LoadingPhase"": ""Default"",
			""PlatformAllowList"": [ {platforms} ]
		}}
		(""Type"" guessed from the name suffix -- adjust if needed, e.g. ""UncookedOnly"" for K2 node hosts.)";
	}

	private bool IsBaseDependency(string name) =>
		BaseDependencies.Contains(name) || BaseEditorDependencies.Contains(name);

	private static bool IsPCGExModule(string name) => name.StartsWith(ModulePrefix);

	private static bool IsUmbrellaModule(string name) =>
		name == PluginName || name == PluginName + EditorSuffix;
}