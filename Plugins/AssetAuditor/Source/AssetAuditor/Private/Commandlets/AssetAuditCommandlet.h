#pragma once

#include "Commandlets/Commandlet.h"
#include "AssetAuditCommandlet.generated.h"

/**
 * Headless audit for CI:
 *   UnrealEditor-Cmd.exe Project.uproject -run=AssetAudit [-Path=/Game] [-Report=out.csv] [-FailOn=Error|Warning|Info|None]
 *
 * Exit codes: 0 = pass, 1 = issues at or above the -FailOn severity, 2 = bad arguments, 3 = report not written.
 * The engine finds the class by name: "-run=AssetAudit" resolves to UAssetAuditCommandlet.
 */
UCLASS()
class UAssetAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
