$files = Get-ChildItem `
    -Recurse `
    -Include *.cpp,*.h,*.hpp `
    -File `
    | Where-Object {
        $_.FullName -notmatch "\\build\\" -and
        $_.FullName -notmatch "\\_deps\\"
    }

$failed = $false

foreach ($file in $files)
{
    clang-format --dry-run --Werror $file.FullName

    if ($LASTEXITCODE -ne 0)
    {
        $failed = $true
    }
}

exit $failed