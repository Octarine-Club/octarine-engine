#!/usr/bin/env bash
# Deletes GitHub Actions artifacts in the target repository.
# Usage:
#   scripts/clean-actions-artifacts.sh [owner/repo]
# Default repo: Octarine-Club/octarine-engine

set -euo pipefail

REPO="${1:-Octarine-Club/octarine-engine}"

echo "Fetching artifacts for $REPO..."
artifact_ids=$(gh api --paginate "repos/$REPO/actions/artifacts" --jq '.artifacts[].id' || true)

if [ -z "$artifact_ids" ]; then
  echo "No artifacts found in $REPO."
  exit 0
fi

count=$(echo "$artifact_ids" | wc -l | tr -d ' ')
echo "Found $count artifacts in $REPO. Starting deletion..."

deleted=0
for id in $artifact_ids; do
  deleted=$((deleted + 1))
  printf "Deleting artifact %d/%d (id: %s)... " "$deleted" "$count" "$id"
  gh api -X DELETE "repos/$REPO/actions/artifacts/$id" --silent
  echo "deleted."
done

echo "Successfully cleaned up $deleted artifacts from $REPO."
