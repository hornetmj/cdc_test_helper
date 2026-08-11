#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
USER_BIN_DIR=${HOME}/.local/bin

"$SCRIPT_DIR/build.sh"

if [[ ! -f "$SCRIPT_DIR/cdc_test_helper.conf" ]]; then
  echo "[ERROR] 설정 파일이 없습니다: $SCRIPT_DIR/cdc_test_helper.conf"
  exit 1
fi

chmod 600 "$SCRIPT_DIR/cdc_test_helper.conf"
mkdir -p "$USER_BIN_DIR"

if [[ -e "$USER_BIN_DIR/cdc_test_helper" || -L "$USER_BIN_DIR/cdc_test_helper" ]]; then
  EXISTING_TARGET=$(readlink -f "$USER_BIN_DIR/cdc_test_helper" 2>/dev/null || true)
  if [[ "$EXISTING_TARGET" != "$SCRIPT_DIR/cdc_test_helper" ]]; then
    echo "[ERROR] 다른 파일이 이미 존재합니다: $USER_BIN_DIR/cdc_test_helper"
    echo "        기존 파일을 확인한 후 다시 설치해 주세요."
    exit 1
  fi
fi

ln -sfn "$SCRIPT_DIR/cdc_test_helper" "$USER_BIN_DIR/cdc_test_helper"

echo
echo "[OK] 설치 완료"
echo "[OK] 실행 파일: $SCRIPT_DIR/cdc_test_helper"
echo "[OK] 설정 파일: $SCRIPT_DIR/cdc_test_helper.conf"
echo "[OK] 명령 링크: $USER_BIN_DIR/cdc_test_helper"
echo

case ":${PATH}:" in
  *":${USER_BIN_DIR}:"*) ;;
  *)
    echo "[NOTICE] $USER_BIN_DIR가 PATH에 없습니다. 다음 내용을 셸 설정 파일에 추가해 주세요."
    echo "         export PATH=\"\$HOME/.local/bin:\$PATH\""
    ;;
esac

echo "설정 파일을 수정한 후 다음 명령으로 사용법을 확인하세요."
echo "  cdc_test_helper --help"
