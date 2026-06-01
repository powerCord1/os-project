#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WALI_DIR="$(cd "$SCRIPT_DIR/../../WALI" && pwd)"
LLVM_BIN="$WALI_DIR/build/llvm/bin"
SYSROOT="$WALI_DIR/build/sysroot"
INSTALL_DIR="${1:-$HOME/.local/bin}"

if [ ! -f "$LLVM_BIN/clang" ]; then
    echo "Error: WALI LLVM not found at $LLVM_BIN"
    echo "Build it first: cd $WALI_DIR && make wali-compiler && make libc"
    exit 1
fi

mkdir -p "$INSTALL_DIR"

TARGET=wasm32-unknown-linux-muslwali

cat > "$INSTALL_DIR/${TARGET}-cc" << EOF
#!/bin/bash
exec "$LLVM_BIN/clang" --target=$TARGET --sysroot="$SYSROOT" \\
    -pthread -fdeclspec -fwasm-exceptions \\
    -matomics -mbulk-memory -mexception-handling \\
    -L"$SYSROOT/lib" \\
    -Wl,--shared-memory -Wl,--export-memory \\
    -Wl,--max-memory=2147483648 \\
    -Wl,--undefined=__walirt_wasm_memory_size "\$@"
EOF

cat > "$INSTALL_DIR/${TARGET}-ld" << EOF
#!/bin/bash
exec "$LLVM_BIN/wasm-ld" "\$@"
EOF

cat > "$INSTALL_DIR/${TARGET}-ar" << EOF
#!/bin/bash
exec "$LLVM_BIN/llvm-ar" "\$@"
EOF

cat > "$INSTALL_DIR/${TARGET}-ranlib" << EOF
#!/bin/bash
exec "$LLVM_BIN/llvm-ranlib" "\$@"
EOF

chmod +x "$INSTALL_DIR/${TARGET}-"{cc,ld,ar,ranlib}

echo "Installed WALI toolchain wrappers to $INSTALL_DIR:"
ls -1 "$INSTALL_DIR/${TARGET}-"*
