# TwsWebSocket
Websocket for TWS API.

## Versions
* 20.11.1 Rewrite
* 20.10.1 Initial Release
## Build
* Prerequisites
    * Save https://www.boost.org/ to environement variable BOOST_DIR.
    * Create environment variable to directory to build REPO_DIR.
    * Extract pre-built binaries from https://tukaani.org/xz/ to $REPO_DIR.
    * Download latest twsApi from https://interactivebrokers.github.io/
    * Win64 OpenSSL v1.1.1k from https://slproweb.com/products/Win32OpenSSL.html
    * Installer project from https://marketplace.visualstudio.com/items?itemName=VisualStudioClient.MicrosoftVisualStudio2017InstallerProjects
    * `chocolatey install jq`
    * Sql server express. Create db 'jde'
    * From powershell, create odbc connection. - `Add-OdbcDsn -Name "Jde_TWS_Connection" -DriverName "ODBC Driver 17 for SQL Server" -Platform "64
-bit" -DsnType "User" -SetPropertyValue @("SERVER=localhost", "Trusted_Connection=Yes", "DATABASE=jde")`

