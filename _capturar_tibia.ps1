# Captura la ventana de un cliente de Tibia.
#
#   .\_capturar_tibia.ps1                    -> 1 foto del cliente OFICIAL
#   .\_capturar_tibia.ps1 -Veces 5 -Cada 3   -> 5 fotos, una cada 3 segundos
#   .\_capturar_tibia.ps1 -Proceso otclient  -> captura el nuestro en vez del oficial
#
# El cliente oficial esta acelerado por GPU y devuelve negro tanto con
# CopyFromScreen como con PrintWindow. Por eso esto trae la ventana al frente
# y dispara Win+Impr Pant, que lo hace el propio Windows con el compositor.
# Windows guarda la foto en Imagenes\Capturas de pantalla; el script la localiza,
# la recorta al marco de la ventana y la deja en la carpeta de destino.

param(
    [string]$Proceso = "client",
    [int]$Veces = 1,
    [int]$Cada = 3,
    [string]$Destino = "C:\Users\JC-PC\Pictures\auto_screenshots"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$VK_LWIN = 0x5B; $VK_SNAPSHOT = 0x2C; $KEYUP = 0x0002
$CarpetaWin = "C:\Users\JC-PC\Pictures\Screenshots"

$p = Get-Process -Name $Proceso -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($null -eq $p) { Write-Output "No encuentro ninguna ventana del proceso '$Proceso'."; exit 1 }

$h = $p.MainWindowHandle
Write-Output "Ventana: $($p.MainWindowTitle)   (PID $($p.Id))"
if (-not (Test-Path $Destino)) { New-Item -ItemType Directory -Path $Destino -Force | Out-Null }

for ($i = 1; $i -le $Veces; $i++) {
    if ([Win]::IsIconic($h)) { [Win]::ShowWindow($h, 9) | Out-Null }
    [Win]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 600

    if ([Win]::GetForegroundWindow() -ne $h) {
        Write-Output "  [$i/$Veces] AVISO: Windows no dejo traer la ventana al frente. Haz clic en Tibia y reintenta."
        continue
    }

    $antes = (Get-ChildItem $CarpetaWin -Filter *.png -ErrorAction SilentlyContinue | Measure-Object).Count

    [Win]::keybd_event($VK_LWIN, 0, 0, [UIntPtr]::Zero)
    [Win]::keybd_event($VK_SNAPSHOT, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [Win]::keybd_event($VK_SNAPSHOT, 0, $KEYUP, [UIntPtr]::Zero)
    [Win]::keybd_event($VK_LWIN, 0, $KEYUP, [UIntPtr]::Zero)

    # espera a que Windows escriba el fichero
    $nuevo = $null
    for ($t = 0; $t -lt 30; $t++) {
        Start-Sleep -Milliseconds 200
        $lista = Get-ChildItem $CarpetaWin -Filter *.png -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
        if ($lista.Count -gt $antes) { $nuevo = $lista[0]; break }
    }

    if ($null -eq $nuevo) {
        Write-Output "  [$i/$Veces] No aparecio ninguna captura nueva en $CarpetaWin"
        continue
    }

    $r = New-Object Win+RECT
    [Win]::GetWindowRect($h, [ref]$r) | Out-Null
    $full = [System.Drawing.Image]::FromFile($nuevo.FullName)
    $x = [Math]::Max(0, $r.Left); $y = [Math]::Max(0, $r.Top)
    $w = [Math]::Min($r.Right - $x, $full.Width - $x)
    $alto = [Math]::Min($r.Bottom - $y, $full.Height - $y)

    $bmp = New-Object System.Drawing.Bitmap $w, $alto
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($full, (New-Object System.Drawing.Rectangle 0, 0, $w, $alto), (New-Object System.Drawing.Rectangle $x, $y, $w, $alto), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $full.Dispose()

    $nombre = Join-Path $Destino ("{0}_{1:yyyyMMdd_HHmmss}_{2:d2}.png" -f $Proceso, (Get-Date), $i)
    $bmp.Save($nombre, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "  [$i/$Veces] $nombre   ($w x $alto)"

    if ($i -lt $Veces) { Start-Sleep -Seconds $Cada }
}
