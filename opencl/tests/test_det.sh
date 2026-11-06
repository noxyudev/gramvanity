#!/bin/sh
# EN: Integration tests for gramvanity's deterministic (resumable) search and target blocks. Run
# EN: from the opencl/ directory in an MSYS2 UCRT64 shell (or on Linux):  ./tests/test_det.sh
# EN: Needs a working OpenCL GPU (the binary refuses to run without one). External verifiers are
# EN: used when available; otherwise hit tests fall back to --no-verify (CPU re-derivation only).
# RU: Интеграционные тесты детерминированного (возобновляемого) поиска gramvanity и блоков целей.
# RU: Запускать из каталога opencl/ в оболочке MSYS2 UCRT64 (или на Linux):  ./tests/test_det.sh
# RU: Нужен рабочий GPU с OpenCL (без него бинарник отказывается работать). Внешние верификаторы
# RU: используются, когда доступны; иначе тесты хитов переходят на --no-verify (только пересчёт на CPU).
#
# EN: The assertions below match English messages and file text, so the suite pins the language.
# EN: Without this, a GRAMVANITY_LANG=ru in the environment would make the whole run fail.
# RU: Проверки ниже сравнивают английские сообщения и текст файлов, поэтому набор фиксирует язык.
# RU: Без этого GRAMVANITY_LANG=ru в окружении сломал бы весь прогон.
GRAMVANITY_LANG=en
export GRAMVANITY_LANG
set -u
cd "$(dirname "$0")/.." || exit 1

BIN=./gramvanity
[ -x "$BIN" ] || BIN=./gramvanity.exe
[ -x "$BIN" ] || { echo "FAIL: build gramvanity first (make)"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }

# EN: verifiers: use the Go verifier when present (as the README describes)
# RU: верификаторы: используем Go-верификатор, если он есть (как описано в README)
HAVE_GO=0
[ -f ../verify-go/w5addr ] || [ -f ../verify-go/w5addr.exe ] && HAVE_GO=1
if [ "$HAVE_GO" = 1 ]; then NOVERIFY=""; else NOVERIFY="--no-verify"; fi

next_index()  { "$BIN" --ckpt-info "$1" | grep -o 'next index [0-9]*' | grep -o '[0-9]*'; }
frontier()    { "$BIN" --ckpt-info "$1" | grep -o 'frontier [0-9]*'    | grep -o '[0-9]*'; }
unique_hits() { "$BIN" --ckpt-info "$1" | grep -o 'unique hits [0-9]*'  | grep -o '[0-9]*'; }
target_hits() { "$BIN" --ckpt-info "$1" | sed -n "s/^    $2: \([0-9][0-9]*\) hit(s).*/\1/p"; }

echo "== T0: unit tests =="
if "$BIN" --selftest-det > "$TMP/unit.log" 2>&1; then ok "selftest-det"; else bad "selftest-det"; cat "$TMP/unit.log"; fi

echo "== T1: stop-after, resume, range completion (bip39-12) =="
CK="$TMP/t1.ckpt"; OUT="$TMP/t1-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 1000 --tries 100000 \
       --stop-after 30000 --checkpoint-interval 5 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t1a.log" 2>&1
N1=$(next_index "$CK")
if [ -n "$N1" ] && [ "$N1" -ge 31000 ] && [ "$N1" -le 60000 ]; then ok "stop-after near 30000 (next=$N1)"; else bad "stop-after (next='$N1')"; fi
"$BIN" --resume "$CK" $NOVERIFY > "$TMP/t1b.log" 2>&1
N2=$(next_index "$CK")
check "resume completes the range" "$N2" "101000"
if [ ! -f "$OUT/job.ckpt" ]; then ok "explicit --checkpoint does not create a second checkpoint in the job folder"; else bad "explicit --checkpoint does not create a second checkpoint in the job folder"; fi

echo "== T2: verified hit, target name in the file, key+index output, replay (bip39-12) =="
CK="$TMP/t2.ckpt"; OUT="$TMP/t2-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 100000000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" > "$TMP/t2.log" 2>&1
NEW=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
HIT="$OUT/$NEW"
case "$NEW" in
    hit-t1-*) ok "single target is named t1 in the hit file ($NEW)";;
    *) bad "single target hit file name (got '$NEW')";;
esac
if [ -n "$NEW" ]; then
    grep -q "HIT" "$HIT" && ok "hit file marked HIT" || bad "hit file marked HIT"
    grep -q "matched target t1:" "$HIT" && ok "hit file names the matched target" || bad "hit file names the matched target"
    grep -q "candidate index:" "$HIT" && ok "hit file has the index" || bad "hit file has the index"
    grep -q "search key (SECRET" "$HIT" && ok "hit file has the search key" || bad "hit file has the search key"
    if grep -q "phrase (SECRET" "$HIT"; then bad "hit file leaks the phrase"; else ok "hit file stores no phrase"; fi
    KEY=$(grep -o 'derive [0-9a-f]\{64\}' "$HIT" | grep -o '[0-9a-f]\{64\}')
    IDX=$(grep -o 'candidate index: [0-9]*' "$HIT" | grep -o '[0-9]*')
    A_HIT=$(grep -o 'address UQ[^:]*: [A-Za-z0-9_-]\{48\}' "$HIT" | grep -o '[A-Za-z0-9_-]\{48\}')
    A_DER=$("$BIN" --mode bip39-12 --derive "$KEY" "$IDX" | grep -o 'address UQ: [A-Za-z0-9_-]\{48\}' | grep -o '[A-Za-z0-9_-]\{48\}')
    A_REV=$("$BIN" --reveal "$CK" "$IDX" | grep -o 'address UQ: [A-Za-z0-9_-]\{48\}' | grep -o '[A-Za-z0-9_-]\{48\}')
    check "--derive replays the address" "$A_DER" "$A_HIT"
    check "--reveal replays the address" "$A_REV" "$A_HIT"
    grep -q "tonutils-go OK" "$HIT" && ok "independent verifier ran" || ok "verifier absent, CPU re-derivation only"
fi

echo "== T3: corrupted / conflicting checkpoints =="
CK="$TMP/t3.ckpt"; OUT="$TMP/t3-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 100000 --stop-after 40000 \
       --checkpoint-interval 5 --no-verify --checkpoint "$CK" --out "$OUT" > /dev/null 2>&1
cp "$CK" "$TMP/t3bad.ckpt"
printf 'x' | dd of="$TMP/t3bad.ckpt" bs=1 seek=100 conv=notrunc 2>/dev/null
if "$BIN" --resume "$TMP/t3bad.ckpt" > "$TMP/t3.log" 2>&1; then bad "corrupted checkpoint rejected"; else ok "corrupted checkpoint rejected"; fi
if "$BIN" --resume "$CK" --suffix OTHER > "$TMP/t3c.log" 2>&1; then bad "global suffix conflict rejected"; else ok "global suffix conflict rejected"; fi
if "$BIN" --resume "$CK" --target t1 --suffix OTHER > "$TMP/t3d.log" 2>&1; then bad "target suffix conflict rejected"; else ok "target suffix conflict rejected"; fi

echo "== T4: ton-24 deterministic range with interruption =="
CK="$TMP/t4.ckpt"; OUT="$TMP/t4-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode ton-24 --suffix ZZZZZZ --start-offset 500 --tries 60000 --stop-after 40000 \
       --checkpoint-interval 3 --no-verify --checkpoint "$CK" --out "$OUT" > "$TMP/t4a.log" 2>&1
F1=$(frontier "$CK")
if [ -n "$F1" ] && [ "$F1" -ge 40000 ] && [ "$F1" -le 90000 ]; then ok "ton stop-after frontier (=$F1)"; else bad "ton stop-after frontier (='$F1')"; fi
"$BIN" --resume "$CK" --no-verify > "$TMP/t4b.log" 2>&1
check "ton range completes after resume" "$(next_index "$CK")" "60500"

echo "== T5: multiple suffix alternatives (OR inside a group) =="
CK="$TMP/t5.ckpt"; OUT="$TMP/t5-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix 'ZZZZZ' --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 100000 \
       --count 1 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5.log" 2>&1
NEW5=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
case "$NEW5" in
    hit-t1-*) ok "multi-suffix hit keeps the target name ($NEW5)";;
    *) bad "multi-suffix hit file (got '$NEW5')";;
esac
grep -q 'endings "ZZZZZ" "\[0-9A-Za-z_-\]" (one from each group; AND)' "$OUT/$NEW5" \
    && ok "hit file lists both suffix alternatives" || bad "hit file lists both suffix alternatives"
"$BIN" --ckpt-info "$CK" | grep -q 'endings: "ZZZZZ" ([0-9]* hit(s)), "\[0-9A-Za-z_-\]" ([0-9]* hit(s))' \
    && ok "suffix alternatives survive the checkpoint" || bad "suffix alternatives survive the checkpoint"

echo "== T5b: prefix patterns, ignore-case, checkpoint roundtrip =="
CK="$TMP/t5b.ckpt"; OUT="$TMP/t5b-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --prefix AB,CD --start-offset 0 --tries 2000000 --count 1 \
       $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5b.log" 2>&1
NEW5B=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
if [ -n "$NEW5B" ]; then
    A5B=$(grep -o 'address UQ[^:]*: [A-Za-z0-9_-]\{48\}' "$OUT/$NEW5B" | grep -o '[A-Za-z0-9_-]\{48\}')
    case "$A5B" in
        UQAB*|UQCD*) ok "prefix address starts with one of the alternatives after UQ";;
        *) bad "prefix address (got '$A5B')";;
    esac
    grep -q 'matched target t1: prefixes "AB" "CD" (one from each group; AND)' "$OUT/$NEW5B" \
        && ok "hit file lists both prefix alternatives" || bad "hit file lists both prefix alternatives"
else
    bad "prefix hit file"
fi
"$BIN" --ckpt-info "$CK" | grep -q 'prefixes: "AB" ([0-9]* hit(s)), "CD" ([0-9]* hit(s))' && ok "prefixes survive the checkpoint" || bad "prefixes survive the checkpoint"
if "$BIN" --resume "$CK" --prefix EF > "$TMP/t5bc.log" 2>&1; then bad "prefix conflict rejected"; else ok "prefix conflict rejected"; fi
CK="$TMP/t5c.ckpt"; OUT="$TMP/t5c-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --prefix ab --ignore-case --start-offset 0 --tries 2000000 --count 1 \
       $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5c.log" 2>&1
NEW5C=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
if [ -n "$NEW5C" ]; then
    A5C=$(grep -o 'address UQ[^:]*: [A-Za-z0-9_-]\{48\}' "$OUT/$NEW5C" | grep -o '[A-Za-z0-9_-]\{48\}')
    case "$A5C" in
        UQ[Aa][Bb]*) ok "--ignore-case applies to a prefix given earlier on the command line";;
        *) bad "--ignore-case prefix address (got '$A5C')";;
    esac
else
    bad "ignore-case prefix hit file"
fi

echo "== T5f: --match or: either group alone is a hit; conflict and override =="
CK="$TMP/t5f.ckpt"; OUT="$TMP/t5f-out"; rm -f "$CK"; rm -rf "$OUT"
# EN: AND would need AB...ZZZZZZZ (~2^50): any hit in 2M tries proves the prefix alone matched
# RU: AND потребовал бы AB...ZZZZZZZ (~2^50): любой хит за 2M попыток доказывает, что совпал префикс
"$BIN" --mode bip39-12 --match or --start-offset 0 --tries 2000000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" --prefix AB --suffix 'ZZZZZZZ' > "$TMP/t5f.log" 2>&1
NEW5F=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
if [ -n "$NEW5F" ]; then
    A5F=$(grep -o 'address UQ[^:]*: [A-Za-z0-9_-]\{48\}' "$OUT/$NEW5F" | grep -o '[A-Za-z0-9_-]\{48\}')
    case "$A5F" in
        UQAB*|*ZZZZZZZ) ok "--match or: an address matching one group is a hit";;
        *) bad "--match or hit address (got '$A5F')";;
    esac
    grep -q 'prefix "AB", ending "ZZZZZZZ" (one group suffices; OR)' "$OUT/$NEW5F" \
        && ok "hit file names the OR combination" || bad "hit file names the OR combination"
else
    bad "--match or hit file"
fi
"$BIN" --ckpt-info "$CK" | grep -q 'prefix OR suffix (--match or)' \
    && ok "--match or survives the checkpoint" || bad "--match or survives the checkpoint"
if "$BIN" --resume "$CK" --match and --tries 10 > "$TMP/t5fc.log" 2>&1; then bad "--match conflict rejected"; else ok "--match conflict rejected"; fi
"$BIN" --resume "$CK" --tries 10 --count 2 $NOVERIFY > "$TMP/t5fr.log" 2>&1 \
    && ok "resume keeps the saved --match or" || bad "resume keeps the saved --match or"
# EN: a per-target --match overrides the global default: A stays or, B is and
# RU: --match внутри цели переопределяет глобальное значение: у A остаётся or, у B — and
CK="$TMP/t5g.ckpt"; OUT="$TMP/t5g-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --match or --start-offset 0 --tries 200000 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --prefix AB --suffix 'ZZZZZZZ' \
       --target B --match and --suffix 'ZZ' > "$TMP/t5g.log" 2>&1
NH5G=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "per-target override finds hits for both targets" "$NH5G" "2"
grep -q '(one group suffices; OR)' "$OUT"/hit-A-* \
    && ok "target A keeps the global --match or" || bad "target A keeps the global --match or"
grep -q '(one from each group; AND)' "$OUT"/hit-B-* \
    && ok "target B overrides with --match and" || bad "target B overrides with --match and"
check "only target A carries the OR flag" "$("$BIN" --ckpt-info "$CK" | grep -c 'prefix OR suffix (--match or)')" "1"

echo "== T5g2: Cyrillic target names are transliterated and kept as labels =="
CK="$TMP/t5i.ckpt"; OUT="$TMP/t5i-out"; rm -f "$CK"; rm -rf "$OUT"
CYR=$(printf '\320\237\320\260\321\210\320\260') # Паша
"$BIN" --mode bip39-12 --start-offset 0 --tries 200000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" --target "$CYR" --prefix AB > "$TMP/t5i.log" 2>&1
H5I=$(ls "$OUT" 2>/dev/null | grep '^hit-Pasha-' | head -1)
if [ -n "$H5I" ] && grep -q "target Pasha ($CYR)" "$OUT/$H5I"; then
    ok "Cyrillic name becomes the ASCII id Pasha and stays as its label"
else
    bad "Cyrillic name becomes the ASCII id Pasha and stays as its label"
fi
"$BIN" --ckpt-info "$CK" | grep -q "label: $CYR" \
    && ok "Cyrillic label survives the checkpoint" || bad "Cyrillic label survives the checkpoint"
# EN: resume by the Cyrillic label (raise the count so the run actually starts)
# RU: продолжаем по кириллической метке (поднимаем счётчик, чтобы запуск действительно начался)
OUT5I="$TMP/t5i-out2"; rm -rf "$OUT5I"
"$BIN" --resume "$CK" --count 2 --tries 10 $NOVERIFY --out "$OUT5I" --target "$CYR" > "$TMP/t5ir.log" 2>&1 \
    && ok "resume finds a target by its Cyrillic label" || bad "resume finds a target by its Cyrillic label"

echo "== T5h: --all-patterns: --count applies per pattern variant =="
CK="$TMP/t5h.ckpt"; OUT="$TMP/t5h-out"; rm -f "$CK"; rm -rf "$OUT"
# EN: two disjoint, easily findable prefix variants: the run must not stop after the first one
# RU: два непересекающихся легко находимых варианта префикса: запуск не должен останавливаться на первом
"$BIN" --mode bip39-12 --all-patterns --start-offset 0 --tries 2000000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" --prefix AB --prefix CD > "$TMP/t5h.log" 2>&1
NH5H=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "--all-patterns keeps searching until every variant has its hit" "$NH5H" "2"
"$BIN" --ckpt-info "$CK" | grep -q -- '--all-patterns' \
    && ok "--all-patterns survives the checkpoint" || bad "--all-patterns survives the checkpoint"
PH5H=$("$BIN" --ckpt-info "$CK" | sed -n 's/^      prefixes: //p')
case "$PH5H" in
    *'"AB" (1 hit(s))'*'"CD" (1 hit(s))') ok "per-pattern hit counts survive the checkpoint";;
    *) bad "per-pattern hit counts (got '$PH5H')";;
esac
grep -q 'per-pattern hits: prefix "AB" 1/1' "$OUT"/stop-* 2>/dev/null \
    && ok "stop report lists per-pattern progress" || bad "stop report lists per-pattern progress"
# EN: resuming with a raised count keeps the restored per-pattern counters (1 of the new 2 already there)
# RU: продолжение с увеличенным счётчиком сохраняет восстановленные счётчики по шаблонам (1 из новых 2 уже есть)
OUT5H="$TMP/t5h-out2"; rm -rf "$OUT5H"
"$BIN" --resume "$CK" --count 2 --tries 10 $NOVERIFY --out "$OUT5H" > "$TMP/t5hr.log" 2>&1
SR5H=$(ls "$OUT5H"/stop-* 2>/dev/null | head -1)
# EN: the restored counters must be nonzero; a rare extra hit during the 10 tries may make them 2/2
# RU: восстановленные счётчики должны быть ненулевыми; редкий лишний хит за 10 попыток даёт 2/2
if [ -n "$SR5H" ] && grep -q 'prefix "AB" [1-9]/2' "$SR5H" && grep -q 'prefix "CD" [1-9]/2' "$SR5H"; then
    ok "resume keeps the per-pattern counters with the raised --count"
else
    bad "resume keeps the per-pattern counters"
fi

echo "== T5d: no fixed pattern count limit =="
CK="$TMP/t5d.ckpt"; OUT="$TMP/t5d-out"; rm -f "$CK"; rm -rf "$OUT"
PFX=""; I=0
while [ "$I" -lt 12 ]; do PFX="${PFX:+$PFX,}[0-9A-Za-z_-]"; I=$((I+1)); done
"$BIN" --mode bip39-12 --prefix "$PFX" --start-offset 0 --tries 100000 --count 1 \
       $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5d.log" 2>&1
NEW5D=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
if [ -n "$NEW5D" ]; then ok "twelve prefixes work at once ($NEW5D)"; else bad "twelve prefixes work at once"; fi
NP5D=$("$BIN" --ckpt-info "$CK" | sed -n 's/^      prefixes: //p' | grep -o '([0-9]* hit(s))' | wc -l | tr -d ' ')
check "twelve prefixes survive the checkpoint" "$NP5D" "12"

echo "== T5e: default count is one hit per target =="
CK="$TMP/t5e.ckpt"; OUT="$TMP/t5e-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --prefix AB --start-offset 0 --tries 100000 --no-verify \
       --checkpoint "$CK" --out "$OUT" > "$TMP/t5e.log" 2>&1
NH5E=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "a target without --count stops after one hit" "$NH5E" "1"
N5E=$(next_index "$CK")
if [ -n "$N5E" ] && [ "$N5E" -lt 100000 ]; then ok "the default count stops the run early (next=$N5E)"; else bad "the default count stops the run early (next='$N5E')"; fi
grep -q 'all target counts reached' "$OUT"/stop-* 2>/dev/null && ok "the default count ends the run" || bad "the default count ends the run"

echo "== T6: tg-wallet (WalletTg) hit, replay, signing half, testnet =="
CK="$TMP/t6.ckpt"; OUT="$TMP/t6-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode tg-wallet --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 100000000 --count 1 \
       $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t6.log" 2>&1
NEW6=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
HIT6="$OUT/$NEW6"
case "$NEW6" in
    hit-t1-*) ok "tg hit keeps the target name ($NEW6)";;
    *) bad "tg hit file name (got '$NEW6')";;
esac
if [ -n "$NEW6" ]; then
    grep -q "two BIP39-12 halves" "$HIT6" && ok "tg hit describes the two halves" || bad "tg hit describes the two halves"
    grep -q "wallet: WalletTg (Telegram Wallet), mainnet" "$HIT6" && ok "tg hit names the wallet and network" || bad "tg hit names the wallet and network"
    grep -q "w5addr.*activate" "$HIT6" && ok "tg hit tells how to activate" || bad "tg hit tells how to activate"
    KEY6=$(grep -o 'derive [0-9a-f]\{64\}' "$HIT6" | grep -o '[0-9a-f]\{64\}')
    IDX6=$(grep -o 'candidate index: [0-9]*' "$HIT6" | grep -o '[0-9]*')
    A_HIT6=$(grep -o 'address UQ[^:]*: [A-Za-z0-9_-]\{48\}' "$HIT6" | grep -o '[A-Za-z0-9_-]\{48\}')
    A_DER6=$("$BIN" --mode tg-wallet --derive "$KEY6" "$IDX6" | grep -o 'address UQ: [A-Za-z0-9_-]\{48\}' | grep -o '[A-Za-z0-9_-]\{48\}')
    A_REV6=$("$BIN" --reveal "$CK" "$IDX6" | grep -o 'address UQ: [A-Za-z0-9_-]\{48\}' | grep -o '[A-Za-z0-9_-]\{48\}')
    check "tg --derive replays the address" "$A_DER6" "$A_HIT6"
    check "tg --reveal replays the address" "$A_REV6" "$A_HIT6"
    D_OUT=$("$BIN" --mode tg-wallet --derive "$KEY6" "$IDX6")
    S_DER=$(printf '%s\n' "$D_OUT" | grep -o 'signing public key[^:]*: [0-9a-f]\{64\}' | grep -o '[0-9a-f]\{64\}')
    S_REV=$("$BIN" --reveal "$CK" "$IDX6" | grep -o 'signing public key[^:]*: [0-9a-f]\{64\}' | grep -o '[0-9a-f]\{64\}')
    check "tg signing half is reproducible (derive == reveal)" "$S_DER" "$S_REV"
    if [ -n "$S_DER" ]; then ok "tg reports the signing public key"; else bad "tg reports the signing public key"; fi
    NW=$(printf '%s\n' "$D_OUT" | grep 'phrase (SECRET' | sed 's/.*: //' | wc -w | tr -d ' ')
    check "tg phrase has 24 words" "$NW" "24"
fi
T_DER=$("$BIN" --mode tg-wallet --net testnet --derive "$KEY6" "$IDX6" | grep -o 'address 0Q: [A-Za-z0-9_-]\{48\}' | grep -o '[A-Za-z0-9_-]\{48\}')
if [ -n "$T_DER" ] && [ "$T_DER" != "$A_HIT6" ]; then ok "testnet uses the testnet subwallet (0Q form)"; else bad "testnet uses the testnet subwallet (0Q form)"; fi
CK6T="$TMP/t6t.ckpt"; OUT="$TMP/t6t-out"; rm -f "$CK6T"; rm -rf "$OUT"
"$BIN" --mode tg-wallet --net testnet --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 1000000 --count 1 \
       $NOVERIFY --checkpoint "$CK6T" --out "$OUT" > "$TMP/t6t.log" 2>&1
"$BIN" --ckpt-info "$CK6T" | grep -q "WalletTg (Telegram Wallet) testnet" && ok "testnet flag survives the checkpoint" || bad "testnet flag survives the checkpoint"
if "$BIN" --mode bip39-12 --net testnet --suffix X --start-offset 0 --out "$TMP/t6v-out" > "$TMP/t6v.log" 2>&1; then
    bad "--net testnet rejected for other modes"
else
    ok "--net testnet rejected for other modes"
fi

echo "== T7: --power validation, determinism across power levels =="
if "$BIN" --power 0 --suffix ZZZZZZ --out "$TMP/t7bad" > "$TMP/t7a.log" 2>&1; then bad "--power 0 rejected"; else ok "--power 0 rejected"; fi
if "$BIN" --power 101 --suffix ZZZZZZ --out "$TMP/t7bad" > "$TMP/t7b.log" 2>&1; then bad "--power 101 rejected"; else ok "--power 101 rejected"; fi
CK="$TMP/t7.ckpt"; OUT="$TMP/t7-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 4000 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t7c.log" 2>&1
cp "$CK" "$TMP/t7b.ckpt"
"$BIN" --resume "$CK" --tries 8000 --power 100 $NOVERIFY > "$TMP/t7d.log" 2>&1
"$BIN" --resume "$TMP/t7b.ckpt" --tries 8000 --power 20 $NOVERIFY > "$TMP/t7e.log" 2>&1
check "power 100 processes the same range" "$(next_index "$CK")" "12000"
check "power 20 processes the same range" "$(next_index "$TMP/t7b.ckpt")" "12000"
P1=$("$BIN" --reveal "$CK" 5000 | grep 'phrase (SECRET' | sed 's/.*: //')
P2=$("$BIN" --reveal "$TMP/t7b.ckpt" 5000 | grep 'phrase (SECRET' | sed 's/.*: //')
if [ -n "$P1" ] && [ "$P1" = "$P2" ]; then ok "same index gives the same phrase at any power"; else bad "same index gives the same phrase at any power"; fi

echo "== T8: target blocks, per-target counts, shared and separate outs =="
# EN: a) both targets match every address; one address, one shared hit file
# RU: a) обе цели совпадают с любым адресом; один адрес — один общий hit-файл
CK="$TMP/t8a.ckpt"; OUT="$TMP/t8a-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200 --count 1 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' --target B --suffix '[0-9A-Za-z_-]' > "$TMP/t8a.log" 2>&1
FILES=$(ls "$OUT" 2>/dev/null | grep '^hit-' | sort | tr '\n' ',')
check "two targets matching one address write one shared hit file" "$FILES" "hit-A+B-0.txt,"
check "shared hit counts as one unique hit" "$(unique_hits "$CK")" "1"
check "both targets count the shared hit" "$(target_hits "$CK" A)/$(target_hits "$CK" B)" "1/1"
grep -q 'matched target A:' "$OUT/hit-A+B-0.txt" && grep -q 'matched target B:' "$OUT/hit-A+B-0.txt" \
    && ok "shared hit file lists both targets" || bad "shared hit file lists both targets"

# EN: b) separate per-target out directories get their own copy of the same multi-target hit
# RU: b) отдельные папки --out на цель получают свою копию одного и того же мультицелевого хита
CK="$TMP/t8b.ckpt"; OUT="$TMP/t8b-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200 --count 1 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' --out "$OUT/a" \
       --target B --suffix '[0-9A-Za-z_-]' --out "$OUT/b" > "$TMP/t8b.log" 2>&1
[ -f "$OUT/a/hit-A+B-0.txt" ] && ok "target A writes to its own --out" || bad "target A writes to its own --out"
[ -f "$OUT/b/hit-A+B-0.txt" ] && ok "target B writes to its own --out" || bad "target B writes to its own --out"
ls "$OUT" | grep -q '^stop-' && ok "the stop report stays in the job folder" || bad "the stop report stays in the job folder"
[ -f "$TMP/t8b.ckpt" ] && ok "explicit checkpoint stays outside the job folder" || bad "explicit checkpoint stays outside the job folder"

# EN: c) per-target counts: A stops after one hit, B after two (the range stays open, so the run ends
# EN:    because both target counts are reached, not because the range ended)
# RU: c) счётчики на цель: A останавливается после одного хита, B — после двух (диапазон открыт,
# RU:    поэтому запуск завершается по достижению счётчиков, а не по концу диапазона)
CK="$TMP/t8c.ckpt"; OUT="$TMP/t8c-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200000 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' --count 1 \
       --target B --suffix '[0-9A-Za-z_-]' --count 2 > "$TMP/t8c.log" 2>&1
check "a finished target stops counting, the other keeps going" \
      "$(ls "$OUT" 2>/dev/null | grep -c '^hit-A+B-')/$(ls "$OUT" 2>/dev/null | grep -c '^hit-B-')" "1/1"
check "per-target counters" "$(target_hits "$CK" A)/$(target_hits "$CK" B)" "1/2"
check "two saved addresses = two unique hits" "$(unique_hits "$CK")" "2"
grep -q 'all target counts reached' "$OUT"/stop-* 2>/dev/null && ok "stop report says all target counts were reached" || bad "stop report says all target counts were reached"

# EN: d) resume and raise one target's count: no rewind, the other target is unchanged
# RU: d) продолжаем и повышаем счётчик одной цели: отката нет, вторая цель не меняется
"$BIN" --mode bip39-12 --resume "$CK" --tries 10 --no-verify --target A --count 3 > "$TMP/t8d.log" 2>&1
check "resume continues after the last checked index" "$(next_index "$CK")" "16394"
check "raised count is applied to the named target only" "$(target_hits "$CK" A)/$(target_hits "$CK" B)" "3/2"
check "unique hits after resume" "$(unique_hits "$CK")" "4"
check "resume wrote exactly the two new hits" "$(ls "$OUT" 2>/dev/null | grep -c '^hit-')" "4"
check "the new hits belong to the raised target" "$(ls "$OUT" 2>/dev/null | grep -c '^hit-A-')" "2"
check "the finished target and the shared file were not written again" \
      "$(ls "$OUT" 2>/dev/null | grep -c '^hit-B-')/$(ls "$OUT" 2>/dev/null | grep -c '^hit-A+B-')" "1/1"
grep -q 'target "A"' "$TMP/t8d.log" && ok "resume reports the named target" || bad "resume reports the named target"

# EN: e) A and B reach their counts on one address; C has not matched and keeps the run going
# RU: e) A и B добирают счётчики на одном адресе; C не совпала и продолжает запуск
CK="$TMP/t8e.ckpt"; OUT="$TMP/t8e-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200000 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' --count 1 \
       --target B --suffix '[0-9A-Za-z_-]' --count 1 \
       --target C --suffix ZZZZZ --count 1 > "$TMP/t8e.log" 2>&1
check "A and B are done, C is not" "$(target_hits "$CK" A)/$(target_hits "$CK" B)/$(target_hits "$CK" C)" "1/1/0"
check "the shared A+B hit counts once" "$(unique_hits "$CK")" "1"
check "one shared hit file for the A+B address" "$(ls "$OUT" 2>/dev/null | grep -c '^hit-')" "1"
[ -f "$OUT/hit-A+B-0.txt" ] && ok "the A+B hit file is hit-A+B-0.txt" || bad "the A+B hit file is hit-A+B-0.txt"
grep -q 'range complete' "$OUT"/stop-* 2>/dev/null && ok "the run continued for C after A and B finished" || bad "the run continued for C after A and B finished"
# EN: the startup estimate of a resumed job must ignore the two finished targets
# RU: стартовая оценка возобновлённого задания должна игнорировать две завершённые цели
"$BIN" --mode bip39-12 --resume "$CK" --tries 10 --no-verify > "$TMP/t8e2.log" 2>&1
grep -q 'across the 1 remaining target(s)' "$TMP/t8e2.log" \
    && ok "resume odds count only the remaining targets" || bad "resume odds count only the remaining targets"

# EN: f) unnamed blocks become t1, t2, ...; a duplicate name gets a numeric suffix
# RU: f) блоки без имени становятся t1, t2, ...; повтор имени получает числовой суффикс
CK="$TMP/t8f.ckpt"; OUT="$TMP/t8f-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200000 --count 1 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' \
       --target --suffix '[0-9A-Za-z_-]' \
       --target A --suffix '[0-9A-Za-z_-]' > "$TMP/t8f.log" 2>&1
check "unnamed and duplicate target names in the hit file" "$(ls "$OUT" 2>/dev/null | grep '^hit-')" "hit-A+t1+A2-0.txt"
check "the global --count default applies to every target" \
      "$(target_hits "$CK" A)/$(target_hits "$CK" t1)/$(target_hits "$CK" A2)" "1/1/1"

echo "== T9: default job folder =="
BEFORE="$TMP/jobs-before"; AFTER="$TMP/jobs-after"
ls -d ../results/job-*/ 2>/dev/null | sort > "$BEFORE" || : > "$BEFORE"
"$BIN" --mode bip39-12 --tries 1 --count 1 --no-verify --target C --suffix '[0-9A-Za-z_-]' > "$TMP/t9.log" 2>&1
ls -d ../results/job-*/ 2>/dev/null | sort > "$AFTER" || : > "$AFTER"
NEWJOB=$(comm -13 "$BEFORE" "$AFTER" | head -1)
if [ -n "$NEWJOB" ]; then
    ok "a run without --out creates results/job-<timestamp>/ ($NEWJOB)"
    [ -f "$NEWJOB/job.ckpt" ] && ok "the default checkpoint is <job>/job.ckpt" || bad "the default checkpoint is <job>/job.ckpt"
    [ -f "$NEWJOB/hit-C-0.txt" ] && ok "the default hit directory is the job folder" || bad "the default hit directory is the job folder"
    ls "$NEWJOB" | grep -q '^stop-' && ok "the stop report is in the job folder" || bad "the stop report is in the job folder"
    rm -rf "$NEWJOB"
else
    bad "a run without --out creates results/job-<timestamp>/"
fi

# EN: selftest leaves its (empty) checkpoint folder behind; remove it if this run created it
# RU: self-test оставляет после себя (пустую) папку checkpoint; удаляем её, если её создал этот запуск
rmdir ../results/job-selftest 2>/dev/null

echo "== T10: deterministic by default, --random opt-out =="
OUT="$TMP/t10a-out"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --stop-after 4000 --no-verify --out "$OUT" > "$TMP/t10a.log" 2>&1
[ -f "$OUT/job.ckpt" ] && ok "a default job is deterministic and writes job.ckpt" || bad "a default job is deterministic and writes job.ckpt"
grep -q 'deterministic range' "$TMP/t10a.log" && ok "a default job reports a deterministic range" || bad "a default job reports a deterministic range"
grep -q 'deterministic search: indices' "$OUT"/stop-* 2>/dev/null && ok "the stop report of a default job is resumable" || bad "the stop report of a default job is resumable"
N10=$(next_index "$OUT/job.ckpt")
"$BIN" --mode bip39-12 --resume "$OUT/job.ckpt" --stop-after 1 --no-verify > "$TMP/t10b.log" 2>&1
grep -q 'resuming bip39-12 search' "$TMP/t10b.log" && ok "a default job resumes without extra flags" || bad "a default job resumes without extra flags"
N10B=$(next_index "$OUT/job.ckpt")
if [ -n "$N10" ] && [ -n "$N10B" ] && [ "$N10B" -gt "$N10" ]; then ok "resume advanced the default job"; else bad "resume advanced the default job"; fi

OUT="$TMP/t10c-out"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --random --suffix '[0-9A-Za-z_-]' --count 1 --no-verify --out "$OUT" > "$TMP/t10c.log" 2>&1
[ ! -f "$OUT/job.ckpt" ] && ok "--random writes no checkpoint" || bad "--random writes no checkpoint"
grep -q 'random mode: fresh OS randomness' "$TMP/t10c.log" && ok "--random announces random mode" || bad "--random announces random mode"
grep -q 'random mode: progress cannot be resumed' "$OUT"/stop-* 2>/dev/null && ok "--random stop report says it cannot be resumed" || bad "--random stop report says it cannot be resumed"
H10=$(ls "$OUT"/hit-* 2>/dev/null | head -1)
if [ -n "$H10" ] && grep -q 'phrase (.*SECRET' "$H10" && ! grep -q 'search key' "$H10"; then
    ok "--random hit file stores the phrase and no search key"
else
    bad "--random hit file stores the phrase and no search key"
fi
if "$BIN" --random --start-offset 0 --suffix ZZZ --out "$TMP/t10d-out" > "$TMP/t10d.log" 2>&1; then
    bad "--random rejects --start-offset"
else
    ok "--random rejects --start-offset"
fi

echo "== T11: defaults: tg-wallet, one hit per target, --power 65 =="
OUT="$TMP/t11-out"; rm -rf "$OUT"
"$BIN" --suffix '[0-9A-Za-z_-]' --no-verify --out "$OUT" > "$TMP/t11.log" 2>&1
grep -q 'mode tg-wallet' "$TMP/t11.log" && ok "tg-wallet is the default mode" || bad "tg-wallet is the default mode"
grep -q 'power 65%' "$TMP/t11.log" && ok "power defaults to 65%" || bad "power defaults to 65%"
grep -q 'stop after 1 hit(s)' "$TMP/t11.log" && ok "a target defaults to --count 1" || bad "a target defaults to --count 1"
H11=$(ls "$OUT"/hit-* 2>/dev/null | head -1)
if [ -n "$H11" ] && grep -q 'two BIP39-12 halves' "$H11"; then
    ok "the default hit is a tg-wallet phrase"
else
    bad "the default hit is a tg-wallet phrase"
fi
grep -q 'stopped: all target counts reached' "$TMP/t11.log" \
    && ok "the default count ends the run after the first hit" || bad "the default count ends the run after the first hit"

OUT="$TMP/t11b-out"; rm -rf "$OUT"
"$BIN" --no-verify --out "$OUT" --target A --suffix '[0-9A-Za-z_-]' --target B --suffix '[0-9A-Za-z_-]' \
       > "$TMP/t11b.log" 2>&1
if [ -f "$OUT/hit-A+B-0.txt" ]; then ok "the default count applies to every target of a multi-target run"; else bad "the default count applies to every target of a multi-target run"; fi
grep -q 'stopped: all target counts reached' "$TMP/t11b.log" \
    && ok "a multi-target run also stops at the default count" || bad "a multi-target run also stops at the default count"

echo
echo "passed $PASS, failed $FAIL"
[ "$FAIL" -eq 0 ]
