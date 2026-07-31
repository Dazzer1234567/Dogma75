Get-ChildItem 'c:\0_CODE\Dogma75\Workspace\' -Recurse -File |
    Where-Object { $_.FullName -notmatch 'USBDev2|\\\.pio' } |
    ForEach-Object { $_.FullName }
