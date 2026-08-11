#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

detect_cubrid_home()
{
  if [[ -n "${CUBRID:-}" ]]; then
    printf '%s\n' "$CUBRID"
    return
  fi

  local cubrid_command
  cubrid_command=$(command -v cubrid 2>/dev/null || true)
  if [[ -z "$cubrid_command" ]]; then
    return 1
  fi

  cubrid_command=$(readlink -f "$cubrid_command")
  cd -- "$(dirname -- "$cubrid_command")/.." && pwd -P
}

CUBRID_HOME=$(detect_cubrid_home || true)
if [[ -z "$CUBRID_HOME" ]]; then
  echo "[ERROR] CUBRID 설치 경로를 찾을 수 없습니다."
  echo "        CUBRID 환경변수를 설정하거나 cubrid 명령을 PATH에 추가해 주세요."
  exit 1
fi

INCLUDE_DIR="$CUBRID_HOME/include"
LIB_DIR="$CUBRID_HOME/lib"
CCI_LIB_DIR="$CUBRID_HOME/cci/lib"

if [[ ! -f "$INCLUDE_DIR/cubrid_log.h" || ! -f "$INCLUDE_DIR/cas_cci.h" ]]; then
  echo "[ERROR] CUBRID 헤더를 찾을 수 없습니다: $INCLUDE_DIR"
  echo "        CUBRID 소스코드는 필요 없지만 헤더가 포함된 설치본이 필요합니다."
  exit 1
fi

if ! compgen -G "$LIB_DIR/libcubridcs.so*" >/dev/null; then
  echo "[ERROR] libcubridcs 라이브러리를 찾을 수 없습니다: $LIB_DIR"
  exit 1
fi

if compgen -G "$LIB_DIR/libcascci.so*" >/dev/null; then
  CCI_LIB_DIR="$LIB_DIR"
elif ! compgen -G "$CCI_LIB_DIR/libcascci.so*" >/dev/null; then
  echo "[ERROR] libcascci 라이브러리를 찾을 수 없습니다."
  echo "        확인 경로: $LIB_DIR, $CCI_LIB_DIR"
  exit 1
fi

CC=${CC:-gcc}

"$CC" \
  -g \
  -I"$INCLUDE_DIR" \
  "$SCRIPT_DIR/cdc_test_helper.c" \
  -L"$LIB_DIR" \
  -L"$CCI_LIB_DIR" \
  -Wl,-rpath,"$LIB_DIR" \
  -Wl,-rpath,"$CCI_LIB_DIR" \
  -lcubridcs \
  -lcascci \
  -o "$SCRIPT_DIR/cdc_test_helper"

echo "[OK] 빌드 완료: $SCRIPT_DIR/cdc_test_helper"
echo "[OK] 사용한 CUBRID 경로: $CUBRID_HOME"
