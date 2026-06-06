# Reduce a real XTF file to a small, checked-in fixture.
#
# Copies the full file-header block (file header + channel-info blocks, rounded
# up to a 1024-byte boundary, matching XtfReader::readFileHeader) followed by the
# first N ping packets.  Any navigation packets (HeaderType=42) encountered
# before the ping budget is exhausted are kept so nav backfill still works.
#
# Cuts only on exact record boundaries (NumBytesThisRecord), so the output is a
# byte-valid XTF the reader walks without resync.
param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$Pings = 8
)

$bytes = [System.IO.File]::ReadAllBytes($In)
if ($bytes.Length -lt 256) { throw "File too small to be XTF" }
if ($bytes[0] -ne 0x7B)    { throw "Not an XTF file (FileFormat != 0x7B)" }

function U16($off) { return [System.BitConverter]::ToUInt16($bytes, $off) }
function U32($off) { return [System.BitConverter]::ToUInt32($bytes, $off) }

$numSss   = U16 166
$numBathy = U16 168
$numChan  = $numSss + $numBathy

# Header block: round (256 + numChan*128) up to a 1024-byte multiple, min 1024.
$chanBytes   = $numChan * 128
$headerBytes = [math]::Ceiling((256 + $chanBytes) / 1024.0) * 1024
if ($headerBytes -lt 1024) { $headerBytes = 1024 }
$headerBytes = [int]$headerBytes

$acc = New-Object System.Collections.Generic.List[byte]
for ($i = 0; $i -lt $headerBytes -and $i -lt $bytes.Length; $i++) { $acc.Add($bytes[$i]) }

$XTF_MAGIC    = 0xFACE
$PACKET_PING  = 0
$PACKET_NAV   = 42

$offset    = $headerBytes
$pingCount = 0
$navCount  = 0

while ($offset + 256 -le $bytes.Length -and $pingCount -lt $Pings) {
    $magic = U16 $offset
    if ($magic -ne $XTF_MAGIC) { $offset++; continue }   # resync byte-by-byte

    $headerType   = $bytes[$offset + 2]
    $recordBytes  = U32 ($offset + 10)
    if ($recordBytes -lt 256) { break }
    if ($offset + $recordBytes -gt $bytes.Length) { break }   # truncated tail

    $keep = ($headerType -eq $PACKET_PING) -or ($headerType -eq $PACKET_NAV)
    if ($keep) {
        for ($j = 0; $j -lt $recordBytes; $j++) { $acc.Add($bytes[$offset + $j]) }
        if     ($headerType -eq $PACKET_PING) { $pingCount++ }
        elseif ($headerType -eq $PACKET_NAV)  { $navCount++ }
    }
    $offset += $recordBytes
}

[System.IO.File]::WriteAllBytes($Out, $acc.ToArray())

Write-Host ("Reduced {0}" -f [System.IO.Path]::GetFileName($In)) -ForegroundColor Cyan
Write-Host ("  channels={0}  headerBytes={1}" -f $numChan, $headerBytes)
Write-Host ("  ping packets kept={0}  nav packets kept={1}" -f $pingCount, $navCount)
Write-Host ("  output size={0} bytes -> {1}" -f $acc.Count, $Out)
