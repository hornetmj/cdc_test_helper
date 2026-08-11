# CUBRID CDC Test Helper

`cdc_test_helper`는 CUBRID CDC API를 이용해 특정 시간 또는 특정 LSA부터 변경 로그를 추출하고, 사용자가 확인할 수 있는 SQL 형태로 출력하는 도구입니다.

다음 기능을 제공합니다.

- 특정 시간부터 로그 추출
- 특정 LSA(`PAGEID|OFFSET`)부터 로그 추출
- 지정한 개수만큼 로그를 출력한 뒤 자동 종료
- 특정 테이블만 추출
- DML 또는 DDL만 선택하여 출력
- 추출된 DML/DDL을 SQL 형태로 출력
- 필요할 경우 원본 CDC 로그 아이템과 트랜잭션 정보 출력

## 1. 사전 조건

### 1.1 CUBRID 설치

CUBRID 소스코드는 필요하지 않습니다. 다음 헤더와 라이브러리가 포함된 CUBRID 설치본이 필요합니다.

```text
$CUBRID/include/cubrid_log.h
$CUBRID/include/cas_cci.h
$CUBRID/lib/libcubridcs.so
$CUBRID/lib/libcascci.so
```

일부 설치본에서는 CCI 라이브러리가 다음 위치에 있을 수 있습니다.

```text
$CUBRID/cci/lib/libcascci.so
```

여러 CUBRID 버전이 설치되어 있다면 사용할 버전을 명시적으로 지정하는 것이 안전합니다.

```bash
export CUBRID=/사용할/CUBRID/설치경로
export PATH="$CUBRID/bin:$PATH"
```

`CUBRID` 환경변수가 없으면 설치 스크립트가 `PATH`에 등록된 `cubrid` 명령으로 설치 경로를 찾습니다.

설치할 때뿐 아니라 **실행할 때도 추출 대상 DB가 등록된 CUBRID 환경을 적용해야 합니다.** CUBRID 사용자 환경이
셸 시작 시 자동으로 적용되지 않거나 별도의 DB 등록 디렉터리를 사용하는 경우에는 다음처럼 직접 지정합니다.

```bash
export CUBRID=/사용할/CUBRID/설치경로
export CUBRID_DATABASES=/databases.txt가/있는/디렉터리
export PATH="$CUBRID/bin:$PATH"
cdc_test_helper -n 10
```

실행 시 설정된 `CUBRID_DATABASES`의 `databases.txt`에 `database_name`이 등록되어 있지 않으면 서버가 실행 중이어도
접속에 실패할 수 있습니다.

### 1.2 CDC 서버 설정

CDC로 DML을 추출하려면 대상 데이터베이스의 `supplemental_log`가 활성화되어 있어야 합니다. DDL까지 추출하려면 DDL이 기록되는 설정을 사용해야 합니다.

예:

```ini
[common]
supplemental_log=1
```

과거 시점이나 과거 LSA부터 추출하려면 해당 시점의 활성 로그 또는 아카이브 로그가 서버에 남아 있어야 합니다. 필요한 로그가 삭제되었거나 LSA가 올바른 로그 레코드 경계를 가리키지 않으면 `CUBRID_LOG_INVALID_LSA(-5)`가 반환될 수 있습니다.

> **스키마 일치 조건**
>
> 추출하려는 로그가 생성된 당시의 테이블 스키마와 현재 데이터베이스의 테이블 스키마가 일치해야 합니다. CDC는
> 로그를 추출할 때 현재 스키마 정보를 사용하므로, 로그 생성 이후 컬럼 추가·삭제·순서 변경 또는 타입 변경이
> 있었다면 과거 DML의 컬럼과 값을 정확하게 복원할 수 없습니다. 이 경우 일부 값이 누락되거나 잘못 해석되거나
> 로그 추출에 실패할 수 있습니다. 과거 로그를 분석할 때는 해당 로그 시점과 동일한 스키마를 먼저 준비해야 합니다.

### 1.3 필요한 명령

- Linux x86_64
- `gcc`
- `bash`
- 실행 중인 CUBRID 데이터베이스와 브로커

## 2. 설치

저장소를 clone합니다.

```bash
git clone https://github.com/H2SU/cdc_test_helper.git
cd cdc_test_helper
```

설치 스크립트를 실행합니다.

```bash
./install.sh
```

설치 스크립트는 다음 작업을 수행합니다.

1. `CUBRID` 환경변수 또는 `cubrid` 명령으로 설치 경로 확인
2. CDC 및 CCI 헤더 확인
3. `libcubridcs`, `libcascci` 확인
4. 현재 CUBRID 설치본을 이용해 프로그램 컴파일
5. `cdc_test_helper.conf`의 권한을 `600`으로 설정
6. `~/.local/bin/cdc_test_helper` 링크 생성

`~/.local/bin`이 `PATH`에 없다면 다음 내용을 셸 설정 파일에 추가합니다.

```bash
export PATH="$HOME/.local/bin:$PATH"
```

설치 후 도움말을 확인합니다.

```bash
cdc_test_helper --help
```

### 직접 빌드

설치 링크를 만들지 않고 현재 폴더에 실행 파일만 생성하려면 다음 명령을 사용합니다.

```bash
./build.sh
```

생성되는 파일:

```text
./cdc_test_helper
```

빌드 결과에는 빌드에 사용한 CUBRID 라이브러리 경로가 RUNPATH로 기록됩니다. CUBRID 설치 경로를 이동하거나 다른 버전으로 변경한 경우 다시 빌드해야 합니다.

## 3. 설정 파일

접속 정보와 자주 변경하지 않는 고급 설정은 `cdc_test_helper.conf`에서 관리합니다.

```text
cdc_test_helper/
├── cdc_test_helper
├── cdc_test_helper.conf
├── build.sh
├── install.sh
└── README.md
```

프로그램은 실행 파일이 있는 디렉터리에서 다음 파일을 자동으로 찾습니다.

```text
cdc_test_helper.conf
```

따라서 `~/.local/bin`의 심볼릭 링크를 통해 실행하더라도 실제 실행 파일 옆의 설정 파일을 사용합니다.

특수한 경우에는 환경변수로 다른 설정 파일을 지정할 수 있습니다.

```bash
export CDC_TEST_HELPER_CONF=/별도/경로/test.conf
cdc_test_helper -n 10 -m dml
```

### 3.1 설정 파일 문법

설정은 `이름=값` 형식으로 작성합니다.

```ini
database_name=demodb
cdc_server_ip=127.0.0.1
```

- 빈 줄은 무시됩니다.
- `#` 또는 `;`로 시작하는 줄은 주석입니다.
- 키와 값 주변의 공백은 제거됩니다.
- 문자열 값에 따옴표를 붙이지 않습니다.
- Boolean 값은 `true/false`, `yes/no`, `1/0`을 사용할 수 있습니다.
- 알 수 없는 설정명이나 잘못된 값이 있으면 실행을 중단하고 파일명과 줄 번호를 출력합니다.

### 3.2 데이터베이스 및 사용자

| 설정 | 기본값 | 의미 |
|---|---:|---|
| `database_name` | `demodb` | CDC 로그를 추출할 데이터베이스 이름입니다. 실행 명령의 마지막 인자로 다른 DB 이름을 지정하면 해당 값이 우선합니다. |
| `user` | `dba` | CDC 서버 로그인과 브로커 스키마 조회에 사용할 DB 사용자입니다. |
| `password` | 빈 값 | DB 사용자 비밀번호입니다. |

비밀번호가 들어갈 수 있으므로 설정 파일 권한을 다른 사용자가 읽을 수 없도록 유지합니다.

```bash
chmod 600 cdc_test_helper.conf
```

### 3.3 CDC 서버

| 설정 | 기본값 | 의미 |
|---|---:|---|
| `cdc_server_ip` | `127.0.0.1` | CDC API가 접속할 CUBRID 서버 주소입니다. |
| `cdc_server_port` | `1523` | CDC 서버가 사용하는 CUBRID 포트입니다. 허용 범위는 `1~65535`입니다. |
| `connection_timeout` | `300` | CDC 서버 연결 제한 시간입니다. `-1` 또는 `0~360`초를 지정합니다. |
| `extraction_timeout` | `300` | CDC 로그 추출 대기 제한 시간입니다. `-1` 또는 `0~360`초를 지정합니다. |
| `max_log_item` | `512` | 한 번의 CDC API 호출로 받을 최대 로그 아이템 수입니다. 허용 범위는 `1~1024`입니다. CLI의 `-n`과는 다른 설정입니다. |
| `all_in_cond` | `false` | UPDATE/DELETE SQL의 조건절에 전체 컬럼을 포함할지 설정합니다. `false`이면 기본키를 우선 사용합니다. |
| `extraction_users` | 빈 값 | 특정 DB 사용자가 만든 로그만 추출합니다. 여러 사용자는 `DBA,APP_USER`처럼 쉼표로 구분합니다. 빈 값이면 모든 사용자입니다. |

`max_log_item`과 `-n`의 차이:

- `max_log_item`: CDC API 한 번에 서버에서 가져오는 최대 묶음 크기
- `-n`: 필터를 통과해 실제 출력한 총 로그 개수

### 3.4 브로커

| 설정 | 기본값 | 의미 |
|---|---:|---|
| `broker_ip` | `127.0.0.1` | 테이블명, 컬럼명, PK 등 스키마 정보를 조회할 브로커 주소입니다. |
| `broker_port` | `33000` | 스키마 조회에 사용할 브로커 포트입니다. |

CDC 서버와 브로커는 서로 다른 주소나 포트를 사용할 수 있습니다.

### 3.5 출력

| 설정 | 기본값 | 의미 |
|---|---:|---|
| `print_sql` | `true` | 추출한 DML/DDL을 SQL 형태로 출력합니다. |
| `print_log_item` | `false` | 트랜잭션 ID, 사용자, CDC 데이터 타입, 컬럼 인덱스 등 원본 로그 아이템 정보를 추가 출력합니다. |
| `print_timer` | `false` | CDC timer/heartbeat 로그를 원본 로그 아이템 출력에 포함합니다. `print_log_item=true`일 때 사용합니다. |
| `print_transaction` | `false` | commit 시점에 트랜잭션에 포함된 SQL 목록을 추가 출력합니다. |
| `ignore_trigger_dml` | `true` | 트리거에 의해 생성된 DML을 SQL 출력 대상에서 제외합니다. |

일반적인 로그 확인 용도에서는 다음 설정을 권장합니다.

```ini
print_sql=true
print_log_item=false
print_timer=false
print_transaction=false
ignore_trigger_dml=true
```

원인 분석을 위해 원본 CDC 정보를 함께 확인하려면 다음처럼 변경합니다.

```ini
print_log_item=true
```

### 3.6 대상 DB 적용 기능

| 설정 | 기본값 | 의미 |
|---|---:|---|
| `target_server_ip` | 빈 값 | 추출한 SQL을 적용할 대상 DB 서버 주소입니다. |
| `target_server_port` | 빈 값 | 대상 DB 브로커 포트입니다. |
| `target_database_name` | 빈 값 | 대상 데이터베이스 이름입니다. |

세 값을 모두 비워 두면 적용 기능은 비활성화됩니다. 적용 기능을 사용할 때는 세 값을 반드시 모두 설정해야 합니다.

```ini
target_server_ip=192.168.0.20
target_server_port=33000
target_database_name=targetdb
```

대상 DB 적용은 데이터를 변경하므로 단순 로그 확인 목적이라면 세 항목을 비워 두십시오.

## 4. 명령행 옵션

실행할 때 자주 변경하는 값만 명령행 옵션으로 제공합니다.

```text
사용법: cdc_test_helper [옵션] [데이터베이스명]

옵션:
  -t, --start-time TIME    시작 시간(YYYY-MM-DD HH:MM:SS)
  -l, --start-lsa LSA      시작 LSA(PAGEID|OFFSET)
  -n, --max-log-items N    필터를 통과한 로그 N건 출력 후 종료
  -m, --log-type TYPE      출력 종류: all, dml, ddl
  -T, --table TABLES       쉼표로 구분한 테이블 목록
  -h, --help               도움말 출력
```

짧은 옵션과 긴 옵션은 동일하게 동작합니다.

```bash
-t "2026-08-05 04:00:00"
--start-time "2026-08-05 04:00:00"
--start-time="2026-08-05 04:00:00"
```

### 옵션 우선순위

1. 프로그램 기본값
2. `cdc_test_helper.conf`
3. 명령행 옵션 및 마지막 데이터베이스 인자

`-t`와 `-l`은 동시에 사용할 수 없습니다.

## 5. 사용 예시

### 5.1 현재 시점부터 계속 추출

시작 시간이나 LSA를 지정하지 않으면 현재 시점부터 추출합니다. `-n`을 지정하지 않으면 사용자가 종료할 때까지 계속 실행합니다.

```bash
cdc_test_helper -m all
```

종료:

```text
Ctrl+C
```

### 5.2 특정 시간부터 DML 100건

```bash
cdc_test_helper \
  -t "2026-08-05 04:00:00" \
  -n 100 \
  -m dml
```

시간은 프로그램 실행 장비의 로컬 시간대를 기준으로 해석됩니다. 요청 시각과 실제로 선택된 시작 LSA는 실행 초기에 출력됩니다.

```text
[CDC_START] requested_time=2026-08-05 04:00:00, resolved_time=..., lsa=29028|3264, ...
```

### 5.3 특정 LSA부터 DML 100건

```bash
cdc_test_helper \
  -l '29028|3264' \
  -n 100 \
  -m dml
```

`|`는 셸의 파이프 문자이므로 LSA는 반드시 따옴표로 감싸는 것이 안전합니다.

```text
[CDC_START] requested_lsa=29028|3264, raw_lsa=918734323983610212
```

### 5.4 특정 테이블의 DML만 추출

```bash
cdc_test_helper \
  -t "2026-08-05 04:00:00" \
  -n 100 \
  -m dml \
  -T TN_OGDAT_DATA_LST_PSTT
```

### 5.5 여러 테이블 추출

테이블명은 공백 없이 쉼표로 구분합니다.

```bash
cdc_test_helper \
  -l '29028|3264' \
  -n 200 \
  -m dml \
  -T TABLE_A,TABLE_B,TABLE_C
```

### 5.6 DDL만 추출

DDL 로그를 추출하려면 DDL이 supplemental log에 기록되도록 서버가 설정되어 있어야 합니다.

```bash
cdc_test_helper \
  -t "2026-08-05 00:00:00" \
  -n 50 \
  -m ddl
```

### 5.7 설정 파일과 다른 데이터베이스 사용

명령의 마지막에 데이터베이스 이름을 지정하면 `database_name` 설정을 해당 실행에서만 덮어씁니다.

```bash
cdc_test_helper -n 10 -m dml another_db
```

### 5.8 결과 파일 저장

```bash
cdc_test_helper \
  -t "2026-08-05 04:00:00" \
  -n 100 \
  -m dml \
  2>&1 | tee cdc_result.log
```

## 6. 출력 건수 계산

`-n`은 다음 필터를 모두 통과해 실제 출력 대상으로 선택된 로그 아이템을 기준으로 계산합니다.

1. 설정 파일의 `extraction_users`
2. `-T` 테이블 필터
3. `-m` DML/DDL 필터
4. `ignore_trigger_dml`

예를 들어 다음 명령은 지정 시점 이후 전체 로그 100건이 아니라 `TABLE_A`의 DML 100건을 출력한 뒤 종료합니다.

```bash
cdc_test_helper -t "2026-08-05 04:00:00" -T TABLE_A -m dml -n 100
```

완료 시 다음 메시지가 출력됩니다.

```text
[CDC_DONE] output_log_items=100
```

## 7. 문제 해결

### 설정 파일을 찾지 못하는 경우

```text
[ERROR] Configuration file not found: .../cdc_test_helper.conf
```

실행 파일과 설정 파일이 같은 디렉터리에 있는지 확인합니다.

```bash
ls -l cdc_test_helper cdc_test_helper.conf
```

### CUBRID 설치 경로를 찾지 못하는 경우

```bash
export CUBRID=/실제/CUBRID/설치경로
export PATH="$CUBRID/bin:$PATH"
./install.sh
```

### CDC 서버에 연결할 수 없는 경우

다음을 확인합니다.

- `cdc_server_ip`
- `cdc_server_port`
- `database_name`
- `user`, `password`
- CUBRID 서버 실행 여부
- 방화벽 및 네트워크 연결

### 브로커 연결에 실패하는 경우

도구는 DML을 SQL로 변환하기 위해 현재 테이블 스키마를 브로커에서 조회합니다. 다음을 확인합니다.

- `broker_ip`
- `broker_port`
- 브로커 실행 여부
- 해당 브로커에서 대상 DB 접속 가능 여부

### 과거 로그의 DML이 정상적으로 해석되지 않는 경우

다음을 확인합니다.

- 로그 생성 당시와 현재 테이블의 컬럼 순서 및 타입이 동일한지
- 로그 생성 이후 컬럼 추가, 삭제 또는 타입 변경이 있었는지
- 대상 DB에 로그 생성 당시와 동일한 스키마가 준비되어 있는지

### `CUBRID_LOG_INVALID_LSA(-5)`

다음 가능성을 확인합니다.

- LSA의 `PAGEID|OFFSET` 형식이 올바른지
- CDC가 이전에 반환한 정상 LSA인지
- 해당 LSA의 활성 로그 또는 아카이브 로그가 남아 있는지
- LSA가 정확한 로그 레코드 경계를 가리키는지

### `CUBRID_LOG_EXTRACTION_TIMEOUT(-6)`

추출할 로그가 제한 시간 안에 준비되지 않은 경우 발생할 수 있습니다. `extraction_timeout`을 조정하거나 호출 측에서 재시도합니다.

## 8. 보안 및 관리

### 설정 파일 권한

```bash
chmod 600 cdc_test_helper.conf
```

### 비밀번호 저장

`cdc_test_helper.conf`는 저장소에 포함되는 파일입니다. 실제 비밀번호를 입력한 뒤 변경 내용을 commit하거나 외부 저장소에 push하지 않도록 주의하십시오.

```bash
git diff -- cdc_test_helper.conf
git status --short
```

### 업그레이드

```bash
git pull
./install.sh
```

설치 스크립트는 기존 설정 파일을 덮어쓰지 않고 현재 파일을 그대로 사용합니다.

### 설치 제거

```bash
rm -f "$HOME/.local/bin/cdc_test_helper"
rm -f ./cdc_test_helper
```

설정 파일은 자동으로 삭제하지 않습니다.

## 9. 도움말

설정 파일이 없어도 도움말은 확인할 수 있습니다.

```bash
cdc_test_helper --help
```
