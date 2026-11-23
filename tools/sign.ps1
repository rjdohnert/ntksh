[CmdletBinding()]
param(
    [string[]]$Files,
    [string]$CertSubject = "CN=PC-OpenSystems ntksh Code Signing",
    [string]$PfxPath,
    [string]$Password = "ntksh2026"
)

$ErrorActionPreference = 'Stop'

if (-not $PfxPath) {
    $PfxPath = Join-Path $PSScriptRoot 'ntksh_dev.pfx'
}

$rootDir = Resolve-Path (Join-Path $PSScriptRoot '..')
if (-not $Files -or $Files.Count -eq 0) {
    $Files = @((Join-Path $rootDir 'bin\ksh.exe'), (Join-Path $rootDir 'ksh.exe'))
}

$cert = $null
if (Test-Path $PfxPath) {
    try {
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($PfxPath, $Password, [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable)
        Write-Host "[SIGN] Loaded existing certificate from $PfxPath" -ForegroundColor Cyan
    } catch {
        $cert = $null
    }
}

if (-not $cert) {
    Write-Host "[SIGN] Generating in-memory Code Signing Certificate for '$CertSubject'..." -ForegroundColor Cyan
    $rsa = [System.Security.Cryptography.RSA]::Create(2048)
    $req = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest(
        $CertSubject,
        $rsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
    )

    # Key Usage: DigitalSignature
    $keyUsage = New-Object System.Security.Cryptography.X509Certificates.X509KeyUsageExtension(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,
        $true
    )
    $req.CertificateExtensions.Add($keyUsage)

    # EKU: Code Signing (1.3.6.1.5.5.7.3.3)
    $ekuOidCollection = New-Object System.Security.Cryptography.OidCollection
    [void]$ekuOidCollection.Add((New-Object System.Security.Cryptography.Oid("1.3.6.1.5.5.7.3.3", "Code Signing")))
    $eku = New-Object System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension($ekuOidCollection, $true)
    $req.CertificateExtensions.Add($eku)

    $notBefore = [DateTimeOffset]::UtcNow.AddDays(-1)
    $notAfter = [DateTimeOffset]::UtcNow.AddYears(5)
    $rawCert = $req.CreateSelfSigned($notBefore, $notAfter)

    # Export to PFX and reimport with private key so Set-AuthenticodeSignature can sign
    $pfxBytes = $rawCert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $Password)
    [System.IO.File]::WriteAllBytes($PfxPath, $pfxBytes)
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($PfxPath, $Password, [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable)
    Write-Host "[SIGN] Certificate created and saved to $PfxPath" -ForegroundColor Green
}

foreach ($file in $Files) {
    if (Test-Path $file) {
        Write-Host "[SIGN] Signing $file with Authenticode..." -ForegroundColor Cyan
        $sig = Set-AuthenticodeSignature -FilePath $file -Certificate $cert -HashAlgorithm SHA256
        Write-Host "[SIGN] Result for $(Split-Path $file -Leaf): Status=$($sig.Status), Algorithm=$($sig.SignerCertificate.SignatureAlgorithm.FriendlyName)" -ForegroundColor Green
    }
}
