# Builds the app icon (assets/sys-bar.ico) and a transparent logo (docs/images/logo.png)
# from the master artwork docs/images/sys-bar-logo.png (rounded tile on a flat background).
# Usage: pwsh tools/make-icon.ps1
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'docs\images\sys-bar-logo.png'
$icoPath = Join-Path $root 'assets\sys-bar.ico'
$logoPath = Join-Path $root 'docs\images\logo.png'
New-Item -ItemType Directory -Force (Split-Path $icoPath) | Out-Null

# Tile geometry inside the 1254x1254 master (measured once; see git history for the method).
$tile = [System.Drawing.RectangleF]::new(203, 203, 849, 828)
$radius = 167.0

function New-RoundedPath([System.Drawing.RectangleF]$r, [float]$rad) {
    $p = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = $rad * 2
    $p.AddArc($r.X, $r.Y, $d, $d, 180, 90)
    $p.AddArc($r.Right - $d, $r.Y, $d, $d, 270, 90)
    $p.AddArc($r.Right - $d, $r.Bottom - $d, $d, $d, 0, 90)
    $p.AddArc($r.X, $r.Bottom - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    $p
}

# 1) Cut the tile out with an anti-aliased rounded mask (inset by 1.5 px to drop the background fringe).
$master = [System.Drawing.Bitmap]::new($source)
$side = [int][Math]::Ceiling([Math]::Max($tile.Width, $tile.Height))
$cut = [System.Drawing.Bitmap]::new($side, $side, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($cut)
$g.SmoothingMode = 'AntiAlias'
$g.InterpolationMode = 'HighQualityBicubic'
$g.PixelOffsetMode = 'HighQuality'
$g.Clear([System.Drawing.Color]::Transparent)
$offsetY = ($side - $tile.Height) / 2
$inset = 1.5
$maskRect = [System.Drawing.RectangleF]::new($inset, $offsetY + $inset, $tile.Width - 2 * $inset, $tile.Height - 2 * $inset)
$brush = [System.Drawing.TextureBrush]::new($master, [System.Drawing.RectangleF]::new($tile.X, $tile.Y, $tile.Width, $tile.Height))
$brush.TranslateTransform(0, $offsetY)
$g.FillPath($brush, (New-RoundedPath $maskRect ($radius - $inset)))
$g.Dispose()

# 2) Resize with a small transparent margin, like the system icons.
function Resize([System.Drawing.Bitmap]$src, [int]$size, [double]$fill) {
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gr = [System.Drawing.Graphics]::FromImage($bmp)
    $gr.InterpolationMode = 'HighQualityBicubic'
    $gr.PixelOffsetMode = 'HighQuality'
    $gr.SmoothingMode = 'AntiAlias'
    $gr.Clear([System.Drawing.Color]::Transparent)
    $target = $size * $fill
    $o = ($size - $target) / 2
    $gr.DrawImage($src, [System.Drawing.RectangleF]::new($o, $o, $target, $target))
    $gr.Dispose()
    $bmp
}
function ToPng([System.Drawing.Bitmap]$bmp) {
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    , $ms.ToArray()
}

$logo = Resize $cut 512 1.0
$logo.Save($logoPath, [System.Drawing.Imaging.ImageFormat]::Png)

# Small sizes get a slightly larger fill so the bars stay legible.
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = foreach ($s in $sizes) { , (ToPng (Resize $cut $s ($(if ($s -le 24) { 1.0 } else { 0.94 })))) }

$fs = [System.IO.File]::Create($icoPath)
$w = [System.IO.BinaryWriter]::new($fs)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $images[$i].Length
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$offset)
    $offset += $len
}
foreach ($img in $images) { $w.Write($img) }
$w.Close()
$master.Dispose()
Write-Host "Icon: $icoPath"
Write-Host "Logo: $logoPath"
