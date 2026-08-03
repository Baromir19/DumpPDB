$files = Get-ChildItem `
    -Path . `
    -Recurse `
    -Include *.cpp,*.h,*.hpp `
    -File `
    | Where-Object {
        $_.FullName -notmatch "\\build\\" -and
        $_.FullName -notmatch "\\_deps\\"
    }

foreach ($file in $files)
{
    clang-format -i $file.FullName
}