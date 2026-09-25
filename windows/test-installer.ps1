[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$FailureInstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$LegacyInstallerPath,

    [ValidateRange(1, 300)]
    [int]$ProcessTimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Equal {
    param(
        [AllowNull()]
        [object]$Actual,

        [AllowNull()]
        [object]$Expected,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if ($Actual -ne $Expected) {
        throw "$Description was '$Actual'; expected '$Expected'."
    }
}

function Invoke-BoundedProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$ArgumentList = @(),

        [string]$WorkingDirectory = '',

        [string]$ExpectedErrorText = '',

        [int[]]$ExpectedExitCodes = @(0)
    )

    $parameters = @{
        FilePath = $FilePath
        ArgumentList = $ArgumentList
        PassThru = $true
    }
    if ($WorkingDirectory) {
        $parameters.WorkingDirectory = $WorkingDirectory
    }

    Remove-Item -LiteralPath $env:MEMENTO_INSTALLER_LOG -Force `
        -ErrorAction SilentlyContinue
    $process = Start-Process @parameters
    if (-not $process.WaitForExit($ProcessTimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Process timed out after $ProcessTimeoutSeconds seconds: $FilePath"
    }

    $diagnostic = ''
    if (Test-Path -LiteralPath $env:MEMENTO_INSTALLER_LOG) {
        $diagnostic = [IO.File]::ReadAllText($env:MEMENTO_INSTALLER_LOG)
    }
    if ($ExpectedExitCodes -notcontains $process.ExitCode) {
        throw "Process exited with code $($process.ExitCode), expected $($ExpectedExitCodes -join ', '): $FilePath`n$diagnostic"
    }
    if ($ExpectedErrorText -and -not $diagnostic.Contains($ExpectedErrorText)) {
        throw "Installer failed at an unexpected stage: $diagnostic"
    }
}

function Wait-ForPathState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [bool]$Present,

        [int]$TimeoutSeconds = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ((Test-Path -LiteralPath $Path) -ne $Present -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }

    Assert-Equal `
        -Actual (Test-Path -LiteralPath $Path) `
        -Expected $Present `
        -Description "Path state for $Path"
}

$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
$resolvedFailureInstaller = `
    (Resolve-Path -LiteralPath $FailureInstallerPath).Path
$resolvedLegacyInstaller = `
    (Resolve-Path -LiteralPath $LegacyInstallerPath).Path
$installDirectory = Join-Path $env:ProgramFiles 'Memento Study Edition'
$installedExecutable = Join-Path $installDirectory 'memento.exe'
$installedLogo = Join-Path $installDirectory 'logo.ico'
$ownershipMarker = Join-Path $installDirectory `
    '.memento-study-edition-install'
$ownershipManifest = Join-Path $installDirectory `
    '.memento-study-edition-owned-files'
$residueMarker = Join-Path $installDirectory `
    '.memento-study-edition-residue'
$stageDirectory = Join-Path $installDirectory `
    '.memento-study-edition-stage'
$uninstaller = Join-Path $installDirectory 'uninstall.exe'
$legacyOnlyFile = Join-Path $installDirectory 'legacy-only.dll'
$shortcut = Join-Path $env:ProgramData `
    'Microsoft\Windows\Start Menu\Programs\Memento Study Edition.lnk'
$nativeRegistryPath = `
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\mu0dev Memento Study Edition'
$redirectedRegistryPath = `
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\mu0dev Memento Study Edition'
$uninstallRegistryPaths = @($nativeRegistryPath, $redirectedRegistryPath)

$projectFile = Join-Path $PSScriptRoot '..\CMakeLists.txt'
$projectText = Get-Content -LiteralPath $projectFile -Raw
$versionMatch = [regex]::Match(
    $projectText,
    '(?ms)project\s*\(.*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
)
Assert-True $versionMatch.Success 'Could not determine the CMake project version.'
$expectedVersion = $versionMatch.Groups[1].Value
$versionParts = $expectedVersion.Split('.')

function Assert-InstalledState {
    Assert-True `
        (Test-Path -LiteralPath $installedExecutable -PathType Leaf) `
        "The installer did not create the application: $installedExecutable"
    Assert-True `
        (Test-Path -LiteralPath $uninstaller -PathType Leaf) `
        "The installer did not create its uninstaller: $uninstaller"
    Assert-True `
        (Test-Path -LiteralPath $installedLogo -PathType Leaf) `
        "The installer did not create its icon: $installedLogo"
    Assert-True `
        (Test-Path -LiteralPath $shortcut -PathType Leaf) `
        "The installer did not create its Start Menu shortcut: $shortcut"
    Assert-True `
        (Test-Path -LiteralPath $ownershipMarker -PathType Leaf) `
        "The installer did not create its ownership marker: $ownershipMarker"
    Assert-True `
        (Test-Path -LiteralPath $ownershipManifest -PathType Leaf) `
        "The installer did not create its ownership manifest: $ownershipManifest"
    Assert-True `
        (Test-Path -LiteralPath $residueMarker -PathType Leaf) `
        "The installer did not create its residue marker: $residueMarker"
    Assert-True `
        (-not (Test-Path -LiteralPath $stageDirectory)) `
        "The installer left its transaction stage behind: $stageDirectory"

    $markerContents = [IO.File]::ReadAllText($ownershipMarker)
    Assert-Equal `
        -Actual $markerContents `
        -Expected "Memento Study Edition installed v1`r`n" `
        -Description 'Ownership marker contents'
    $residueContents = [IO.File]::ReadAllText($residueMarker)
    Assert-Equal `
        -Actual $residueContents `
        -Expected "Memento Study Edition directory v1`r`n" `
        -Description 'Residue marker contents'

    $presentRegistryPaths = @(
        $uninstallRegistryPaths |
            Where-Object { Test-Path -LiteralPath $_ }
    )
    Assert-Equal `
        -Actual $presentRegistryPaths.Count `
        -Expected 1 `
        -Description 'Number of uninstall registry entries'
    Assert-Equal `
        -Actual $presentRegistryPaths[0] `
        -Expected $nativeRegistryPath `
        -Description 'Uninstall registry view'

    $metadata = Get-ItemProperty -LiteralPath $nativeRegistryPath
    Assert-Equal `
        $metadata.DisplayName `
        'Memento Study Edition - A Japanese study player with Jimaku and single-episode torrent streaming.' `
        'DisplayName'
    Assert-Equal `
        $metadata.UninstallString `
        ('"' + $uninstaller + '"') `
        'UninstallString'
    Assert-Equal `
        $metadata.QuietUninstallString `
        ('"' + $uninstaller + '" /S') `
        'QuietUninstallString'
    Assert-Equal $metadata.InstallLocation $installDirectory 'InstallLocation'
    Assert-Equal $metadata.DisplayIcon $installedLogo 'DisplayIcon'
    Assert-Equal $metadata.Publisher 'mu0dev' 'Publisher'
    Assert-Equal $metadata.DisplayVersion $expectedVersion 'DisplayVersion'
    Assert-Equal `
        $metadata.HelpLink `
        'https://github.com/OozoraGokuu/memento-custom/blob/main/README.md' `
        'HelpLink'
    Assert-Equal `
        $metadata.URLUpdateInfo `
        'https://github.com/OozoraGokuu/memento-custom/releases' `
        'URLUpdateInfo'
    Assert-Equal `
        $metadata.URLInfoAbout `
        'https://github.com/OozoraGokuu/memento-custom' `
        'URLInfoAbout'
    Assert-Equal `
        ([int]$metadata.VersionMajor) `
        ([int]$versionParts[0]) `
        'VersionMajor'
    Assert-Equal `
        ([int]$metadata.VersionMinor) `
        ([int]$versionParts[1]) `
        'VersionMinor'
    Assert-Equal ([int]$metadata.NoModify) 1 'NoModify'
    Assert-Equal ([int]$metadata.NoRepair) 1 'NoRepair'
    Assert-Equal ([int]$metadata.EstimatedSize) 240000 'EstimatedSize'
}

function Assert-UninstalledMetadata {
    Assert-True `
        (-not (Test-Path -LiteralPath $shortcut)) `
        "Uninstall left the Start Menu shortcut behind: $shortcut"
    foreach ($registryPath in $uninstallRegistryPaths) {
        Assert-True `
            (-not (Test-Path -LiteralPath $registryPath)) `
            "Uninstall left registry state behind: $registryPath"
    }
}

function Assert-ResidualState {
    param(
        [switch]$AllowUnknownUninstaller
    )

    Assert-True `
        (-not (Test-Path -LiteralPath $installedExecutable)) `
        'Residual state retained the application executable.'
    Assert-True `
        (-not (Test-Path -LiteralPath $installedLogo)) `
        'Residual state retained the application icon.'
    if (-not $AllowUnknownUninstaller) {
        Assert-True `
            (-not (Test-Path -LiteralPath $uninstaller)) `
            'Residual state retained the installed uninstaller.'
    }
    Assert-True `
        (-not (Test-Path -LiteralPath $ownershipMarker)) `
        'Residual state retained the installed-state marker.'
    Assert-True `
        (-not (Test-Path -LiteralPath $ownershipManifest)) `
        'Residual state retained the installed ownership manifest.'
    Assert-True `
        (Test-Path -LiteralPath $residueMarker -PathType Leaf) `
        'Residual state is missing its safe-reinstall marker.'
    Assert-Equal `
        -Actual ([IO.File]::ReadAllText($residueMarker)) `
        -Expected "Memento Study Edition directory v1`r`n" `
        -Description 'Residual marker contents'
    Assert-True `
        (-not (Test-Path -LiteralPath $stageDirectory)) `
        'Residual state retained an installer transaction stage.'
    Assert-UninstalledMetadata
}

$testRoot = Join-Path $env:SystemDrive "MementoInstallerAudit-$PID"
$overrideDirectory = Join-Path $testRoot 'override-target'
$movedDirectory = Join-Path $testRoot 'moved-uninstaller'
$userSentinel = Join-Path $installDirectory 'user-owned.keep'
$failure = $null
$runningApplication = $null
$previousDiagnosticPath = $env:MEMENTO_INSTALLER_LOG
$env:MEMENTO_INSTALLER_LOG = Join-Path $testRoot 'installer-errors.log'

if (Test-Path -LiteralPath $installDirectory) {
    throw "Refusing to overwrite a pre-existing installation: $installDirectory"
}
if (Test-Path -LiteralPath $shortcut) {
    throw "Refusing to overwrite a pre-existing shortcut: $shortcut"
}
foreach ($registryPath in $uninstallRegistryPaths) {
    if (Test-Path -LiteralPath $registryPath) {
        throw "Refusing to overwrite pre-existing registry state: $registryPath"
    }
}
if (Test-Path -LiteralPath $testRoot) {
    throw "Refusing to reuse the installer audit directory: $testRoot"
}

function Invoke-SynchronousInstalledUninstaller {
    $copiedUninstaller = Join-Path $testRoot `
        "uninstall-test-$([Guid]::NewGuid().ToString('N')).exe"
    Copy-Item -LiteralPath $uninstaller -Destination $copiedUninstaller
    try {
        Invoke-BoundedProcess `
            -FilePath $copiedUninstaller `
            -ArgumentList @('/S', "_?=$installDirectory")
    }
    finally {
        Remove-Item -LiteralPath $copiedUninstaller -Force `
            -ErrorAction SilentlyContinue
    }
}

function Invoke-NormalInstalledUninstaller {
    Invoke-BoundedProcess `
        -FilePath $uninstaller `
        -ArgumentList '/S'
    Wait-ForPathState -Path $ownershipMarker -Present $false
    Wait-ForPathState -Path $uninstaller -Present $false
}

function Get-InstalledFileSnapshot {
    @(
        Get-ChildItem -LiteralPath $installDirectory -File -Force -Recurse |
            ForEach-Object {
                $relative = $_.FullName.Substring($installDirectory.Length)
                $hash = (Get-FileHash -LiteralPath $_.FullName `
                    -Algorithm SHA256).Hash
                "$relative`t$hash"
            } |
            Sort-Object
    ) -join "`n"
}

function Get-UninstallRegistrySnapshot {
    $key = Get-Item -LiteralPath $nativeRegistryPath
    $entries = @(
        foreach ($name in ($key.GetValueNames() | Sort-Object)) {
            $kind = $key.GetValueKind($name).ToString()
            $value = $key.GetValue($name, $null, `
                [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($value -is [byte[]]) {
                $value = [Convert]::ToBase64String($value)
            }
            elseif ($value -is [array]) {
                $value = $value -join "`u{001f}"
            }
            "$name`t$kind`t$value"
        }
        foreach ($name in ($key.GetSubKeyNames() | Sort-Object)) {
            "SUBKEY`t$name"
        }
    )
    return $entries -join "`n"
}

try {
    New-Item -ItemType Directory -Path $overrideDirectory | Out-Null
    $overrideSentinel = Join-Path $overrideDirectory 'do-not-delete.txt'
    Set-Content -LiteralPath $overrideSentinel -Value 'not installer-owned'

    Write-Host 'Verifying rollback after an injected late install failure...'
    Invoke-BoundedProcess `
        -FilePath $resolvedFailureInstaller `
        -ArgumentList '/S' `
        -ExpectedErrorText 'All newly installed files were rolled back' `
        -ExpectedExitCodes 2
    Assert-True `
        (-not (Test-Path -LiteralPath $installDirectory)) `
        'A failed clean install left its installation directory behind.'
    Assert-UninstalledMetadata

    Write-Host 'Refusing to overwrite an unrelated Start Menu shortcut...'
    Set-Content -LiteralPath $shortcut -Value 'unrelated shortcut data'
    $shortcutHash = (Get-FileHash -LiteralPath $shortcut -Algorithm SHA256).Hash
    Invoke-BoundedProcess `
        -FilePath $resolvedInstaller `
        -ArgumentList '/S' `
        -ExpectedExitCodes 2
    Assert-Equal `
        -Actual (Get-FileHash -LiteralPath $shortcut -Algorithm SHA256).Hash `
        -Expected $shortcutHash `
        -Description 'Unrelated shortcut hash after refused install'
    Assert-True `
        (-not (Test-Path -LiteralPath $installDirectory)) `
        'Shortcut-collision refusal created an installation directory.'
    Remove-Item -LiteralPath $shortcut -Force

    Write-Host 'Refusing to claim an existing empty uninstall registry key...'
    New-Item -Path $nativeRegistryPath -Force | Out-Null
    Invoke-BoundedProcess `
        -FilePath $resolvedInstaller `
        -ArgumentList '/S' `
        -ExpectedExitCodes 2
    Assert-True `
        (Test-Path -LiteralPath $nativeRegistryPath) `
        'Installer removed an unrelated empty registry key.'
    $emptyCollisionKey = Get-Item -LiteralPath $nativeRegistryPath
    Assert-Equal `
        -Actual ($emptyCollisionKey.GetValueNames().Count) `
        -Expected 0 `
        -Description 'Value count in unrelated empty registry key'
    Assert-Equal `
        -Actual ($emptyCollisionKey.GetSubKeyNames().Count) `
        -Expected 0 `
        -Description 'Subkey count in unrelated empty registry key'
    Assert-True `
        (-not (Test-Path -LiteralPath $installDirectory)) `
        'Empty-registry-key collision created an installation directory.'
    Remove-Item -LiteralPath $nativeRegistryPath -Force

    Write-Host 'Refusing to overwrite an unrelated uninstall registry key...'
    New-Item -Path $nativeRegistryPath -Force | Out-Null
    Set-ItemProperty `
        -LiteralPath $nativeRegistryPath `
        -Name DisplayName `
        -Value 'Unrelated application'
    Invoke-BoundedProcess `
        -FilePath $resolvedInstaller `
        -ArgumentList '/S' `
        -ExpectedExitCodes 2
    Assert-Equal `
        -Actual (Get-ItemProperty -LiteralPath $nativeRegistryPath).DisplayName `
        -Expected 'Unrelated application' `
        -Description 'Unrelated registry value after refused install'
    Assert-True `
        (-not (Test-Path -LiteralPath $installDirectory)) `
        'Registry-collision refusal created an installation directory.'
    Remove-Item -LiteralPath $nativeRegistryPath -Recurse -Force

    Write-Host 'Installing into an empty pre-created canonical directory...'
    New-Item -ItemType Directory -Path $installDirectory | Out-Null

    Write-Host 'Installing with an attempted /D override...'
    Invoke-BoundedProcess `
        -FilePath $resolvedLegacyInstaller `
        -ArgumentList @('/S', "/D=$overrideDirectory")
    Assert-InstalledState
    Assert-True `
        (Test-Path -LiteralPath $legacyOnlyFile -PathType Leaf) `
        'The synthetic previous-version payload was not installed.'
    Assert-True `
        (Test-Path -LiteralPath $overrideSentinel -PathType Leaf) `
        'The installer modified the /D override target.'
    Assert-True `
        (-not (Test-Path -LiteralPath `
            (Join-Path $overrideDirectory 'memento.exe'))) `
        'The installer honored an unsafe /D override.'

    Write-Host 'Running the installed application smoke test...'
    Invoke-BoundedProcess `
        -FilePath $installedExecutable `
        -ArgumentList '--smoke-test' `
        -WorkingDirectory $installDirectory

    Write-Host 'Verifying that a moved uninstaller cannot remove files...'
    New-Item -ItemType Directory -Path $movedDirectory | Out-Null
    $movedUninstaller = Join-Path $movedDirectory 'uninstall.exe'
    Copy-Item -LiteralPath $uninstaller -Destination $movedUninstaller
    Set-Content `
        -LiteralPath (Join-Path $movedDirectory `
            '.memento-study-edition-install') `
        -Value 'Memento Study Edition installed v1'
    Set-Content `
        -LiteralPath (Join-Path $movedDirectory `
            '.memento-study-edition-residue') `
        -Value 'Memento Study Edition directory v1'
    Invoke-BoundedProcess `
        -FilePath $movedUninstaller `
        -ArgumentList @('/S', "_?=$movedDirectory") `
        -ExpectedExitCodes 2
    Assert-InstalledState
    Assert-True `
        (Test-Path -LiteralPath $movedUninstaller -PathType Leaf) `
        'The moved uninstaller removed itself despite the path guard.'

    Write-Host 'Verifying that a running application blocks an upgrade...'
    $executableHashBeforeBlockedUpgrade = `
        (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash
    $inventoryBeforeBlockedUpgrade = @(
        Get-ChildItem -LiteralPath $installDirectory -File -Force -Recurse |
            ForEach-Object { $_.FullName.Substring($installDirectory.Length) } |
            Sort-Object
    )
    $logoHashBeforeUpgrade = `
        (Get-FileHash -LiteralPath $installedLogo -Algorithm SHA256).Hash
    $runningApplication = Start-Process `
        -FilePath $installedExecutable `
        -WorkingDirectory $installDirectory `
        -PassThru
    Start-Sleep -Seconds 2
    Assert-True `
        (-not $runningApplication.HasExited) `
        'Memento exited before the locked-file upgrade check.'
    try {
        Invoke-BoundedProcess `
            -FilePath $resolvedInstaller `
            -ArgumentList '/S' `
            -ExpectedExitCodes 2
    }
    finally {
        if (-not $runningApplication.HasExited) {
            [void]$runningApplication.CloseMainWindow()
            if (-not $runningApplication.WaitForExit(5000)) {
                $runningApplication.Kill()
                $runningApplication.WaitForExit()
            }
        }
        $runningApplication.Dispose()
        $runningApplication = $null
        Start-Sleep -Milliseconds 500
    }
    Assert-InstalledState
    Assert-Equal `
        (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash `
        $executableHashBeforeBlockedUpgrade `
        'Executable hash after blocked upgrade'
    $inventoryAfterBlockedUpgrade = @(
        Get-ChildItem -LiteralPath $installDirectory -File -Force -Recurse |
            ForEach-Object { $_.FullName.Substring($installDirectory.Length) } |
            Sort-Object
    )
    Assert-Equal `
        ($inventoryAfterBlockedUpgrade -join "`n") `
        ($inventoryBeforeBlockedUpgrade -join "`n") `
        'Installed file inventory after blocked upgrade'

    Write-Host 'Restoring an exact older-version install after a late failure...'
    [IO.File]::WriteAllText($userSentinel, 'preserve this user-owned file')
    [IO.File]::WriteAllText($installedLogo, 'legacy-owned-icon-bytes')
    $legacyFilesBeforeFailure = Get-InstalledFileSnapshot
    $legacyShortcutBeforeFailure = `
        (Get-FileHash -LiteralPath $shortcut -Algorithm SHA256).Hash
    $legacyRegistryBeforeFailure = Get-UninstallRegistrySnapshot
    Invoke-BoundedProcess `
        -FilePath $resolvedFailureInstaller `
        -ArgumentList '/S' `
        -ExpectedErrorText 'All newly installed files were rolled back' `
        -ExpectedExitCodes 2
    Assert-InstalledState
    Assert-Equal `
        -Actual (Get-InstalledFileSnapshot) `
        -Expected $legacyFilesBeforeFailure `
        -Description 'Previous-version files and hashes after failed upgrade'
    Assert-Equal `
        -Actual (Get-FileHash -LiteralPath $shortcut -Algorithm SHA256).Hash `
        -Expected $legacyShortcutBeforeFailure `
        -Description 'Previous-version shortcut after failed upgrade'
    Assert-Equal `
        -Actual (Get-UninstallRegistrySnapshot) `
        -Expected $legacyRegistryBeforeFailure `
        -Description 'Previous-version registry after failed upgrade'
    Assert-True `
        (Test-Path -LiteralPath $legacyOnlyFile -PathType Leaf) `
        'Failed upgrade did not restore the previous-version-only file.'
    Invoke-BoundedProcess `
        -FilePath $installedExecutable `
        -ArgumentList '--smoke-test' `
        -WorkingDirectory $installDirectory

    Write-Host 'Completing the cross-version upgrade and removing old files...'
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-True `
        (-not (Test-Path -LiteralPath $legacyOnlyFile)) `
        'Successful upgrade retained a previous-version-only file.'
    Assert-True `
        (-not ((Get-Content -LiteralPath $ownershipManifest) `
            -contains 'legacy-only.dll')) `
        'Current ownership manifest retained a previous-version-only path.'
    Assert-Equal `
        (Get-FileHash -LiteralPath $installedLogo -Algorithm SHA256).Hash `
        $logoHashBeforeUpgrade `
        'Current-version icon hash'
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after upgrade'

    Write-Host 'Restoring the runnable prior install after a late upgrade failure...'
    [IO.File]::WriteAllText($installedLogo, 'prior-version-owned-content')
    $logoHashBeforeFailedUpgrade = `
        (Get-FileHash -LiteralPath $installedLogo -Algorithm SHA256).Hash
    $executableHashBeforeFailedUpgrade = `
        (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash
    $inventoryBeforeFailedUpgrade = @(
        Get-ChildItem -LiteralPath $installDirectory -File -Force -Recurse |
            ForEach-Object { $_.FullName.Substring($installDirectory.Length) } |
            Sort-Object
    )
    Invoke-BoundedProcess `
        -FilePath $resolvedFailureInstaller `
        -ArgumentList '/S' `
        -ExpectedErrorText 'All newly installed files were rolled back' `
        -ExpectedExitCodes 2
    Assert-InstalledState
    Assert-Equal `
        -Actual (Get-FileHash -LiteralPath $installedLogo -Algorithm SHA256).Hash `
        -Expected $logoHashBeforeFailedUpgrade `
        -Description 'Prior icon hash after failed installed upgrade'
    Assert-Equal `
        -Actual (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash `
        -Expected $executableHashBeforeFailedUpgrade `
        -Description 'Prior executable hash after failed installed upgrade'
    $inventoryAfterFailedUpgrade = @(
        Get-ChildItem -LiteralPath $installDirectory -File -Force -Recurse |
            ForEach-Object { $_.FullName.Substring($installDirectory.Length) } |
            Sort-Object
    )
    Assert-Equal `
        -Actual ($inventoryAfterFailedUpgrade -join "`n") `
        -Expected ($inventoryBeforeFailedUpgrade -join "`n") `
        -Description 'Installed inventory after failed installed upgrade'
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after failed installed upgrade'
    Invoke-BoundedProcess `
        -FilePath $installedExecutable `
        -ArgumentList '--smoke-test' `
        -WorkingDirectory $installDirectory
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-Equal `
        (Get-FileHash -LiteralPath $installedLogo -Algorithm SHA256).Hash `
        $logoHashBeforeUpgrade `
        'Icon hash after successful retry'

    Write-Host 'Recovering a cross-version marker-only partial uninstall...'
    Invoke-BoundedProcess -FilePath $resolvedLegacyInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-True `
        (Test-Path -LiteralPath $legacyOnlyFile -PathType Leaf) `
        'Synthetic previous-version file was not present before partial recovery.'
    Remove-Item -LiteralPath $uninstaller -Force
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-True `
        (-not (Test-Path -LiteralPath $legacyOnlyFile)) `
        'Partial cross-version recovery retained an old owned file.'
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after partial-uninstall recovery'

    Write-Host 'Upgrading successfully when uninstall registry data is absent...'
    Remove-Item -LiteralPath $nativeRegistryPath -Recurse -Force
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after registry-less upgrade'

    Write-Host 'Running the installed uninstaller through its normal self-copy path...'
    Invoke-NormalInstalledUninstaller
    Assert-ResidualState
    Assert-True `
        (Test-Path -LiteralPath $userSentinel -PathType Leaf) `
        'Uninstall removed a user-added file.'
    $remainingEntries = @(
        Get-ChildItem -LiteralPath $installDirectory -Force
    )
    Assert-Equal `
        -Actual $remainingEntries.Count `
        -Expected 2 `
        -Description 'Entries retained after uninstall'

    Write-Host 'Refusing an unknown uninstaller in a residual directory...'
    [IO.File]::WriteAllText($uninstaller, 'not an installer-owned executable')
    $unknownUninstallerHash = `
        (Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash
    Invoke-BoundedProcess `
        -FilePath $resolvedInstaller `
        -ArgumentList '/S' `
        -ExpectedExitCodes 2
    Assert-Equal `
        -Actual (Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash `
        -Expected $unknownUninstallerHash `
        -Description 'Unknown residual uninstaller hash after refused install'
    Assert-ResidualState -AllowUnknownUninstaller
    Remove-Item -LiteralPath $uninstaller -Force

    Write-Host 'Rolling back a failed reinstall without touching residual files...'
    Invoke-BoundedProcess `
        -FilePath $resolvedFailureInstaller `
        -ArgumentList '/S' `
        -ExpectedExitCodes 2
    Assert-ResidualState
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after failed residual reinstall'

    Write-Host 'Reinstalling over a marked residual directory...'
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Assert-Equal `
        ([IO.File]::ReadAllText($userSentinel)) `
        'preserve this user-owned file' `
        'User-owned file after residual reinstall'

    Write-Host 'Uninstalling successfully when registry data is already absent...'
    Remove-Item -LiteralPath $nativeRegistryPath -Recurse -Force
    Invoke-NormalInstalledUninstaller
    Assert-ResidualState
    Assert-True `
        (Test-Path -LiteralPath $userSentinel -PathType Leaf) `
        'Registry-less uninstall removed a user-added file.'

    Write-Host 'Verifying a final complete install and normal self-deleting uninstall...'
    Invoke-BoundedProcess -FilePath $resolvedInstaller -ArgumentList '/S'
    Assert-InstalledState
    Remove-Item -LiteralPath $userSentinel -Force
    Invoke-NormalInstalledUninstaller
    Wait-ForPathState -Path $installDirectory -Present $false
    Assert-UninstalledMetadata
}
catch {
    $failure = $_
}
finally {
    if ($null -ne $runningApplication) {
        try {
            if (-not $runningApplication.HasExited) {
                $runningApplication.Kill()
                $runningApplication.WaitForExit()
            }
            $runningApplication.Dispose()
        }
        catch {
            Write-Error $_ -ErrorAction Continue
        }
    }

    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
        try {
            Invoke-SynchronousInstalledUninstaller
        }
        catch {
            Write-Error $_ -ErrorAction Continue
        }
    }
    if (Test-Path -LiteralPath $userSentinel -PathType Leaf) {
        Remove-Item -LiteralPath $userSentinel -Force `
            -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $residueMarker -PathType Leaf) {
        Remove-Item -LiteralPath $residueMarker -Force `
            -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $shortcut -PathType Leaf) {
        Remove-Item -LiteralPath $shortcut -Force `
            -ErrorAction SilentlyContinue
    }
    foreach ($registryPath in $uninstallRegistryPaths) {
        if (Test-Path -LiteralPath $registryPath) {
            Remove-Item -LiteralPath $registryPath -Recurse -Force `
                -ErrorAction SilentlyContinue
        }
    }
    if (Test-Path -LiteralPath $installDirectory) {
        Remove-Item -LiteralPath $installDirectory -Force `
            -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
    $env:MEMENTO_INSTALLER_LOG = $previousDiagnosticPath
}

if ($null -ne $failure) {
    throw $failure
}

Write-Host 'Installer safety, upgrade, metadata, startup, and uninstall tests passed.'
