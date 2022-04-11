# TwsWebSocket
Websocket for TWS API.

## Versions
* 20.10.1 Initial Release
## Build
* Prerequisites
    * Extract https://www.boost.org/users/download/ and set environement variable BOOST_DIR to the path.
    * Create a build folder  and set environment variable REPO_DIR to the path.
    * Extract [Intel Decimal Floating](https://www.intel.com/content/www/us/en/developer/articles/tool/intel-decimal-floating-point-math-library.html) point library to $REPO_DIR
        *  add environement variable INCLUDE=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt;C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\MSVC\14.31.31103\include
        *  add to path "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.31.31103\bin\Hostx64\x64\"
        * `$REPO_DIR/IntelRDFPMathLib20U2/LIBRARY/nmake -fmakefile.mak CC=cl CALL_BY_REF=0 GLOBAL_RND=0 GLOBAL_FLAGS=0 UNCHANGED_BINARY_FLAGS=0`
    * Extract pre-built binaries from https://tukaani.org/xz/ to $REPO_DIR.
    * Download latest twsApi from https://interactivebrokers.github.io/
    * Win64 OpenSSL v1.1.1k from https://slproweb.com/products/Win32OpenSSL.html
    * Installer project from https://marketplace.visualstudio.com/items?itemName=VisualStudioClient.MicrosoftVisualStudio2022InstallerProjects
    * `chocolatey install jq`
    * Sql server express. Create db 'jde'
    * From powershell, create odbc connection. - `Add-OdbcDsn -Name "Jde_TWS_Connection" -DriverName "ODBC Driver 17 for SQL Server" -Platform "64
-bit" -DsnType "User" -SetPropertyValue @("SERVER=localhost", "Trusted_Connection=Yes", "DATABASE=jde")`

