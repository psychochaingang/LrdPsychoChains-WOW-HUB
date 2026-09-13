<#
    SOAP command runner for TrinityCore/AzerothCore/LegionCore

    Usage:
        .\soap.ps1 -Command "server info"
        .\soap.ps1 -Command "send items MyChar ""Gear"" ""For you"" 6948:1"
        .\soap.ps1 -Command ".lbot team"

    Configure:
        - $SoapUrl : your server SOAP endpoint (must match SOAP.Port in worldserver.conf)
        - $User    : a GM account (user:password)
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Command,

    [string]$SoapUrl = "http://127.0.0.1:7879/",
    [string]$User    = "<GM_USER>:<GM_PASSWORD>"
)

$xml = '<?xml version="1.0" encoding="utf-8"?><SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:TC"><SOAP-ENV:Body><ns1:executeCommand><command>' + $Command + '</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>'

$bodyFile = Join-Path $env:TEMP "soap_body.xml"
[System.IO.File]::WriteAllText($bodyFile, $xml, (New-Object System.Text.UTF8Encoding($false)))

curl.exe -s -X POST $SoapUrl -u $User `
    -H "Content-Type: text/xml; charset=utf-8" `
    -H "SOAPAction: urn:TC#executeCommand" `
    --data-binary "@$bodyFile" --max-time 30
