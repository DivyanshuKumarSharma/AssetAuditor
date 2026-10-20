#pragma once

#include "Modules/ModuleInterface.h"

class SDockTab;
class FSpawnTabArgs;

/** Editor module: owns the lifetime of the Asset Auditor tab spawner. */
class FAssetAuditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName AuditorTabName;

private:
	TSharedRef<SDockTab> SpawnAuditorTab(const FSpawnTabArgs& Args);
	void RegisterRuleSettings();

	TArray<FName> RegisteredRuleSections;
};
