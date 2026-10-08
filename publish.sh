#!/usr/bin/env bash
# Publish Picori Co-op to GitHub in one go:
#   fork 999sian/tmc under your account as "minishcap-coop" (once),
#   push branch coop/main and tag v0.1.0, make coop/main the default branch,
#   create GitHub release v0.1.0 with notes from CHANGELOG.md (no binaries, no ROM).
# Safe to run again: every step skips what is already done.
set -euo pipefail

UPSTREAM="999sian/tmc"
REPO_NAME="minishcap-coop"
BRANCH="coop/main"
TAG="v0.1.0"
REMOTE="coop"

cd "$(dirname "$0")"

say() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nОшибка: %s\n' "$*" >&2; exit 1; }

if ! command -v gh >/dev/null 2>&1; then
    cat <<'EOF'
Не найден GitHub CLI (gh). Установи его и войди в аккаунт:

    brew install gh
    gh auth login

(в gh auth login выбери GitHub.com → HTTPS → Login with a web browser)
Потом запусти публикацию ещё раз.
EOF
    exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
    cat <<'EOF'
GitHub CLI установлен, но ты не вошёл в аккаунт. Выполни:

    gh auth login

(выбери GitHub.com → HTTPS → Login with a web browser)
Потом запусти публикацию ещё раз.
EOF
    exit 1
fi

git rev-parse --verify --quiet "refs/heads/$BRANCH" >/dev/null || fail "нет локальной ветки $BRANCH"
git rev-parse --verify --quiet "refs/tags/$TAG" >/dev/null || fail "нет локального тега $TAG"

LOGIN="$(gh api user --jq .login)"
say "Аккаунт GitHub: $LOGIN"

# The fork: minishcap-coop, or an earlier fork of 999sian/tmc under another name.
find_fork() {
    if gh repo view "$LOGIN/$REPO_NAME" --json name --jq .name >/dev/null 2>&1; then
        echo "$REPO_NAME"
        return
    fi
    gh api --paginate "repos/$UPSTREAM/forks?per_page=100" \
        --jq ".[] | select(.owner.login == \"$LOGIN\") | .name" 2>/dev/null | head -n 1
}
NAME="$(find_fork)"
if [ -z "$NAME" ]; then
    say "Создаю форк $UPSTREAM → $LOGIN/$REPO_NAME"
    gh repo fork "$UPSTREAM" --fork-name "$REPO_NAME" --clone=false --default-branch-only
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        NAME="$(find_fork)"
        [ -n "$NAME" ] && break
        sleep 3
    done
    [ -n "$NAME" ] || fail "форк не появился на GitHub, запусти ещё раз через минуту"
else
    say "Форк уже есть: $LOGIN/$NAME"
fi
FULL="$LOGIN/$NAME"
URL="https://github.com/$FULL.git"

if git remote get-url "$REMOTE" >/dev/null 2>&1; then
    git remote set-url "$REMOTE" "$URL"
else
    git remote add "$REMOTE" "$URL"
fi

gh auth setup-git >/dev/null 2>&1 || true

say "Отправляю ветку $BRANCH и тег $TAG в $FULL"
git push "$REMOTE" "refs/heads/$BRANCH:refs/heads/$BRANCH"
git push "$REMOTE" "refs/tags/$TAG:refs/tags/$TAG"

say "Делаю $BRANCH веткой по умолчанию"
gh repo edit "$FULL" --default-branch "$BRANCH" \
    --description "Picori Co-op: local two-player co-op for the Minish Cap PC port (Project Picori)"

if gh release view "$TAG" -R "$FULL" >/dev/null 2>&1; then
    say "Релиз $TAG уже есть"
else
    say "Создаю релиз $TAG"
    NOTES="$(mktemp)"
    trap 'rm -f "$NOTES"' EXIT
    awk '/^## Picori Co-op v0\.1\.0/ { on = 1; next } on && /^## / { exit } on { print }' CHANGELOG.md > "$NOTES"
    printf '\nBuild from source with your own USA ROM: see README-COOP.md. No ROM or binaries are attached.\n' >> "$NOTES"
    gh release create "$TAG" -R "$FULL" --verify-tag --title "Picori Co-op $TAG" --notes-file "$NOTES"
fi

say "Готово: https://github.com/$FULL"
