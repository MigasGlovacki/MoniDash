param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$required = @('schema_version', 'event_type', 'timestamp_ms', 'monotonic_seconds')
$lineNumber = 0
Get-Content -LiteralPath $Path | ForEach-Object {
    $lineNumber++
    try { $record = $_ | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Invalid JSON at line ${lineNumber}: $($_.Exception.Message)" }
    foreach ($field in $required) {
        if ($null -eq $record.$field) { throw "Missing '$field' at line $lineNumber" }
    }
}

Write-Output "Valid JSONL telemetry: $lineNumber records"
