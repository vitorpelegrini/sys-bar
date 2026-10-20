# Rebuilds the README banner: the artwork (assets/banner-base.png: background, logo, wordmark, taskbar) with
# the real hover card, as rendered by the tests (build/previews/banner-card.png, transparent with its
# shadow), laid over the card area. Run after any visual change to the card.
# Usage: pwsh ./build.ps1; pwsh tools/update-banner.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$base = Join-Path $root 'assets/banner-base.png'
$card = Join-Path $root 'build/previews/banner-card.png'
$out = Join-Path $root 'docs/images/sys-bar-banner.png'
if (-not (Test-Path $card)) { throw "missing $card - run pwsh ./build.ps1 first" }

Add-Type -AssemblyName System.Drawing
$canvas = [Drawing.Bitmap]::new($base)
$overlay = [Drawing.Image]::FromFile($card)
try {
    $g = [Drawing.Graphics]::FromImage($canvas)
    $g.CompositingQuality = 'HighQuality'
    $g.InterpolationMode = 'HighQualityBicubic'
    # Centered where the artwork's card is (1312, 409 in the 1774x887 artwork), covering it entirely.
    $cx = 1312 * $canvas.Width / 1774
    $cy = 409 * $canvas.Height / 887
    $x = [int]($cx - $overlay.Width / 2)
    $y = [int]($cy - $overlay.Height / 2)
    $g.DrawImage($overlay, $x, $y, $overlay.Width, $overlay.Height)
    $g.Dispose()
    $canvas.Save($out, [Drawing.Imaging.ImageFormat]::Png)
    Write-Host "docs/images/sys-bar-banner.png (card at $x,$y, $($overlay.Width)x$($overlay.Height))"
} finally {
    $overlay.Dispose()
    $canvas.Dispose()
}
