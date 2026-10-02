# Builds the glyph smoke test and runs it under vamos (68020 emulation).
# Usage:  .\test.ps1 [text]      e.g.  .\test.ps1 Ag   or   .\test.ps1 @fdde
# Needs third_party/dejavu-fonts-ttf-2.37 (see tests/test.prefs).
param([string]$text = "Ag")
$image = "aatext-vamos"
if (-not (docker images -q $image)) { docker build -t $image "$PSScriptRoot/tools/vamos" }
docker run --rm -v "${PSScriptRoot}:/src" -w /src --entrypoint sh $image -c "make test && vamos -C 68020 -m 8192 build/68020/fttest tests/test.prefs '$text'"
exit $LASTEXITCODE
