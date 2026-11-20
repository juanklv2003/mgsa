# Package the native touch game as an APK (Windows / PowerShell, no Gradle).
$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
$repo = "C:\Users\Gloria\Documents\mgsa"
Set-Location $repo
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-25.0.5.8-hotspot"
$sdk = "$repo\work\sdk"
$env:ANDROID_HOME = $sdk
$bt = "$sdk\build-tools\34.0.0"
$android_jar = "$sdk\platforms\android-34\android.jar"
$javac = "$env:JAVA_HOME\bin\javac.exe"

$src = "$repo\native\android"
$legacy = "$repo\profiles\mgsa\android"
$work = "$src\build\apk"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$work\classes", "$work\dex", "$work\lib\arm64-v8a" | Out-Null

Write-Host "=== 1. resources ==="
$resFiles = @()
$resFiles += "$legacy\res\values\strings.xml"
$resFiles += Get-ChildItem -Recurse "$legacy\res" -Filter ic_launcher.png | ForEach-Object { $_.FullName }
& "$bt\aapt2.exe" compile -o "$work\compiled.flata" $resFiles
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed" }
Push-Location $work
& "$bt\aapt2.exe" link -o "app-res.apk" -I $android_jar `
    --manifest "$src\AndroidManifest.xml" --java "$work\classes" `
    --min-sdk-version 29 --target-sdk-version 34 "compiled.flata"
Pop-Location
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

Write-Host "=== 2. Java + dex ==="
$mgsaJava = "$src\java\io\github\juanklv2003\mgsa\touch\MgsaActivity.java"
$rjava = "$work\classes\io\github\juanklv2003\mgsa\touch\R.java"
$javaSources = @($mgsaJava)
if (Test-Path $rjava) { $javaSources += $rjava }
& $javac -nowarn --release 11 -classpath $android_jar -d "$work\classes" $javaSources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }
$classFiles = Get-ChildItem -Recurse "$work\classes" -Filter *.class | ForEach-Object { $_.FullName }
& "$bt\d8.bat" --release --min-api 29 --lib $android_jar --output "$work\dex" $classFiles
if ($LASTEXITCODE -ne 0) { throw "d8 failed" }

Write-Host "=== 3. native library ==="
Copy-Item "$repo\native\android\build\libmgsa_touch.so" "$work\lib\arm64-v8a\libmgsa_touch.so" -Force

Write-Host "=== 4. assemble APK ==="
$resZip = "$work\app-res.zip"
Copy-Item "$work\app-res.apk" $resZip -Force
Expand-Archive -Path $resZip -DestinationPath "$work\res_unpacked" -Force
$dexFile = Get-ChildItem "$work\dex" -Filter classes.dex | Select-Object -First 1
Copy-Item $dexFile.FullName "$work\res_unpacked\classes.dex" -Force
Copy-Item "$work\lib" "$work\res_unpacked\lib" -Recurse -Force
$apk = "$repo\native-touch-unsigned.apk"
if (Test-Path $apk) { Remove-Item $apk -Force }
Push-Location "$work\res_unpacked"
& zip -q -r $apk .
& zip -q -0 $apk resources.arsc
Pop-Location

Write-Host "=== 5. zipalign (16 kB pages) ==="
$aligned = "$repo\native-touch-aligned.apk"
if (Test-Path $aligned) { Remove-Item $aligned -Force }
& "$bt\zipalign.exe" -f -p 16384 $apk $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

Write-Host "=== DONE ==="
Get-Item $aligned | Select-Object FullName, Length
