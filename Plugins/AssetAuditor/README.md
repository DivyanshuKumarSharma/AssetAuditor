# Asset Auditor

An Unreal Engine editor plugin that scans project content for convention and budget problems, lists them in a sortable, filterable table, and fixes most of them with one click. The same rules also run through **Data Validation** (on save, *Validate Assets*, and pre-submit) and through a **commandlet** for CI.

Built and tested on **Unreal Engine 5.6.1**, Windows.

## What it checks

| Rule | Finds | Severity | One-click fix | Undo with Ctrl+Z |
|---|---|---|---|---|
| **Naming Convention** | Missing or wrong type prefix (`T_`, `SM_`, `M_`, `MI_`, `BP_`, `WBP_`, `ABP_`…) | Warning | Rename via `IAssetTools` (fixes references, leaves redirectors) | No, renames move files (you're asked to confirm first) |
| **Texture Size** | Non-power-of-two textures; textures larger than the size budget (default 2048) | Warning | Set *Padding* to *Pad to Power of Two* / set *Max Texture Size* to the budget | Yes |
| **Static Mesh LODs & Collision** | Too few LODs (skips tiny and Nanite meshes); no simple collision (skips *Complex as Simple* and *NoCollision* meshes) | Warning / **Error** | Generate LODs / add box collision (engine's `UStaticMeshEditorSubsystem`) | Yes |
| **Unused Asset** | Assets no other package references (levels and Asset Manager primary assets count as roots) | Info | None: deleting is a human decision | n/a |

Most checks read **Asset Registry tags** (`Dimensions`, `LODs`, `Triangles`, `CollisionPrims`…) and never load the asset. A rule only loads an asset when the tags can't answer the question, for example a texture whose tag says 1000×600 might already be set to pad itself.

## Install

1. Copy the `AssetAuditor` folder into your project's `Plugins/` folder.
2. Build the editor target, either from your IDE or the command line:
   ```bash
   "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development -Project="<path>\<Project>.uproject" -WaitMutex
   ```
3. Open the editor and go to **Tools → Asset Auditor**. The tab is dockable and remembers where you put it.

The plugin depends on the engine's **DataValidation** plugin, which its `.uplugin` enables automatically. Python is *not* required, except for the optional test-content script.

## Using the panel

- **Scan** audits the folder in the path box (default `/Game`, recursive). The button waits until the Asset Registry has finished discovering files after editor startup.
- **Sort** by clicking any column header. **Filter** with the Error, Warning and Info toggles and the search box (matches asset path, rule and message).
- **Click an asset name** to select it in the Content Browser. **Double-click a row** to open its editor. **Right-click** for Fix, Show in Content Browser and Open.
- **Fix** (per row), **Fix Selected** and the context menu all apply suggested fixes. All property fixes from one click share a single transaction, so one Ctrl+Z reverts the whole batch, and the table refreshes itself on undo and redo.
- **Export CSV** saves the issues you're currently viewing (after filters) to a UTF-8 CSV that opens cleanly in Excel.

Fixed assets are modified in memory. Save them as usual (*File → Save All*).

## Data Validation

`UAssetAuditorValidator` (a `UEditorValidatorBase`) runs the same rules wherever the editor validates assets: on save (if *Validate On Save* is enabled), from **right-click → Asset Actions → Validate Assets**, and before a source-control submit. Errors make an asset *invalid*. Warnings and info are reported but don't fail validation. The Unused rule is off for validation by default, because every new asset is "unused" at the moment it's first saved.

## Settings

**Project Settings → Plugins → Asset Auditor** has the global settings: default scan path, excluded folders, and the CI failure threshold.

Every rule also has its own page, **Project Settings → Plugins → Asset Auditor – \<Rule\>**, where you can enable or disable it, choose whether it runs during Data Validation, and set its thresholds (prefix table, texture budget, LOD and triangle limits, ignored folders). Everything is saved to `Config/DefaultEditor.ini`, so the whole team shares it through source control.

## CI / command line

```bash
UnrealEditor-Cmd.exe MyProject.uproject -run=AssetAudit -Path=/Game -Report=Saved/AssetAudit/AssetAudit.csv -FailOn=Error -unattended -nosplash
```

| Argument | Default | Meaning |
|---|---|---|
| `-Path=` | project setting (`/Game`) | Folder to audit, recursive |
| `-Report=` | `Saved/AssetAudit/AssetAudit.csv` | CSV output |
| `-FailOn=` | project setting (`Error`) | `Error`, `Warning`, `Info` or `None` |

Exit codes: **0** pass · **1** issues at or above `-FailOn` · **2** bad arguments · **3** report couldn't be written. Issues are logged at *Display* verbosity on purpose, so CI wrappers that fail jobs on logged errors don't double-report. The exit code is the contract.

Console commands (editor): `AssetAuditor.Scan [/Game/Path]` logs all issues, and `AssetAuditor.Fix <RuleId|All> [/Game/Path]` bulk-applies fixes, renames included, without a confirmation prompt.

## Adding a rule

Subclass `UAssetAuditRule` in any editor module that depends on `AssetAuditor`. That's all: the scanner discovers rules by reflection, and the rule automatically gets a Project Settings page and appears in the panel, the validator and the commandlet.

```cpp
// AssetAuditRule_TriangleBudget.h
#pragma once

#include "AssetAuditRule.h"
#include "Engine/StaticMesh.h"
#include "AssetAuditRule_TriangleBudget.generated.h"

/** Static meshes must stay under a triangle budget. */
UCLASS(meta = (DisplayName = "Triangle Budget"))
class UAssetAuditRule_TriangleBudget : public UAssetAuditRule
{
	GENERATED_BODY()

public:
	virtual bool AppliesTo(const FAssetData& Asset) const override
	{
		return Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName();
	}

	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const override
	{
		int32 Triangles = 0;
		if (Asset.GetTagValue(TEXT("Triangles"), Triangles) && Triangles > MaxTriangles)
		{
			AddIssue(OutIssues, Asset, EAssetAuditSeverity::Warning,
				FText::Format(INVTEXT("{0} triangles exceeds the budget of {1}."), Triangles, MaxTriangles));
		}
	}

private:
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxTriangles = 50000;
};
```

(This exact code was compiled against the plugin to check the example.)

Guidelines:

- **Stateless.** Rules are used through their class default object. Read inputs from the asset and from `UPROPERTY(Config)` fields only.
- **Tags first.** `AppliesTo` must not load anything. In `Check`, prefer `FAssetData` tags and call `GetAsset()` only when you must.
- **Fixes.** Set `Issue.FixId` and `Issue.FixDescription` in `Check`, and override `ApplyFix`. The caller has already opened an `FScopedTransaction`, so call `Modify()` on every object before changing it, or use the engine helpers, which do. If your fix touches files or packages (rename, delete, move), set `Issue.bFixIsUndoable = false`. It will then run outside the transaction and the UI will ask before applying it.

## Test content

`Scripts/create_test_assets.py` builds `/Game/AuditTests`: ten generated assets with known problems (wrong prefixes, a 1000×600 texture, a 4096² texture, a dense sphere with no collision or LODs) plus a clean control texture. Every rule has something to find. Expect **17 issues: 1 error, 9 warnings, 7 info**. The script is idempotent, so you can re-run it to reset after testing fixes.

```bash
UnrealEditor-Cmd.exe ToolsLab.uproject -run=pythonscript -script="Plugins/AssetAuditor/Scripts/create_test_assets.py"
```

## Tests

Automation tests live in `Private/Tests/` and show up under **AssetAuditor** in the Session Frontend's *Automation* tab (Tools → Session Frontend).

| Test | Checks |
|---|---|
| `AssetAuditor.Rules.Naming.ExpectedName` | Expected-name logic on in-memory asset data: missing prefix, another type's prefix replaced (`SM_WrongPrefix` texture → `T_WrongPrefix`), `MI_`/`M_` stripped whole, `AnimBlueprint` gets `ABP_` rather than `BP_` |
| `AssetAuditor.Report.CsvEscaping` | `ToCsv` quotes fields with commas, quotes or newlines and doubles embedded quotes |
| `AssetAuditor.Scanner.ScanTestContent` | A scan of `/Game/AuditTests` finds exactly 17 issues (1 error, 9 warnings, 7 info), none on the control texture, sorted most severe first |
| `AssetAuditor.Fixes.TextureSize` | `ApplyFixes` pads the NPOT texture and clamps the 4096² one, and a rescan comes back clean. The test then restores the old values and saves nothing |

The scanner and fix tests need the test content (see above); re-run the script if you've saved fixes to it. Headless:

```bash
UnrealEditor-Cmd.exe ToolsLab.uproject -ExecCmds="Automation RunTests AssetAuditor; Quit" -unattended -nullrhi -log
```

## Code map

| Path | Role |
|---|---|
| `Public/AssetAuditRule.h` | Rule base class: the extension point |
| `Public/AssetAuditScanner.h` | UI-free engine: discovery, scan, fixes, CSV. Shared by every entry point |
| `Public/AssetAuditorSettings.h` | `UDeveloperSettings` for global options |
| `Private/Rules/` | The four built-in rules |
| `Private/UI/SAssetAuditorPanel.*` | Slate panel (`SListView`, sorting, filtering, undo client) |
| `Private/Validation/` | Data Validation bridge |
| `Private/Commandlets/` | CI commandlet |
| `Private/AssetAuditorModule.*` | Tab spawner, per-rule settings pages, console commands |
| `Private/Tests/` | Automation tests |

## Known limitations

- **Unused Asset can't see string references.** Assets loaded only by path from C++ or config (for example `ConstructorHelpers`, `.ini` soft paths) look unused. That's why it's Info-only and has no fix. Checking real reachability from maps, primary assets and config roots would be stricter.
- **Unused is per asset, not per subgraph.** A chain of assets that only reference each other (an unused mesh using an unused material) only reports the top of the chain.
- **Naming uses the asset class.** It can't tell a Blueprint Interface or Function Library apart from a regular Blueprint; all of them get `BP_`.
- **Scans run on the game thread.** They're fast because they use tags, and they show a cancellable progress dialog, but a very large project would benefit from time-slicing.
- **Settings pages are registered at startup.** Rules from modules that load later still run, but they won't get a Project Settings page until the next editor start.
