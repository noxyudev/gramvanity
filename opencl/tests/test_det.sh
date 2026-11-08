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

next_index()  { "$BIN" --checkpoint-info "$1" | grep -o 'next index [0-9]*' | grep -o '[0-9]*'; }
frontier()    { "$BIN" --checkpoint-info "$1" | grep -o 'frontier [0-9]*'    | grep -o '[0-9]*'; }
unique_hits() { "$BIN" --checkpoint-info "$1" | grep -o 'unique hits [0-9]*'  | grep -o '[0-9]*'; }
target_hits() { "$BIN" --checkpoint-info "$1" | sed -n "s/^    $2: \([0-9][0-9]*\) hit(s).*/\1/p"; }

echo "== T0: unit tests =="
if "$BIN" --selftest-det > "$TMP/unit.log" 2>&1; then ok "selftest-det"; else bad "selftest-det"; cat "$TMP/unit.log"; fi

echo "== T1: stop-after, resume, range completion (bip39-12) =="
CK="$TMP/t1.checkpoint"; OUT="$TMP/t1-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 1000 --tries 100000 \
       --stop-after 30000 --checkpoint-interval 5 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t1a.log" 2>&1
N1=$(next_index "$CK")
if [ -n "$N1" ] && [ "$N1" -ge 31000 ] && [ "$N1" -le 60000 ]; then ok "stop-after near 30000 (next=$N1)"; else bad "stop-after (next='$N1')"; fi
"$BIN" --resume "$CK" $NOVERIFY > "$TMP/t1b.log" 2>&1
N2=$(next_index "$CK")
check "resume completes the range" "$N2" "101000"
if [ ! -f "$OUT/job.checkpoint" ]; then ok "explicit --checkpoint does not create a second checkpoint in the job folder"; else bad "explicit --checkpoint does not create a second checkpoint in the job folder"; fi

echo "== T2: verified hit, target name in the file, key+index output, replay (bip39-12) =="
CK="$TMP/t2.checkpoint"; OUT="$TMP/t2-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK="$TMP/t3.checkpoint"; OUT="$TMP/t3-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 100000 --stop-after 40000 \
       --checkpoint-interval 5 --no-verify --checkpoint "$CK" --out "$OUT" > /dev/null 2>&1
cp "$CK" "$TMP/t3bad.checkpoint"
printf 'x' | dd of="$TMP/t3bad.checkpoint" bs=1 seek=100 conv=notrunc 2>/dev/null
if "$BIN" --resume "$TMP/t3bad.checkpoint" > "$TMP/t3.log" 2>&1; then bad "corrupted checkpoint rejected"; else ok "corrupted checkpoint rejected"; fi
if "$BIN" --resume "$CK" --suffix OTHER > "$TMP/t3c.log" 2>&1; then bad "global suffix conflict rejected"; else ok "global suffix conflict rejected"; fi
if "$BIN" --resume "$CK" --target t1 --suffix OTHER > "$TMP/t3d.log" 2>&1; then bad "target suffix conflict rejected"; else ok "target suffix conflict rejected"; fi

echo "== T3b: the text format: hand edits and --checkpoint-fix =="
CK="$TMP/t3b.checkpoint"; OUT="$TMP/t3b-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 100000 --stop-after 40000 \
       --checkpoint-interval 5 --no-verify --checkpoint "$CK" --out "$OUT" > /dev/null 2>&1
# EN: the file is plain text: the format line, the key and the checksum are visible fields
# RU: файл — обычный текст: строка формата, ключ и контрольная сумма — видимые поля
head -1 "$CK" | grep -q '^# gramvanity checkpoint v1' \
    && ok "the checkpoint file starts with the text format header" || bad "the checkpoint file starts with the text format header"
grep -q '^key           = [0-9a-f]\{64\}' "$CK" \
    && ok "the search key is a visible hex field" || bad "the search key is a visible hex field"
grep -q '^checksum      = [0-9a-f]\{64\}' "$CK" \
    && ok "the checksum is a visible hex field" || bad "the checksum is a visible hex field"
# EN: a hand edit (the count raised from 1 to 3) without --checkpoint-fix is rejected; with the fix
# EN: it takes effect
# RU: ручная правка (счётчик поднят с 1 до 3) без --checkpoint-fix отвергается; с fix она вступает в силу
sed 's/^count .*/count         = 3/' "$CK" > "$TMP/t3b-edited.checkpoint"
if "$BIN" --resume "$TMP/t3b-edited.checkpoint" --tries 10 > "$TMP/t3ba.log" 2>&1; then
    bad "hand edit without --checkpoint-fix rejected"
else
    ok "hand edit without --checkpoint-fix rejected"
fi
"$BIN" --checkpoint-fix "$TMP/t3b-edited.checkpoint" > "$TMP/t3bb.log" 2>&1 \
    && ok "--checkpoint-fix accepts the hand edit" || bad "--checkpoint-fix accepts the hand edit"
"$BIN" --checkpoint-info "$TMP/t3b-edited.checkpoint" | grep -q 'count: 3' \
    && ok "the edited count survives --checkpoint-fix" || bad "the edited count survives --checkpoint-fix"
"$BIN" --resume "$TMP/t3b-edited.checkpoint" --tries 10 $NOVERIFY > "$TMP/t3bc.log" 2>&1 \
    && ok "resume works after --checkpoint-fix" || bad "resume works after --checkpoint-fix"
# EN: a binary file (any foreign format) is rejected as not a checkpoint
# RU: бинарный файл (любой чужой формат) отвергается как не-checkpoint
printf 'SVANCKP\0not-a-checkpoint-anymore' > "$TMP/t3bg.checkpoint"
if "$BIN" --resume "$TMP/t3bg.checkpoint" > "$TMP/t3bd.log" 2>&1; then
    bad "binary file rejected as foreign"
else
    ok "binary file rejected as foreign"
fi

echo "== T3c: a failing checkpoint save is reported =="
# EN: an explicit --checkpoint whose parent folder does not exist cannot be written. The search
# EN: must keep going and finish its own stop path, but say so: once per autosave attempt and a
# EN: final failure line instead of "checkpoint saved".
# RU: явный --checkpoint в несуществующей папке записать нельзя. Поиск продолжает работу и
# RU: доходит до своей остановки, но сообщает об этом: раз за попытку автосохранения и итоговой
# RU: строкой о неудаче вместо "checkpoint saved".
OUT="$TMP/t3c-out"; rm -rf "$TMP/t3c-missing" "$OUT"
if "$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 100000 --stop-after 3000 \
       --checkpoint-interval 1 --no-verify --checkpoint "$TMP/t3c-missing/t3c.checkpoint" \
       --out "$OUT" > "$TMP/t3c.log" 2>&1; then
    ok "a run with an unwritable checkpoint still finishes"
else
    bad "a run with an unwritable checkpoint still finishes"
fi
grep -q "checkpoint save FAILED" "$TMP/t3c.log" \
    && ok "the autosave failure is reported" || bad "the autosave failure is reported"
grep -q "this session may not be resumable" "$TMP/t3c.log" \
    && ok "the final save failure replaces the success line" || bad "the final save failure replaces the success line"
ls "$OUT"/stop-* >/dev/null 2>&1 && ok "the stop report is still written" || bad "the stop report is still written"

echo "== T4: ton-24 deterministic range with interruption =="
CK="$TMP/t4.checkpoint"; OUT="$TMP/t4-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode ton-24 --suffix ZZZZZZ --start-offset 500 --tries 60000 --stop-after 40000 \
       --checkpoint-interval 3 --no-verify --checkpoint "$CK" --out "$OUT" > "$TMP/t4a.log" 2>&1
F1=$(frontier "$CK")
if [ -n "$F1" ] && [ "$F1" -ge 40000 ] && [ "$F1" -le 90000 ]; then ok "ton stop-after frontier (=$F1)"; else bad "ton stop-after frontier (='$F1')"; fi
"$BIN" --resume "$CK" --no-verify > "$TMP/t4b.log" 2>&1
check "ton range completes after resume" "$(next_index "$CK")" "60500"

echo "== T4b: --tries on resume cannot fall behind the stage-1 frontier (ton-24) =="
# EN: kill -9 mid-stage-1 leaves next < s1_next in the checkpoint (the survivors of the gap are
# EN: not stored). A resume whose --tries ends before that gap used to search past the end, count
# EN: the hits, and then rewind det.next: the work was forgotten and would be duplicated on the
# EN: next extension. Such a resume must be refused; a resume with a large enough --tries works.
# RU: kill -9 посреди стадии 1 оставляет в checkpoint next < s1_next (выжившие из промежутка не
# RU: сохраняются). Продолжение, чей --tries заканчивается раньше промежутка, раньше искало за
# RU: пределами end, засчитывало хиты и откатывало det.next: работа забывалась и дублировалась
# RU: при следующем расширении. Такое продолжение должно быть отклонено; продолжение с
# RU: достаточным --tries работает.
CK="$TMP/t4b.checkpoint"; OUT="$TMP/t4b-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode ton-24 --suffix ZZZZZZ --start-offset 0 --tries 6000000 --checkpoint-interval 1 \
       --no-verify --checkpoint "$CK" --out "$OUT" > "$TMP/t4ba.log" 2>&1 &
T4B_PID=$!
T4B_N=""; T4B_F=""
for T4B_I in $(seq 1 100); do
    sleep 0.3
    T4B_N=$(next_index "$CK" 2>/dev/null)
    T4B_F=$(frontier "$CK" 2>/dev/null)
    [ -n "$T4B_N" ] && [ -n "$T4B_F" ] && [ "$T4B_F" -gt "$T4B_N" ] && break
done
kill -9 $T4B_PID 2>/dev/null
wait $T4B_PID 2>/dev/null
if [ -n "$T4B_N" ] && [ -n "$T4B_F" ] && [ "$T4B_F" -gt "$T4B_N" ]; then
    T4B_W=$((T4B_F - T4B_N))
    T4B_HALF=$((T4B_W / 2))
    if [ "$T4B_HALF" -ge 1 ]; then
        if "$BIN" --resume "$CK" --tries "$T4B_HALF" --no-verify > "$TMP/t4br.log" 2>&1; then
            bad "resume --tries before the stage-1 frontier is refused"
        else
            ok "resume --tries before the stage-1 frontier is refused"
        fi
        grep -q "stage-1 frontier" "$TMP/t4br.log" \
            && ok "the refusal names the stage-1 frontier" || bad "the refusal names the stage-1 frontier"
    fi
    "$BIN" --resume "$CK" --tries $((T4B_W + 60000)) --no-verify > "$TMP/t4bs.log" 2>&1
    check "resume past the frontier completes the extended range" "$(next_index "$CK")" "$((T4B_F + 60000))"
else
    bad "kill -9 leaves a stage-1 gap in the checkpoint (next='$T4B_N' frontier='$T4B_F')"
fi

echo "== T5: multiple suffix alternatives (OR inside a group) =="
CK="$TMP/t5.checkpoint"; OUT="$TMP/t5-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix 'ZZZZZ' --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 100000 \
       --count 1 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5.log" 2>&1
NEW5=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
case "$NEW5" in
    hit-t1-*) ok "multi-suffix hit keeps the target name ($NEW5)";;
    *) bad "multi-suffix hit file (got '$NEW5')";;
esac
grep -q 'endings "ZZZZZ" "\[0-9A-Za-z_-\]" (one from each group; AND)' "$OUT/$NEW5" \
    && ok "hit file lists both suffix alternatives" || bad "hit file lists both suffix alternatives"
"$BIN" --checkpoint-info "$CK" | grep -q 'endings: "ZZZZZ" ([0-9]* hit(s)), "\[0-9A-Za-z_-\]" ([0-9]* hit(s))' \
    && ok "suffix alternatives survive the checkpoint" || bad "suffix alternatives survive the checkpoint"

echo "== T5b: prefix patterns, ignore-case, checkpoint roundtrip =="
CK="$TMP/t5b.checkpoint"; OUT="$TMP/t5b-out"; rm -f "$CK"; rm -rf "$OUT"
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
"$BIN" --checkpoint-info "$CK" | grep -q 'prefixes: "AB" ([0-9]* hit(s)), "CD" ([0-9]* hit(s))' && ok "prefixes survive the checkpoint" || bad "prefixes survive the checkpoint"
if "$BIN" --resume "$CK" --prefix EF > "$TMP/t5bc.log" 2>&1; then bad "prefix conflict rejected"; else ok "prefix conflict rejected"; fi
CK="$TMP/t5c.checkpoint"; OUT="$TMP/t5c-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK="$TMP/t5f.checkpoint"; OUT="$TMP/t5f-out"; rm -f "$CK"; rm -rf "$OUT"
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
"$BIN" --checkpoint-info "$CK" | grep -q 'prefix OR suffix (--match or)' \
    && ok "--match or survives the checkpoint" || bad "--match or survives the checkpoint"
if "$BIN" --resume "$CK" --match and --tries 10 > "$TMP/t5fc.log" 2>&1; then bad "--match conflict rejected"; else ok "--match conflict rejected"; fi
"$BIN" --resume "$CK" --tries 10 --count 2 $NOVERIFY > "$TMP/t5fr.log" 2>&1 \
    && ok "resume keeps the saved --match or" || bad "resume keeps the saved --match or"
# EN: a per-target --match overrides the global default: A stays or, B is and
# RU: --match внутри цели переопределяет глобальное значение: у A остаётся or, у B — and
CK="$TMP/t5g.checkpoint"; OUT="$TMP/t5g-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --match or --start-offset 0 --tries 200000 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --prefix AB --suffix 'ZZZZZZZ' \
       --target B --match and --suffix 'ZZ' > "$TMP/t5g.log" 2>&1
NH5G=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "per-target override finds hits for both targets" "$NH5G" "2"
grep -q '(one group suffices; OR)' "$OUT"/hit-A-* \
    && ok "target A keeps the global --match or" || bad "target A keeps the global --match or"
grep -q '(one from each group; AND)' "$OUT"/hit-B-* \
    && ok "target B overrides with --match and" || bad "target B overrides with --match and"
check "only target A carries the OR flag" "$("$BIN" --checkpoint-info "$CK" | grep -c 'prefix OR suffix (--match or)')" "1"

echo "== T5g2: Cyrillic target names are transliterated and kept as labels =="
CK="$TMP/t5i.checkpoint"; OUT="$TMP/t5i-out"; rm -f "$CK"; rm -rf "$OUT"
CYR=$(printf '\320\237\320\260\321\210\320\260') # Паша
"$BIN" --mode bip39-12 --start-offset 0 --tries 200000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" --target "$CYR" --prefix AB > "$TMP/t5i.log" 2>&1
H5I=$(ls "$OUT" 2>/dev/null | grep '^hit-Pasha-' | head -1)
if [ -n "$H5I" ] && grep -q "target Pasha ($CYR)" "$OUT/$H5I"; then
    ok "Cyrillic name becomes the ASCII id Pasha and stays as its label"
else
    bad "Cyrillic name becomes the ASCII id Pasha and stays as its label"
fi
"$BIN" --checkpoint-info "$CK" | grep -q "label: $CYR" \
    && ok "Cyrillic label survives the checkpoint" || bad "Cyrillic label survives the checkpoint"
# EN: resume by the Cyrillic label (raise the count so the run actually starts)
# RU: продолжаем по кириллической метке (поднимаем счётчик, чтобы запуск действительно начался)
OUT5I="$TMP/t5i-out2"; rm -rf "$OUT5I"
"$BIN" --resume "$CK" --count 2 --tries 10 $NOVERIFY --out "$OUT5I" --target "$CYR" > "$TMP/t5ir.log" 2>&1 \
    && ok "resume finds a target by its Cyrillic label" || bad "resume finds a target by its Cyrillic label"

echo "== T5h: --all-patterns: --count applies per pattern variant =="
CK="$TMP/t5h.checkpoint"; OUT="$TMP/t5h-out"; rm -f "$CK"; rm -rf "$OUT"
# EN: two disjoint, easily findable prefix variants: the run must not stop after the first one
# RU: два непересекающихся легко находимых варианта префикса: запуск не должен останавливаться на первом
"$BIN" --mode bip39-12 --all-patterns --start-offset 0 --tries 2000000 --count 1 $NOVERIFY \
       --checkpoint "$CK" --out "$OUT" --prefix AB --prefix CD > "$TMP/t5h.log" 2>&1
NH5H=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "--all-patterns keeps searching until every variant has its hit" "$NH5H" "2"
"$BIN" --checkpoint-info "$CK" | grep -q -- '--all-patterns' \
    && ok "--all-patterns survives the checkpoint" || bad "--all-patterns survives the checkpoint"
PH5H=$("$BIN" --checkpoint-info "$CK" | sed -n 's/^      prefixes: //p')
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
CK="$TMP/t5d.checkpoint"; OUT="$TMP/t5d-out"; rm -f "$CK"; rm -rf "$OUT"
PFX=""; I=0
while [ "$I" -lt 12 ]; do PFX="${PFX:+$PFX,}[0-9A-Za-z_-]"; I=$((I+1)); done
"$BIN" --mode bip39-12 --prefix "$PFX" --start-offset 0 --tries 100000 --count 1 \
       $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t5d.log" 2>&1
NEW5D=$(ls "$OUT" 2>/dev/null | grep '^hit-' | head -1)
if [ -n "$NEW5D" ]; then ok "twelve prefixes work at once ($NEW5D)"; else bad "twelve prefixes work at once"; fi
NP5D=$("$BIN" --checkpoint-info "$CK" | sed -n 's/^      prefixes: //p' | grep -o '([0-9]* hit(s))' | wc -l | tr -d ' ')
check "twelve prefixes survive the checkpoint" "$NP5D" "12"

echo "== T5e: default count is one hit per target =="
CK="$TMP/t5e.checkpoint"; OUT="$TMP/t5e-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --prefix AB --start-offset 0 --tries 100000 --no-verify \
       --checkpoint "$CK" --out "$OUT" > "$TMP/t5e.log" 2>&1
NH5E=$(ls "$OUT" 2>/dev/null | grep -c '^hit-')
check "a target without --count stops after one hit" "$NH5E" "1"
N5E=$(next_index "$CK")
if [ -n "$N5E" ] && [ "$N5E" -lt 100000 ]; then ok "the default count stops the run early (next=$N5E)"; else bad "the default count stops the run early (next='$N5E')"; fi
grep -q 'all target counts reached' "$OUT"/stop-* 2>/dev/null && ok "the default count ends the run" || bad "the default count ends the run"

echo "== T6: tg-wallet (WalletTg) hit, replay, signing half, testnet =="
CK="$TMP/t6.checkpoint"; OUT="$TMP/t6-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK6T="$TMP/t6t.checkpoint"; OUT="$TMP/t6t-out"; rm -f "$CK6T"; rm -rf "$OUT"
"$BIN" --mode tg-wallet --net testnet --suffix '[0-9A-Za-z_-]' --start-offset 0 --tries 1000000 --count 1 \
       $NOVERIFY --checkpoint "$CK6T" --out "$OUT" > "$TMP/t6t.log" 2>&1
"$BIN" --checkpoint-info "$CK6T" | grep -q "WalletTg (Telegram Wallet) testnet" && ok "testnet flag survives the checkpoint" || bad "testnet flag survives the checkpoint"
if "$BIN" --mode bip39-12 --net testnet --suffix X --start-offset 0 --out "$TMP/t6v-out" > "$TMP/t6v.log" 2>&1; then
    bad "--net testnet rejected for other modes"
else
    ok "--net testnet rejected for other modes"
fi

echo "== T7: --power validation, determinism across power levels =="
if "$BIN" --power 0 --suffix ZZZZZZ --out "$TMP/t7bad" > "$TMP/t7a.log" 2>&1; then bad "--power 0 rejected"; else ok "--power 0 rejected"; fi
if "$BIN" --power 101 --suffix ZZZZZZ --out "$TMP/t7bad" > "$TMP/t7b.log" 2>&1; then bad "--power 101 rejected"; else ok "--power 101 rejected"; fi
CK="$TMP/t7.checkpoint"; OUT="$TMP/t7-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --suffix ZZZZZZ --start-offset 0 --tries 4000 $NOVERIFY --checkpoint "$CK" --out "$OUT" > "$TMP/t7c.log" 2>&1
cp "$CK" "$TMP/t7b.checkpoint"
"$BIN" --resume "$CK" --tries 8000 --power 100 $NOVERIFY > "$TMP/t7d.log" 2>&1
"$BIN" --resume "$TMP/t7b.checkpoint" --tries 8000 --power 20 $NOVERIFY > "$TMP/t7e.log" 2>&1
check "power 100 processes the same range" "$(next_index "$CK")" "12000"
check "power 20 processes the same range" "$(next_index "$TMP/t7b.checkpoint")" "12000"
P1=$("$BIN" --reveal "$CK" 5000 | grep 'phrase (SECRET' | sed 's/.*: //')
P2=$("$BIN" --reveal "$TMP/t7b.checkpoint" 5000 | grep 'phrase (SECRET' | sed 's/.*: //')
if [ -n "$P1" ] && [ "$P1" = "$P2" ]; then ok "same index gives the same phrase at any power"; else bad "same index gives the same phrase at any power"; fi

echo "== T8: target blocks, per-target counts, shared and separate outs =="
# EN: a) both targets match every address; one address, one shared hit file
# RU: a) обе цели совпадают с любым адресом; один адрес — один общий hit-файл
CK="$TMP/t8a.checkpoint"; OUT="$TMP/t8a-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK="$TMP/t8b.checkpoint"; OUT="$TMP/t8b-out"; rm -f "$CK"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --start-offset 0 --tries 200 --count 1 --out "$OUT" --checkpoint "$CK" --no-verify \
       --target A --suffix '[0-9A-Za-z_-]' --out "$OUT/a" \
       --target B --suffix '[0-9A-Za-z_-]' --out "$OUT/b" > "$TMP/t8b.log" 2>&1
[ -f "$OUT/a/hit-A+B-0.txt" ] && ok "target A writes to its own --out" || bad "target A writes to its own --out"
[ -f "$OUT/b/hit-A+B-0.txt" ] && ok "target B writes to its own --out" || bad "target B writes to its own --out"
ls "$OUT" | grep -q '^stop-' && ok "the stop report stays in the job folder" || bad "the stop report stays in the job folder"
[ -f "$TMP/t8b.checkpoint" ] && ok "explicit checkpoint stays outside the job folder" || bad "explicit checkpoint stays outside the job folder"

# EN: c) per-target counts: A stops after one hit, B after two (the range stays open, so the run ends
# EN:    because both target counts are reached, not because the range ended)
# RU: c) счётчики на цель: A останавливается после одного хита, B — после двух (диапазон открыт,
# RU:    поэтому запуск завершается по достижению счётчиков, а не по концу диапазона)
CK="$TMP/t8c.checkpoint"; OUT="$TMP/t8c-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK="$TMP/t8e.checkpoint"; OUT="$TMP/t8e-out"; rm -f "$CK"; rm -rf "$OUT"
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
CK="$TMP/t8f.checkpoint"; OUT="$TMP/t8f-out"; rm -f "$CK"; rm -rf "$OUT"
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
    [ -f "$NEWJOB/job.checkpoint" ] && ok "the default checkpoint is <job>/job.checkpoint" || bad "the default checkpoint is <job>/job.checkpoint"
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
[ -f "$OUT/job.checkpoint" ] && ok "a default job is deterministic and writes job.checkpoint" || bad "a default job is deterministic and writes job.checkpoint"
grep -q 'deterministic range' "$TMP/t10a.log" && ok "a default job reports a deterministic range" || bad "a default job reports a deterministic range"
grep -q 'deterministic search: indices' "$OUT"/stop-* 2>/dev/null && ok "the stop report of a default job is resumable" || bad "the stop report of a default job is resumable"
N10=$(next_index "$OUT/job.checkpoint")
"$BIN" --mode bip39-12 --resume "$OUT/job.checkpoint" --stop-after 1 --no-verify > "$TMP/t10b.log" 2>&1
grep -q 'resuming bip39-12 search' "$TMP/t10b.log" && ok "a default job resumes without extra flags" || bad "a default job resumes without extra flags"
N10B=$(next_index "$OUT/job.checkpoint")
if [ -n "$N10" ] && [ -n "$N10B" ] && [ "$N10B" -gt "$N10" ]; then ok "resume advanced the default job"; else bad "resume advanced the default job"; fi

OUT="$TMP/t10c-out"; rm -rf "$OUT"
"$BIN" --mode bip39-12 --random --suffix '[0-9A-Za-z_-]' --count 1 --no-verify --out "$OUT" > "$TMP/t10c.log" 2>&1
[ ! -f "$OUT/job.checkpoint" ] && ok "--random writes no checkpoint" || bad "--random writes no checkpoint"
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

echo "== T12: --checkpoint-merge consolidates unfinished targets =="
CKA="$TMP/t12a.checkpoint"; OUTA="$TMP/t12a-out"
CKB="$TMP/t12b.checkpoint"; OUTB="$TMP/t12b-out"
CKC="$TMP/t12c.checkpoint"; OUTC="$TMP/t12c-out"
rm -f "$CKA" "$CKB" "$CKC"; rm -rf "$OUTA" "$OUTB" "$OUTC"
# EN: job A: X reaches its count, Y does not; job B (default checkpoint in its job folder):
# EN: Z stays without hits; job C is tg-wallet. --out goes before the --target blocks: there it is
# EN: the job folder, not a per-target hit directory.
# RU: задание A: X добирает свой count, Y нет; задание B (checkpoint по умолчанию в своей папке):
# RU: Z без хитов; задание C — tg-wallet. --out стоит до блоков --target: там это папка задания,
# RU: а не каталог хитов отдельной цели.
"$BIN" --mode bip39-12 --out "$OUTA" --target X --suffix '[0-9A-Za-z_-]' --target Y --suffix ZZZZZZ \
       --count 1 --tries 20000 --checkpoint-interval 5 $NOVERIFY --checkpoint "$CKA" > "$TMP/t12a.log" 2>&1
check "job A: target X reached its count" "$(target_hits "$CKA" X)" "1"
check "job A: target Y is still unfinished" "$(target_hits "$CKA" Y)" "0"
"$BIN" --mode bip39-12 --out "$OUTB" --target Z --suffix ZZZZZZ --count 2 --tries 3000 \
       --checkpoint-interval 5 $NOVERIFY > "$TMP/t12b.log" 2>&1
check "job B: target Z has no hits" "$(target_hits "$OUTB/job.checkpoint" Z)" "0"
"$BIN" --suffix ZZZZZZ --count 1 --tries 100 --checkpoint-interval 5 $NOVERIFY \
       --checkpoint "$CKC" --out "$OUTC" > "$TMP/t12c.log" 2>&1
MOUT="$TMP/t12-merged"; rm -rf "$MOUT"
# EN: --in takes both forms: a checkpoint file and a job folder
# RU: --in принимает обе формы: файл checkpoint и папку задания
"$BIN" --checkpoint-merge --in "$CKA" --in "$OUTB" --out "$MOUT" > "$TMP/t12m.log" 2>&1
MCK="$MOUT/job.checkpoint"
[ -f "$MCK" ] && ok "merge writes job.checkpoint into the --out folder" || bad "merge writes job.checkpoint into the --out folder"
grep -q '2 target(s) kept, 1 done target(s) dropped' "$TMP/t12m.log" \
    && ok "merge keeps the unfinished targets and drops the done one" || bad "merge keeps the unfinished targets and drops the done one"
check "merged: the done target X is gone" "$(target_hits "$MCK" X)" ""
check "merged: Y is kept" "$(target_hits "$MCK" Y)" "0"
check "merged: Z is kept" "$(target_hits "$MCK" Z)" "0"
"$BIN" --checkpoint-info "$MCK" | grep -q 'job folder: (not stored)' \
    && ok "the merged file stores no job folder (its own folder is the job folder)" || bad "the merged file stores no job folder (its own folder is the job folder)"
grep -q '^job_dir' "$MCK" && bad "the merged file has no job_dir line" || ok "the merged file has no job_dir line"
"$BIN" --resume "$MCK" --tries 3000 $NOVERIFY > "$TMP/t12r.log" 2>&1
check "the merged job resumes from index 0" "$(next_index "$MCK")" "3000"
MOUT2="$TMP/t12-mixed"; rm -rf "$MOUT2"
if "$BIN" --checkpoint-merge --in "$CKA" --in "$CKC" --out "$MOUT2" > "$TMP/t12x.log" 2>&1; then
    bad "merge rejects mixed modes"
else
    ok "merge rejects mixed modes"
fi
# EN: without --out the merged job folder follows the default results/job-<timestamp> rule
# RU: без --out папка объединённого задания подчиняется правилу results/job-<метка> по умолчанию
BEFORE="$TMP/t12-jobs-before"; AFTER="$TMP/t12-jobs-after"
ls -d ../results/job-*/ 2>/dev/null | sort > "$BEFORE" || : > "$BEFORE"
"$BIN" --checkpoint-merge --in "$CKA" --in "$OUTB" > "$TMP/t12d.log" 2>&1
ls -d ../results/job-*/ 2>/dev/null | sort > "$AFTER" || : > "$AFTER"
NEWMERGE=$(comm -13 "$BEFORE" "$AFTER" | head -1)
if [ -n "$NEWMERGE" ] && [ -f "$NEWMERGE/job.checkpoint" ]; then
    ok "merge without --out creates results/job-<timestamp>/ with job.checkpoint ($NEWMERGE)"
    rm -rf "$NEWMERGE"
else
    bad "merge without --out creates results/job-<timestamp>/ with job.checkpoint"
fi

echo
echo "passed $PASS, failed $FAIL"
[ "$FAIL" -eq 0 ]
