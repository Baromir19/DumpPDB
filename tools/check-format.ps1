$files = Get-ChildItem `
    -Recurse `
    -Path Core, API, CLI, tests `
    -Include *.cpp,*.h,*.hpp `
    -File

$failed = $false

foreach ($file in $files)
{
    Write-Host "Checking $($file.FullName)"

    clang-format `
        --style=file `
        --dry-run `
        --Werror `
        $file.FullName

    if ($LASTEXITCODE -ne 0)
    {
        $failed = $true
    }
}

exit $failed