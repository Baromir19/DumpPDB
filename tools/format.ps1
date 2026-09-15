$files = Get-ChildItem `
    -Recurse `
    -Path Core, API, CLI, tests `
    -Include *.cpp,*.h,*.hpp `
    -File

foreach ($file in $files)
{
    clang-format -i $file.FullName
}