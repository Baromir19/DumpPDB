$files = Get-ChildItem -Recurse `
    -Path Core, API, CLI `
    -Include *.cpp, *.hpp, *.h |
    Where-Object {
        $_.FullName -notmatch "build"
    }


foreach ($file in $files)
{
    clang-tidy `
        $file.FullName `
        -p build-tidy `
        --quiet
}