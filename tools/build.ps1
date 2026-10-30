param (
	[string] $configuration = 'Release',
	[switch] $rebuild,
	[string] $name = ''
)

$root = Split-Path -Parent $PSScriptRoot
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$target = 'Build'

if ( $rebuild ) { $target = 'Rebuild' }

$extra = @( )

if ( $name ) { $extra += "/p:TargetName=$name" }

$output = & $msbuild ( Join-Path $root 'pearl.sln' ) /m /nologo /v:minimal /t:$target /p:Configuration=$configuration /p:Platform=x64 @extra

$problems = $output | Where-Object { $_ -match ': (error|warning)' -or $_ -match 'shaders.ps1' } | ForEach-Object { $_.Replace( $root + '\', '' ).Trim( ) } | Select-Object -Unique

$problems | ForEach-Object { Write-Output $_ }

if ( $LASTEXITCODE -ne 0 ) {

	Write-Output "build failed ($configuration)"
	exit 1

}

Write-Output "build ok ($configuration)"