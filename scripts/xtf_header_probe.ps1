# Read-only XTF file-header inspector.
# Parses the first 256-byte XtfFileHeader and the first few XtfChanInfo blocks.
param([Parameter(Mandatory=$true)][string]$Path)

$fs = [System.IO.File]::OpenRead($Path)
try {
    $buf = New-Object byte[] 1024
    $n = $fs.Read($buf, 0, 1024)
    if ($n -lt 256) { Write-Host "File too small"; return }

    function Str($off, $len) {
        $s = [System.Text.Encoding]::ASCII.GetString($buf, $off, $len)
        return ($s -replace "\0.*$","").Trim()
    }
    function U16($off) { return [System.BitConverter]::ToUInt16($buf, $off) }
    function F32($off) { return [System.BitConverter]::ToSingle($buf, $off) }

    Write-Host "=== $([System.IO.Path]::GetFileName($Path)) ===" -ForegroundColor Cyan
    Write-Host ("FileFormat        : 0x{0:X2} (expect 0x7B)" -f $buf[0])
    Write-Host ("SystemType        : {0}" -f $buf[1])
    Write-Host ("RecProgramName    : {0}" -f (Str 2 8))
    Write-Host ("RecProgramVersion : {0}" -f (Str 10 8))
    Write-Host ("SonarName         : {0}" -f (Str 18 16))
    Write-Host ("SonarType         : {0}" -f (U16 34))
    Write-Host ("NoteString        : {0}" -f (Str 36 64))
    Write-Host ("ThisFileName      : {0}" -f (Str 100 64))
    $navUnits = U16 164
    $numSss   = U16 166
    $numBathy = U16 168
    Write-Host ("NavUnits          : {0} (0/1=geo, 3=projected)" -f $navUnits)
    Write-Host ("NumSonarChannels  : {0}" -f $numSss)
    Write-Host ("NumBathyChannels  : {0}" -f $numBathy)

    $total = $numSss + $numBathy
    if ($total -gt 8) { $total = 8 }
    for ($c = 0; $c -lt $total; $c++) {
        $base = 256 + $c * 128
        if ($base + 128 -gt $n) { break }
        $type = $buf[$base]
        $sub  = $buf[$base + 1]
        $bps  = U16 ($base + 6)
        $spc  = [System.BitConverter]::ToUInt32($buf, $base + 8)
        $name = Str ($base + 12) 16
        $freq = F32 ($base + 32)
        Write-Host ("  chan[{0}] type={1} sub={2} bps={3} spc={4} freq={5} name='{6}'" -f `
            $c, $type, $sub, $bps, $spc, $freq, $name)
    }
} finally {
    $fs.Close()
}
