#!/bin/bash
# Collect MinGW runtime DLLs required by Windows executables/DLLs in a folder.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <dist-directory>" >&2
    exit 1
fi

DIST="$(cd "$1" && pwd)"
MINGW_BIN="/mingw64/bin"
OBJDUMP="${OBJDUMP:-/mingw64/bin/objdump.exe}"

if [[ ! -x "$OBJDUMP" ]]; then
    echo "Error: objdump not found at $OBJDUMP" >&2
    exit 1
fi

is_system_dll() {
    case "$1" in
        ADVAPI32.dll|AUTHZ.dll|bcrypt.dll|bcryptPrimitives.dll|cfgmgr32.dll|COMBASE.dll|COMCTL32.dll|COMDLG32.dll|comdlg32.dll|CRYPT32.dll|CRYPTBASE.dll|d3d9.dll|d3d11.dll|D3D12.dll|d3d12.dll|DEVOBJ.dll|DNSAPI.dll|DWrite.dll|dwmapi.dll|dxgi.dll|GDI32.dll|gdi32full.dll|GLU32.dll|IMM32.dll|IPHLPAPI.DLL|kernel.appcore.dll|KERNEL32.dll|MPR.dll|msvcp_win.dll|msvcrt.dll|MSWSOCK.dll|ncrypt.dll|NETAPI32.dll|Normaliz.dll|NSI.dll|ntdll.dll|ole32.dll|OLEAUT32.dll|OPENGL32.dll|powrprof.dll|PROPSYS.dll|PSAPI.DLL|RPCRT4.dll|Secur32.dll|sechost.dll|SETUPAPI.dll|SHELL32.dll|SHCORE.dll|SHLWAPI.dll|SSPICLI.dll|ucrtbase.dll|UMPDC.dll|USER32.dll|USERENV.dll|USP10.dll|UxTheme.dll|VERSION.dll|WINHTTP.dll|WININET.dll|WINMM.dll|WINSPOOL.DRV|win32u.dll|Windows.Storage.dll|windows.storage.dll|WLDAP32.dll|WTSAPI32.dll|WS2_32.dll|apphelp.dll|profapi.dll)
            return 0
            ;;
        api-ms-win-*)
            return 0
            ;;
    esac
    return 1
}

in_dist() {
    find "$DIST" -maxdepth 3 -iname "$1" -print -quit | grep -q .
}

declare -A SCANNED MISSING
pending=()
copied=0

for exe in "$DIST"/*.exe; do
    [[ -f "$exe" ]] || continue
    pending+=("$exe")
done

while ((${#pending[@]} > 0)); do
    file="${pending[0]}"
    pending=("${pending[@]:1}")
    [[ -f "$file" ]] || continue
    [[ -n "${SCANNED[$file]:-}" ]] && continue
    SCANNED["$file"]=1

    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        is_system_dll "$dep" && continue
        if in_dist "$dep"; then
            while IFS= read -r -d '' found; do
                [[ -n "${SCANNED[$found]:-}" ]] || pending+=("$found")
            done < <(find "$DIST" -maxdepth 3 -iname "$dep" -print0)
            continue
        fi
        [[ -n "${MISSING[$dep]:-}" ]] && continue
        MISSING["$dep"]=1

        if [[ -f "$MINGW_BIN/$dep" ]]; then
            cp "$MINGW_BIN/$dep" "$DIST/"
            copied=1
            echo "Copied $dep"
            pending+=("$DIST/$dep")
        else
            echo "Warning: dependency not found in $MINGW_BIN: $dep" >&2
        fi
    done < <("$OBJDUMP" -p "$file" 2>/dev/null | awk '/DLL Name:/ {print $3}')
done

if [[ $copied -eq 0 ]]; then
    echo "No additional MinGW DLLs were needed."
fi
