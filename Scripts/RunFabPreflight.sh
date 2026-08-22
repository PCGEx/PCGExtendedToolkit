#!/bin/sh
SCRIPT_LOCATION="$(cd "$(dirname "$0")" && pwd)"

if [ -f "$SCRIPT_LOCATION/fab-preflight.py" ]; then
    SCRIPT_DIR="$SCRIPT_LOCATION"
elif [ -f "$SCRIPT_LOCATION/Plugins/PCGExtendedToolkit/Scripts/fab-preflight.py" ]; then
    SCRIPT_DIR="$SCRIPT_LOCATION/Plugins/PCGExtendedToolkit/Scripts"
else
    echo "ERROR: Could not find fab-preflight.py."
    echo "Run this from either:"
    echo "  - Project root (containing Plugins/PCGExtendedToolkit/)"
    echo "  - Plugins/PCGExtendedToolkit/Scripts/"
    exit 1
fi

if command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo ""
    echo "ERROR: Python not found. Install it from https://python.org"
    exit 1
fi

"$PY" "$SCRIPT_DIR/fab-preflight.py" --selftest || exit 1
"$PY" "$SCRIPT_DIR/fab-preflight.py" "$@"
RESULT=$?

echo ""
if [ $RESULT -eq 0 ]; then
    echo "Clean."
else
    echo "Pre-flight found blocking issues -- fix them before submitting to FAB."
fi

exit $RESULT
