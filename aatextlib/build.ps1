# Builds aatext.library inside the amiga-gcc Docker container.
# The whole AAText folder is mounted: FreeType comes from
# ../third_party/freetype-2.14.3 (tools/fetch-freetype.sh or the AAText build).
# Usage:  .\build.ps1              (68020 release)
#         .\build.ps1 CPU=68060
#         .\build.ps1 DEBUG=1      (debug build, kprintf to serial)
#         .\build.ps1 test         (test programs)
#         .\build.ps1 dist         (Aminet archive in dist\)
#         .\build.ps1 clean
$image = "ghcr.io/rondoval/amiga-build-container:latest"
docker run --rm -v "${PSScriptRoot}\..:/src" -w /src/aatextlib --entrypoint make $image @args
exit $LASTEXITCODE
