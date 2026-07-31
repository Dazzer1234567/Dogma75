$file = "C:\Users\Daz_Inv\.claude\projects\c--0-CODE-Dogma75\0c823f05-3845-438d-ab47-912854b905de\tool-results\toolu_01BZcWQC7h69cgMRZj7wDwYM.txt"
$content = Get-Content $file
Write-Host "E1:" ($content | Select-String "E1:").Count
Write-Host "E2:" ($content | Select-String "E2:").Count
Write-Host "E3:" ($content | Select-String "E3:").Count
Write-Host "E4:" ($content | Select-String "E4:").Count
Write-Host "E5:" ($content | Select-String "E5:").Count
Write-Host "E6:" ($content | Select-String "E6:").Count
