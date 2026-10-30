param (
	[string] $output
)

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'src\shaders'
$fxc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\fxc.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1

if ( -not $fxc ) {

	Write-Output 'shaders.ps1 : error : fxc.exe not found'
	exit 1

}

New-Item -ItemType Directory -Force $output | Out-Null

$entries = @(
	@( 'ui.hlsl', 'vs_main', 'vs_5_0', 'ui_vs.cso' ),
	@( 'ui.hlsl', 'ps_main', 'ps_5_0', 'ui_ps.cso' ),
	@( 'post.hlsl', 'vs_full', 'vs_5_0', 'full_vs.cso' ),
	@( 'post.hlsl', 'ps_down', 'ps_5_0', 'down_ps.cso' ),
	@( 'post.hlsl', 'ps_up', 'ps_5_0', 'up_ps.cso' ),
	@( 'post.hlsl', 'ps_cover', 'ps_5_0', 'cover_ps.cso' )
)

$failed = $false

foreach ( $entry in $entries ) {

	$file = Join-Path $source $entry[ 0 ]
	$target = Join-Path $output $entry[ 3 ]

	if ( ( Test-Path $target ) -and ( Get-Item $target ).LastWriteTimeUtc -gt ( Get-Item $file ).LastWriteTimeUtc ) { continue }

	$result = & $fxc.FullName /nologo /O3 /T $entry[ 2 ] /E $entry[ 1 ] /Fo $target $file 2>&1

	if ( $LASTEXITCODE -ne 0 ) {

		$result | ForEach-Object { Write-Output "$_" }
		Write-Output "shaders.ps1 : error : $( $entry[ 0 ] ) $( $entry[ 1 ] ) failed"
		$failed = $true

	}

}

if ( $failed ) { exit 1 }

exit 0