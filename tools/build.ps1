param (
	[string] $configuration = 'Release',
	[string] $platform = 'x64',
	[switch] $rebuild,
	[string] $name = ''
)

$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
$target = 'Build'

if ( Test-Path $vswhere ) {

	$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

}

if ( -not $msbuild ) {

	Write-Output 'build failed: MSBuild not found, install Visual Studio 2022 or its Build Tools'
	exit 1

}

if ( $rebuild ) { $target = 'Rebuild' }

$extra = @( )

if ( $name ) { $extra += "/p:TargetName=$name" }

$output = & $msbuild ( Join-Path $root 'pearl.sln' ) /m /nologo /v:minimal /t:$target /p:Configuration=$configuration /p:Platform=$platform @extra

$problems = $output | Where-Object { $_ -match ': (error|warning)' -or $_ -match 'shaders.ps1' } | ForEach-Object { $_.Replace( $root + '\', '' ).Trim( ) } | Select-Object -Unique

$problems | ForEach-Object { Write-Output $_ }

if ( $LASTEXITCODE -ne 0 ) {

	Write-Output "build failed ($configuration $platform)"
	exit 1

}

Write-Output "build ok ($configuration $platform)"