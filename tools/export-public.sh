#!/bin/bash
#
# Build the public tree (RedHatOnTop/walkman-a55) from a commit of this private
# repository and commit it onto the local branch `public`, which the `public`
# remote's push refspec sends to its main. The working tree and the index are
# not touched.
#
#   tools/export-public.sh [REV]        REV defaults to HEAD
#   tools/export-public.sh -n DIR [REV] write the tree into DIR (empty or new),
#                                       run the checks, commit nothing
#
# The public tree is REV's tree with the DROP paths removed, public/ copied
# over the root (README, LICENSE, the modules' COPYING), every serial listed
# in work/serial (gitignored, one per line) replaced by <serial>, and paths
# under $HOME written from ~. It refuses to commit while a listed serial, a path
# under $HOME, a tailnet address, a restrictive licence header or a dropped path
# is still there. The first commit is a fresh root, so the private history
# stays here; later ones stack on `public`.
#
set -euo pipefail

OUT=
if [ "${1:-}" = -n ]; then
    OUT="${2:?-n needs a directory}"
    shift 2
    mkdir -p "$OUT"
    [ -z "$(ls -A "$OUT")" ] || { echo "$OUT is not empty" >&2; exit 1; }
fi
GIT_DIR="$(cd "$(git rev-parse --git-dir)" && pwd)"
export GIT_DIR
REV="$(git rev-parse --verify "${1:-HEAD}^{commit}")"
# work/ of the main checkout, also from a linked worktree.
SERIALS="$(dirname "$(cd "$(git rev-parse --git-common-dir)" && pwd)")/work/serial"

# Kept private:
DROP=(
    AGENTS.md HANDOFF.md          # notes for the agents working in this repository
    debloat                       # component metadata pulled from Fire OS's APKs
    mcp-server                    # a local server over the private workspace
    probe/kmod/swap-accounting-guard/quarantine   # kept for the record, never to run
    public                        # copied over the root instead
)
[ -s "$SERIALS" ] || { echo "no $SERIALS: list the serials to scrub there" >&2; exit 1; }
mapfile -t serial < <(sed 's/[[:space:]]//g; /^$/d' "$SERIALS")

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
TREE="$TMP/tree"
mkdir "$TREE"
git archive "$REV" | tar -x -C "$TREE"
for p in "${DROP[@]}"; do
    rm -rf "${TREE:?}/$p"
done
git archive "$REV" public | tar -x -C "$TMP"
cp -a "$TMP/public/." "$TREE/"
# git archive stamped REV, a commit of this repository, into kit/COMMIT; the public
# repository's own archive stamps its commit there instead.
git show "$REV:kit/COMMIT" > "$TREE/kit/COMMIT"

for s in "${serial[@]}"; do
    grep -rlF -- "$s" "$TREE" | while IFS= read -r f; do
        sed -i "s/$s/<serial>/g" "$f"
    done
done

# The notes give paths under this machine's home directory.
grep -rlF -- "$HOME/" "$TREE" | while IFS= read -r f; do
    sed -i "s|$HOME/|~/|g" "$f"
done

bad=0
if grep -rqF -- "$HOME/" "$TREE"; then echo "a path under $HOME is still in the tree" >&2; bad=1; fi
for s in "${serial[@]}"; do
    if grep -rqF -- "$s" "$TREE"; then echo "a listed serial is still in the tree" >&2; bad=1; fi
done
if grep -rnE '\b100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\.[0-9]{1,3}\.[0-9]{1,3}\b' "$TREE"; then
    echo "tailnet addresses above" >&2; bad=1
fi
restricted="$(grep -rliE 'confidential and proprietary|may be used only as authori[sz]ed' "$TREE" |
    grep -vxF "$TREE/tools/export-public.sh" || true)"
if [ -n "$restricted" ]; then
    echo "restrictive licence headers in: $restricted" >&2; bad=1
fi
for p in "${DROP[@]}"; do
    if [ -e "$TREE/$p" ]; then echo "$p is still there" >&2; bad=1; fi
done
if [ -n "$OUT" ]; then
    cp -a "$TREE/." "$OUT/"
    echo "tree of $REV in $OUT ($(find "$OUT" -type f | wc -l) files), checks $([ "$bad" = 0 ] && echo passed || echo FAILED)"
    exit "$bad"
fi
[ "$bad" = 0 ] || exit 1

export GIT_INDEX_FILE="$TMP/index"
(cd "$TREE" && git --work-tree="$TREE" add -A -f -- .)
tree="$(git write-tree)"
parent="$(git rev-parse -q --verify refs/heads/public || true)"
if [ -n "$parent" ] && [ "$tree" = "$(git rev-parse "$parent^{tree}")" ]; then
    echo "public: unchanged at $parent"
    exit 0
fi
msg="Public tree of nw-a55 $(git log -1 --format=%cs "$REV")

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
commit="$(git commit-tree "$tree" ${parent:+-p "$parent"} -m "$msg")"
git update-ref refs/heads/public "$commit" ${parent:+"$parent"}
echo "public: $commit ($(git ls-tree -r --name-only "$commit" | wc -l) files)"
