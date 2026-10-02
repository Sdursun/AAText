# Builds AAText inside the amiga-gcc Docker container.
# Usage:  .\build.ps1              (release)
#         .\build.ps1 DEBUG=1      (debug build)
#         .\build.ps1 CPU=68060
#         .\build.ps1 clean
$image = "ghcr.io/rondoval/amiga-build-container:latest"
docker run --rm -v "${PSScriptRoot}:/src" -w /src --entrypoint make $image @args
exit $LASTEXITCODE
