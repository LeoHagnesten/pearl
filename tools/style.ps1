$root = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem -Path ( Join-Path $root 'src' ) -Recurse -Include *.cc, *.hh
$problems = 0

foreach ( $file in $files ) {

	$text = [System.IO.File]::ReadAllText( $file.FullName )
	$name = $file.FullName.Replace( "$root\", '' )
	$bytes = [System.IO.File]::ReadAllBytes( $file.FullName )

	if ( $bytes.Length -ge 3 -and $bytes[ 0 ] -eq 0xEF -and $bytes[ 1 ] -eq 0xBB ) { Write-Output "$name : byte order mark"; $problems++ }

	if ( $text.Contains( "`r" ) ) { Write-Output "$name : CRLF"; $problems++ }

	if ( $text.EndsWith( "`n" ) ) { Write-Output "$name : trailing newline at end"; $problems++ }

	$lines = $text.Split( "`n" )

	for ( $i = 0; $i -lt $lines.Length; $i++ ) {

		$line = $lines[ $i ]
		$code = [regex]::Replace( $line, '"([^"\\]|\\.)*"', '""' )
		$code = [regex]::Replace( $code, "'([^'\\]|\\.)*'", "''" )
		$where = "$name`:$( $i + 1 )"

		if ( $line -match '[ \t]+$' ) { Write-Output "$where : trailing whitespace"; $problems++ }

		if ( $code -match '//' -or $code -match '/\*' ) { Write-Output "$where : comment"; $problems++ }

		if ( $code -match '^\s* {2,}\S' ) { Write-Output "$where : space indent"; $problems++ }

		if ( $code -match '\([^ )\r\n]' ) { Write-Output "$where : no space after ("; $problems++ }

		if ( $code -match '[^ (\t]\)' ) { Write-Output "$where : no space before )"; $problems++ }

		if ( $code -match '<[A-Za-z_][^<>]*[^ <>]>' -and $code -match '(template|static_cast|reinterpret_cast|const_cast|list|span) ?<[^ ]' ) { Write-Output "$where : no space inside < >"; $problems++ }

		if ( $code -match '\bnamespace\s*\{' ) { Write-Output "$where : anonymous namespace"; $problems++ }

		if ( $code -match '\b[a-z0-9_]+_t\b' -and $code -notmatch '\b(u?int(8|16|32|64)|size|wchar|ptrdiff|uintptr|intptr|char(8|16|32))_t\b' ) { Write-Output "$where : _t suffix"; $problems++ }

		if ( $code -match '\bm_[a-z]' ) { Write-Output "$where : m_ prefix"; $problems++ }

	}

}

Write-Output "style: checked $( @( $files ).Count ) files, $problems problems"