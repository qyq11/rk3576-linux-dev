param([switch]$FinalSourceSynchronized)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$repo='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot=Join-Path $repo 'temp/sn100_host/instrument_20261024/source/06_programs/sn100_host'
$initialSourceRoot=Join-Path $repo 'temp/sn100_host/instrument_20261024/source_initial/06_programs/sn100_host'
$work=Join-Path $repo 'temp/sn100_host/instrument_20261024/windows'
$finalWork=Join-Path $work 'final_update'
$public=Join-Path $repo '04_references/sn100_host/instrument_20261024/windows/final_update'
$qtBin='C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin='C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake=Join-Path $qtBin 'qmake.exe'
$make=Join-Path $compilerBin 'mingw32-make.exe'
$oldPath=$env:PATH
. (Join-Path $work 'incremental_helpers.ps1')
. (Join-Path $work 'incremental_contract.ps1')
if (!$FinalSourceSynchronized) { throw 'Requires explicit parent confirmation that final source synchronization is complete.' }
if (!(Test-Path -LiteralPath $initialSourceRoot -PathType Container)) { throw 'Verified source_initial preservation is required.' }
if (Test-Path -LiteralPath $finalWork) { throw 'Fresh final_update evidence directory required; no old result overwritten.' }
New-Item -ItemType Directory -Path $finalWork | Out-Null
$results=[ordered]@{schema='sn100-windows-final-incremental-v1';started_utc=[DateTime]::UtcNow.ToString('o');scope='Final synchronized source; Windows32 compile/link and object reuse audit only. No GUI, connection or deployment.';published=$false;stages=@()}
try {
    $env:PATH="$compilerBin;$qtBin;$oldPath"
    $contract=Get-Content -LiteralPath (Join-Path $work 'initial_contract/contract.json') -Raw | ConvertFrom-Json
    $originalInputs=Get-Content -LiteralPath (Join-Path $work 'all_formal_inputs_before.json') -Raw | ConvertFrom-Json
    $originalMap=Get-Map $originalInputs
    # The source directory keeps its original path for qmake and __FILE__.
    # The old source bytes must be recoverable independently before any reuse.
    foreach ($record in $originalInputs) {
        $relative=$record.path.Substring(([IO.Path]::GetFullPath($sourceRoot).Replace('\','/')+'/').Length)
        $preserved=Join-Path $initialSourceRoot $relative
        if (!(Test-Path -LiteralPath $preserved -PathType Leaf) -or (Get-FileRecord $preserved).sha256 -ne $record.sha256) { throw "source_initial mismatch: $relative" }
    }
    $toolBaseline=(Get-Content -LiteralPath (Join-Path $work 'toolchain.json') -Raw | ConvertFrom-Json).binaries
    $toolBefore=@($toolBaseline | ForEach-Object { Get-FileRecord $_.path })
    $toolBeforeMap=Get-Map $toolBefore
    Save-Json $toolBefore (Join-Path $finalWork 'toolchain_before.json')
    $toolContractChanges=@($toolBaseline | Where-Object { $toolBeforeMap[$_.path].sha256 -ne $_.sha256 })
    $externalBaseline=Get-Content -LiteralPath (Join-Path $work 'initial_contract/external_inputs.json') -Raw | ConvertFrom-Json
    $externalContractChanges=@($externalBaseline | Where-Object { !(Test-Path -LiteralPath $_.path) -or (Get-FileRecord $_.path).sha256 -ne $_.sha256 })
    $results.initial_tool_contract_changed=($toolContractChanges.Count -ne 0)
    $results.initial_external_contract_changed=($externalContractChanges.Count -ne 0)
    $before=@(Get-SourceSnapshot)
    $beforeMap=Get-Map $before
    Save-Json $before (Join-Path $finalWork 'all_formal_inputs_before.json')
    $initialToFinal=@()
    foreach ($record in $before) {
        if (!$originalMap.ContainsKey($record.path)) { $initialToFinal += [ordered]@{path=$record.path;change='added';sha256=$record.sha256} }
        elseif ($originalMap[$record.path].sha256 -ne $record.sha256) { $initialToFinal += [ordered]@{path=$record.path;change='changed';old_sha256=$originalMap[$record.path].sha256;sha256=$record.sha256} }
    }
    foreach ($record in $originalInputs) {
        if (!$beforeMap.ContainsKey($record.path)) { $initialToFinal += [ordered]@{path=$record.path;change='removed';old_sha256=$record.sha256} }
    }
    Save-Json $initialToFinal (Join-Path $finalWork 'initial_to_final_source_changes.json')
    $actual=@{}
    $externalBefore=@{}
    foreach ($target in @(
        @{name='shell';project='shell/shell.pro';output='sn100_shell.exe'},
        @{name='renderer';project='history/renderer.pro';output='sn100_report_renderer.exe'}
    )) {
        $name=$target.name
        $directory=Join-Path $work ($name+'_build')
        $baseline=@($contract.targets | Where-Object { $_.name -eq $name })[0]
        $oldArtifacts=Get-Content -LiteralPath (Join-Path $work ('initial_contract/'+$name+'_artifacts.json')) -Raw | ConvertFrom-Json
        $oldArtifactMap=Get-Map $oldArtifacts
        $currentArtifacts=@(Get-BuildArtifacts $directory)
        Save-Json $currentArtifacts (Join-Path $finalWork ($name+'_artifacts_before.json'))
        $currentArtifactMap=Get-Map $currentArtifacts
        $artifactContractChanges=@($oldArtifacts | Where-Object { !$currentArtifactMap.ContainsKey($_.path) -or $currentArtifactMap[$_.path].sha256 -ne $_.sha256 })
        $stage=[ordered]@{target=$name;directory=$directory;started_utc=[DateTime]::UtcNow.ToString('o');original_artifacts_changed=$artifactContractChanges.Count}
        $qmakeArguments=@((Join-Path $sourceRoot $target.project),'-spec','win32-g++','CONFIG+=release','CONFIG-=debug','CONFIG-=debug_and_release')
        $stage.qmake_exit=Invoke-Logged $qmake $qmakeArguments $directory (Join-Path $finalWork ($name+'_qmake.log'))
        if ($stage.qmake_exit -ne 0) { $results.stages += $stage;throw "Final qmake failed: $name" }
        $newFields=Get-MakeFields $directory
        Save-Json $newFields (Join-Path $finalWork ($name+'_make_fields.json'))
        $fieldChanges=@($newFields.Keys | Where-Object { $baseline.fields.$_ -cne $newFields[$_] })
        $stage.changed_compiler_fields=$fieldChanges
        $stage.full_rebuild_required=$fieldChanges.Count -ne 0 -or $artifactContractChanges.Count -ne 0 -or $toolContractChanges.Count -ne 0 -or $externalContractChanges.Count -ne 0
        if ($stage.full_rebuild_required) {
            $stage.clean_exit=Invoke-Logged $make @('clean') $directory (Join-Path $finalWork ($name+'_clean.log'))
            if ($stage.clean_exit -ne 0) { $results.stages += $stage;throw "Required full clean failed: $name" }
        }
        $dependencies=@(Get-MakefileSources $directory)
        if (!$dependencies.Count) { throw "No final actual formal dependencies: $name" }
        $stage.actual_input_count=$dependencies.Count
        Save-Json $dependencies (Join-Path $finalWork ($name+'_actual_input_paths.json'))
        foreach ($path in $dependencies) { $actual[$path]=$true }
        foreach ($path in (Get-MakeExternalInputs $directory)) { $externalBefore[$path]=Get-FileRecord $path }
        $stage.dry_run_exit=Invoke-Logged $make @('-n') $directory (Join-Path $finalWork ($name+'_dry_run.log'))
        if ($stage.dry_run_exit -ne 0) { $results.stages += $stage;throw "Final make dry-run failed: $name" }
        $makeLog=Join-Path $finalWork ($name+'_make.log')
        $stage.make_exit=Invoke-Logged $make @('-j2') $directory $makeLog
        $results.stages += $stage
        Save-Json $results (Join-Path $finalWork 'windows_result.json')
        if ($stage.make_exit -ne 0) { throw "Final make failed: $name" }
        $commands=@(Get-CompileCommands $makeLog $directory)
        Save-Json $commands (Join-Path $finalWork ($name+'_compiled_objects.json'))
        $compiled=@{}
        foreach ($command in $commands) { $compiled[$command.object]=$true }
        $afterArtifacts=@(Get-BuildArtifacts $directory)
        $afterArtifactMap=Get-Map $afterArtifacts
        Save-Json $afterArtifacts (Join-Path $finalWork ($name+'_artifacts_after.json'))
        $rules=@(Get-ObjectRules $directory)
        Save-Json $rules (Join-Path $finalWork ($name+'_object_rules.json'))
        $reused=0
        foreach ($rule in $rules) {
            $needsCompile=$stage.full_rebuild_required -or !$oldArtifactMap.ContainsKey($rule.object)
            foreach ($inputPath in $rule.inputs) {
                if ($beforeMap.ContainsKey($inputPath)) {
                    if (!$originalMap.ContainsKey($inputPath) -or $originalMap[$inputPath].sha256 -ne $beforeMap[$inputPath].sha256) { $needsCompile=$true }
                } elseif ($oldArtifactMap.ContainsKey($inputPath)) {
                    if (!$afterArtifactMap.ContainsKey($inputPath) -or $afterArtifactMap[$inputPath].sha256 -ne $oldArtifactMap[$inputPath].sha256) { $needsCompile=$true }
                }
            }
            if ($needsCompile -and !$compiled.ContainsKey($rule.object)) { throw "Changed dependency object was not compiled: $($rule.object)" }
            if (!$compiled.ContainsKey($rule.object)) {
                if (!$oldArtifactMap.ContainsKey($rule.object) -or !$afterArtifactMap.ContainsKey($rule.object) -or $oldArtifactMap[$rule.object].sha256 -ne $afterArtifactMap[$rule.object].sha256) { throw "Reused object byte contract failed: $($rule.object)" }
                ++$reused
            }
        }
        $stage.compiled_objects=$commands.Count
        $stage.reused_objects=$reused
        $stage.final_object_count=$rules.Count
        $stage.object_dependency_contract_passed=$true
        $stage.output=Join-Path $directory $target.output
        $stage.pe=Get-PeRecord $stage.output
        $stage.import_exit=Invoke-Logged (Join-Path $compilerBin 'objdump.exe') @('-p',$stage.output) $directory (Join-Path $finalWork ($name+'_pe_imports.log'))
        if ($stage.import_exit -ne 0) { throw "Final PE import audit failed: $name" }
        $stage.finished_utc=[DateTime]::UtcNow.ToString('o')
        Write-Output ("Final $name passed: compiled=$($stage.compiled_objects), reused=$reused")
    }
    $after=@(Get-SourceSnapshot)
    Save-Json $after (Join-Path $finalWork 'all_formal_inputs_after.json')
    $afterMap=Get-Map $after
    if ($beforeMap.Count -ne $afterMap.Count) { throw 'Source inventory changed during final compilation' }
    foreach ($record in $before) {
        if (!$afterMap.ContainsKey($record.path) -or $afterMap[$record.path].sha256 -ne $record.sha256) { throw "Source changed during final compilation: $($record.path)" }
    }
    $actualBefore=@($actual.Keys | Sort-Object | ForEach-Object { if (!$beforeMap.ContainsKey($_)) { throw "Actual input added after before snapshot: $_" };$beforeMap[$_] })
    Save-Json $actualBefore (Join-Path $finalWork 'actual_inputs_before.json')
    Save-Json @($actual.Keys | Sort-Object | ForEach-Object { $afterMap[$_] }) (Join-Path $finalWork 'actual_inputs_after.json')
    $results.actual_input_count=$actualBefore.Count
    $results.actual_inputs_unchanged=$true
    foreach ($required in @('shell/instrument_shell.cpp','instrument/instrument_page.cpp','instrument/maintenance_store.cpp','instrument/frmdriverset.ui','protocol/status_loopback.cpp','protocol/read_only_client.cpp','protocol/tcp_transport.cpp','protocol/read_only_messages.cpp')) {
        $path=[IO.Path]::GetFullPath((Join-Path $sourceRoot $required)).Replace('\','/')
        if (!$actual.ContainsKey($path)) { throw "Required final instrument/common input absent: $required" }
    }
    $toolAfter=@($toolBefore | ForEach-Object { Get-FileRecord $_.path })
    Save-Json $toolAfter (Join-Path $finalWork 'toolchain_after.json')
    foreach ($record in $toolAfter) { if ($toolBeforeMap[$record.path].sha256 -ne $record.sha256) { throw 'Toolchain changed during final build' } }
    $results.toolchain_unchanged=$true
    Save-Json @($externalBefore.Keys | Sort-Object | ForEach-Object { $externalBefore[$_] }) (Join-Path $finalWork 'external_inputs_before.json')
    $externalAfter=@($externalBefore.Keys | Sort-Object | ForEach-Object { Get-FileRecord $_ })
    foreach ($record in $externalAfter) { if ($externalBefore[$record.path].sha256 -ne $record.sha256) { throw "External build input changed: $($record.path)" } }
    Save-Json $externalAfter (Join-Path $finalWork 'external_inputs_after.json')
    $results.external_inputs_unchanged=$true
    $publish=Join-Path $work 'final_release'
    if (Test-Path -LiteralPath $publish) { throw 'final_release already exists; no overwrite permitted' }
    New-Item -ItemType Directory -Path $publish | Out-Null
    foreach ($stage in $results.stages) { Copy-Item -LiteralPath $stage.output -Destination $publish }
    $results.outputs=@($results.stages | ForEach-Object { Get-PeRecord (Join-Path $publish ([IO.Path]::GetFileName($_.output))) })
    $results.published=$true
    $results.exit_code=0
    $results.pair_publish_condition='Both final qmake/make stages exit 0; compiler/tool/external contracts enforced; changed-dependency objects compiled; reused object SHA unchanged; final source stable; PE32/i386 imports verified; prior pair and evidence preserved.'
} catch {
    $results.failure=$_.Exception.Message
    $results.exit_code=1
    Write-Output $results.failure
} finally {
    $env:PATH=$oldPath
    $results.path_restored=($env:PATH -ceq $oldPath)
    $results.finished_utc=[DateTime]::UtcNow.ToString('o')
    Save-Json $results (Join-Path $finalWork 'windows_result.json')
    New-Item -ItemType Directory -Path $public -Force | Out-Null
    $evidenceFiles=@(Get-ChildItem -LiteralPath $finalWork -File | Where-Object { $_.Extension -in @('.json','.log','.ps1') })
    foreach ($file in $evidenceFiles) { Copy-Item -LiteralPath $file.FullName -Destination $public }
    foreach ($name in @('build_windows_final.ps1','incremental_helpers.ps1','incremental_contract.ps1','prepare_incremental_contract.ps1')) { Copy-Item -LiteralPath (Join-Path $work $name) -Destination $public }
    Copy-Item -LiteralPath (Join-Path $work 'initial_contract') -Destination (Join-Path $public 'initial_contract') -Recurse -Force
    $publishedEvidence=@(Get-ChildItem -LiteralPath $public -Recurse -File | Where-Object { $_.Name -ne 'evidence_manifest.json' })
    Save-Json @($publishedEvidence | ForEach-Object { Get-FileRecord $_.FullName }) (Join-Path $public 'evidence_manifest.json')
}
exit $results.exit_code
